import ipaddress
import logging
import re

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import binary_sensor, number, sensor, switch, text_sensor
from esphome.core import CORE, Lambda
from esphome.const import (
    CONF_ID,
    CONF_IP_ADDRESS,
    CONF_NAME,
    CONF_PORT,
    CONF_STEP,
    CONF_TIMEOUT,
    CONF_UPDATE_INTERVAL,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_TOTAL_INCREASING,
)

DOMAIN = "wago_netvar"

_LOGGER = logging.getLogger(__name__)

# "network" zamiast "wifi": komponent działa zarówno przez Wi-Fi, jak i Ethernet.
DEPENDENCIES = ["network"]
# Komponent tworzy encje wszystkich tych rodzajów (także sensory statystyk).
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor", "switch", "number"]
MULTI_CONF = True

MAX_UDP_PACKET_SIZE = 1472
NETVAR_HEADER_SIZE = 20
MAX_PAYLOAD_SIZE = MAX_UDP_PACKET_SIZE - NETVAR_HEADER_SIZE
SCHEDULER_DONT_RUN = 4294967295  # wartość używana przez ESPHome dla "update_interval: never"

wago_netvar_ns = cg.esphome_ns.namespace("wago_netvar")

WagoNetVarComponent = wago_netvar_ns.class_("WagoNetVarComponent", cg.PollingComponent)
WagoSwitch = wago_netvar_ns.class_("WagoSwitch", switch.Switch)
WagoNumber = wago_netvar_ns.class_("WagoNumber", number.Number)

# --- Konfiguracja listy (nazwy takie same jak w integracji Home Assistant) ---
CONF_LIST_ID = "list_id"
CONF_MODE = "mode"
CONF_DIRECTION = "direction"  # alias mode
CONF_PACK_VARIABLES = "pack_variables"
CONF_VAR_COUNT = "var_count"
CONF_PAYLOAD_SIZE = "payload_size"
CONF_ON_CHANGE = "on_change"
CONF_SEND_ON_CHANGE = "send_on_change"  # alias on_change
CONF_MIN_INTERVAL = "min_interval"
CONF_REQUIRE_VALUES = "require_values"  # lista write pakowana
CONF_VARIABLES = "variables"

# --- Konfiguracja zmiennej (nazwy takie same jak w integracji Home Assistant) ---
CONF_UNIQUE_ID = "unique_id"
CONF_OFFSET = "offset"
CONF_BYTE_OFFSET = "byte_offset"  # alias offset (także nazwa wewnętrzna po walidacji)
CONF_INDEX = "index"
CONF_TYPE = "type"
CONF_LENGTH = "length"
CONF_ENTITY = "entity"
CONF_FORMAT = "format"
CONF_INITIAL_VALUE = "initial_value"
CONF_RESTORE_VALUE = "restore_value"
CONF_REQUIRE_VALUE = "require_value"  # zmienna listy write niepakowanej
CONF_MIN = "min"
CONF_MAX = "max"
CONF_ZERO_AS_UNAVAILABLE = "zero_as_unavailable"
CONF_SUGGESTED_DISPLAY_PRECISION = "suggested_display_precision"  # w ESPHome: accuracy_decimals
CONF_ENABLED_BY_DEFAULT = "enabled_by_default"  # w ESPHome: disabled_by_default (odwrotnie)
CONF_INVERTED = "inverted"
CONF_SOURCE = "source"  # sensor / binary_sensor listy write: encja, z której pochodzi wartość do wysyłki

# Klucze wewnętrzne wyniku walidacji zmiennej
CONF_VAR_NAME = "name"
CONF_VAR_TYPE = "type_name"
CONF_KIND = "kind"  # rodzaj encji ESPHome: sensor, text_sensor, binary_sensor, switch, number
CONF_ENTITY_CONFIG = "entity_config"

# Statystyki: liczniki listy (rx dla read, tx dla write) oraz sumy wszystkich list
STATS_READ = {
    "rx_packets": "set_rx_packets_sensor",
    "rx_errors": "set_rx_errors_sensor",
    "rejected_packets": "set_rejected_packets_sensor",
}
STATS_WRITE = {
    "tx_packets": "set_tx_packets_sensor",
    "tx_errors": "set_tx_errors_sensor",
}
STATS_TOTAL = {
    "total_rx_packets": "set_total_rx_packets_sensor",
    "total_rx_errors": "set_total_rx_errors_sensor",
    "total_rejected_packets": "set_total_rejected_packets_sensor",
    "total_tx_packets": "set_total_tx_packets_sensor",
    "total_tx_errors": "set_total_tx_errors_sensor",
}
CONF_STATS = "_stats"
CONF_TOTALS = "_totals"

TYPE_ALIASES = {
    "TOD": "TIME_OF_DAY",
    "DT": "DATE_AND_TIME",
}

# typ -> rozmiar w bajtach; LREAL celowo pominięty (nieobsługiwany przez NetVar)
TYPE_SIZES = {
    "BOOL": 1,
    "BYTE": 1,
    "USINT": 1,
    "SINT": 1,
    "WORD": 2,
    "UINT": 2,
    "INT": 2,
    "DWORD": 4,
    "UDINT": 4,
    "DINT": 4,
    "TIME": 4,
    "DATE": 4,
    "TIME_OF_DAY": 4,
    "DATE_AND_TIME": 4,
    "REAL": 4,
}

# zakresy typów całkowitych (domyślne min/max encji number); REAL i STRING wymagają jawnego min i max
TYPE_RANGES = {
    "BYTE": (0, 255),
    "USINT": (0, 255),
    "SINT": (-128, 127),
    "WORD": (0, 65535),
    "UINT": (0, 65535),
    "INT": (-32768, 32767),
    "DWORD": (0, 4294967295),
    "UDINT": (0, 4294967295),
    "DINT": (-2147483648, 2147483647),
    "TIME": (0, 4294967295),
    "DATE": (0, 4294967295),
    "TIME_OF_DAY": (0, 4294967295),
    "DATE_AND_TIME": (0, 4294967295),
}

NUMERIC_TYPES = set(TYPE_SIZES) - {"BOOL"}
# Typy czasowe: sensor z format: text (domyślnie) pokazuje tekst IEC (T#1s500ms, D#2026-11-08, ...),
# z format: raw surową liczbę.
TEMPORAL_TYPES = {"TIME", "DATE", "TIME_OF_DAY", "DATE_AND_TIME"}

ENTITY_KINDS = ("sensor", "binary_sensor", "switch", "number")
# Każdy rodzaj encji działa w obu rodzajach list. Lista read: encja pokazuje wartość z PLC (switch i number są
# tylko lustrem wartości, polecenia z interfejsu są odrzucane). Lista write: switch i number sterują wysyłaną
# wartością, a sensor i binary_sensor są zmiennymi "tylko do wysyłki" (wartość z source, initial_value albo lambdy).
READ_ENTITIES = ("sensor", "binary_sensor", "switch", "number")
WRITE_ENTITIES = ("sensor", "binary_sensor", "switch", "number")

# Encje, których stan może być źródłem (source) zmiennej listy write, oraz zgodność z typem zmiennej
SOURCE_CLASSES = {
    "sensor": sensor.Sensor,
    "number": number.Number,
    "binary_sensor": binary_sensor.BinarySensor,
    "switch": switch.Switch,
    "text_sensor": text_sensor.TextSensor,
}
SOURCE_KINDS_NUMERIC = ("sensor", "number")
SOURCE_KINDS_BOOL = ("binary_sensor", "switch")
SOURCE_KINDS_STRING = ("text_sensor",)

_POLISH = str.maketrans("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ", "acelnoszzACELNOSZZ")


def slugify(text):
    """Identyfikator z nazwy: małe litery, cyfry i podkreślenia (polskie znaki bez ogonków)."""
    return re.sub(r"[^a-z0-9]+", "_", str(text).translate(_POLISH).lower()).strip("_")



STATS_SENSOR_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=0,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    state_class=STATE_CLASS_TOTAL_INCREASING,
)

# Pełne schematy encji ESPHome: zmienna przyjmuje wszystkie parametry danego rodzaju encji
# (device_class, state_class, unit_of_measurement, filters, on_value, internal itd.).
SENSOR_SCHEMA = sensor.sensor_schema()
TEXT_SENSOR_SCHEMA = text_sensor.text_sensor_schema()
BINARY_SENSOR_SCHEMA = binary_sensor.binary_sensor_schema()
SWITCH_SCHEMA = switch.switch_schema(WagoSwitch)
NUMBER_SCHEMA = number.number_schema(WagoNumber)

PLATFORM_SCHEMAS = {
    "sensor": SENSOR_SCHEMA,
    "text_sensor": TEXT_SENSOR_SCHEMA,
    "binary_sensor": BINARY_SENSOR_SCHEMA,
    "switch": SWITCH_SCHEMA,
    "number": NUMBER_SCHEMA,
}


def time_value(default_unit):
    """Czas: liczba (w default_unit) albo tekst z jednostką (500ms, 5s, 2min, 1h)."""

    def convert(value):
        if isinstance(value, bool):
            raise cv.Invalid("Expected a time period such as 500ms, 5s, 2min")
        if isinstance(value, (int, float)):
            return f"{value}{default_unit}"
        if isinstance(value, str) and value.strip().lower() == "never":
            return "0s"
        return value

    return convert


def migrate_legacy_keys(config):
    """Aliasy nazw (zgodne z integracją HA): direction = mode, send_on_change = on_change.

    Czasy podane jako liczby są zamieniane na tekst z jednostką: min_interval w milisekundach,
    update_interval i timeout w sekundach.
    """
    if CONF_DIRECTION in config:
        if CONF_MODE in config:
            raise cv.Invalid("Use only 'mode' ('direction' is its alias)")
        config[CONF_MODE] = config.pop(CONF_DIRECTION)
    if CONF_SEND_ON_CHANGE in config:
        if CONF_ON_CHANGE in config:
            raise cv.Invalid("Use only 'on_change' ('send_on_change' is its alias)")
        config[CONF_ON_CHANGE] = config.pop(CONF_SEND_ON_CHANGE)
    for key, unit in ((CONF_MIN_INTERVAL, "ms"), (CONF_UPDATE_INTERVAL, "s"), (CONF_TIMEOUT, "s")):
        if key in config:
            config[key] = time_value(unit)(config[key])
    return config


def validate_variable_type(value):
    """Normalizuje nazwę typu (wielkość liter, aliasy, STRING bez długości = STRING(80))."""
    value = value.strip().upper()
    value = TYPE_ALIASES.get(value, value)

    if value in TYPE_SIZES:
        return value

    match = re.fullmatch(r"STRING(?:\((\d+)\))?", value)
    if match:
        if match.group(1) is None:
            return "STRING(80)"
        length = int(match.group(1))
        if length < 1 or length > 255:
            raise cv.Invalid("STRING length must be between 1 and 255")
        return f"STRING({length})"

    raise cv.Invalid(f"Unsupported NetVar type: {value}")


def is_string_type(type_name):
    return type_name.startswith("STRING(")


def get_type_size(type_name):
    if is_string_type(type_name):
        return int(type_name[7:-1]) + 1  # + kończący bajt 0x00
    return TYPE_SIZES[type_name]


def assign_offsets_and_indices(variables, packed):
    """Uzupełnia byte_offset i index oraz sprawdza ich spójność (te same reguły co w integracji HA)."""
    if packed:
        for variable in variables:
            if CONF_BYTE_OFFSET not in variable:
                raise cv.Invalid(f"Variable '{variable[CONF_VAR_NAME]}' requires byte_offset")
            variable.pop(CONF_INDEX, None)
        return

    used = set()
    for variable in variables:
        if CONF_INDEX in variable:
            if variable[CONF_INDEX] in used:
                raise cv.Invalid(f"Duplicate variable index: {variable[CONF_INDEX]}")
            used.add(variable[CONF_INDEX])

    next_offset = 0
    next_index = 0
    for variable in variables:
        if CONF_BYTE_OFFSET not in variable:
            variable[CONF_BYTE_OFFSET] = next_offset
        next_offset = max(next_offset, variable[CONF_BYTE_OFFSET] + get_type_size(variable[CONF_VAR_TYPE]))
        if CONF_INDEX not in variable:
            while next_index in used:
                next_index += 1
            variable[CONF_INDEX] = next_index
            used.add(next_index)


def ranges_overlap(start_a, size_a, start_b, size_b):
    return start_a < start_b + size_b and start_b < start_a + size_a


def validate_layout(variables):
    """Sprawdza ręcznie podane offsety: zakres ładunku oraz brak nakładania się zmiennych."""
    reserved = []  # (nazwa, offset, rozmiar)

    for variable in variables:
        name = variable[CONF_VAR_NAME]
        offset = variable[CONF_BYTE_OFFSET]
        size = get_type_size(variable[CONF_VAR_TYPE])

        if offset + size > MAX_PAYLOAD_SIZE:
            raise cv.Invalid(
                f"Variable '{name}' exceeds the maximum payload size of {MAX_PAYLOAD_SIZE} bytes"
            )
        for other_name, other_offset, other_size in reserved:
            if ranges_overlap(offset, size, other_offset, other_size):
                raise cv.Invalid(
                    f"Variables '{name}' and '{other_name}' occupy overlapping payload bytes"
                )
        reserved.append((name, offset, size))


def validate_ip_address(value):
    try:
        return str(ipaddress.IPv4Address(cv.string(value)))
    except ValueError as err:  # ipaddress.AddressValueError dziedziczy po ValueError
        raise cv.Invalid("A valid IPv4 address is required") from err



def normalize_type(type_value, length=None):
    """Nazwa typu + opcjonalna length (STRING) -> nazwa wewnętrzna, np. STRING(20).

    Długość można podać jak w integracji HA (type: STRING, length: 20) albo w nazwie typu (STRING(20)).
    """
    raw = cv.string_strict(type_value)
    normalized = validate_variable_type(raw)
    if length is None:
        return normalized
    if not is_string_type(normalized):
        raise cv.Invalid("'length' applies only to the STRING type")
    length = cv.int_range(min=1, max=255)(length)
    if "(" in raw and int(normalized[7:-1]) != length:
        raise cv.Invalid("'length' contradicts the length given in the type")
    return f"STRING({length})"


def _pop(config, key, validator=None):
    if key not in config:
        return None
    value = config.pop(key)
    return validator(value) if validator is not None else value


def _check_kind_type(kind, type_name, name):
    if type_name == "BOOL":
        if kind not in ("binary_sensor", "switch"):
            raise cv.Invalid(f"BOOL variable '{name}' must use entity binary_sensor or switch")
    elif kind in ("binary_sensor", "switch"):
        raise cv.Invalid(f"Only BOOL variables can use entity {kind} ('{name}')")


def migrate_variable_keys(config):
    """Aliasy nazw zmiennej: byte_offset = offset, min_value/max_value = min/max (nazwy ESPHome)."""
    config = dict(config)
    for alias, key in ((CONF_BYTE_OFFSET, CONF_OFFSET), ("min_value", CONF_MIN), ("max_value", CONF_MAX)):
        if alias in config:
            if key in config:
                raise cv.Invalid(f"Use only '{key}' ('{alias}' is its alias)")
            config[key] = config.pop(alias)
    return config


def validate_source(value, type_name, name):
    """Źródło wartości zmiennej listy write: id encji ESPHome albo {rodzaj: id}.

    Zwykłe id oznacza sensor (zmienne liczbowe), binary_sensor (BOOL) albo text_sensor (STRING). Inny rodzaj
    encji podaje się jawnie, np. `source: {number: moj_number}` albo `source: {switch: moj_przelacznik}`.
    Rodzaj źródła musi pasować do typu zmiennej: liczby <- sensor, number; BOOL <- binary_sensor, switch;
    STRING <- text_sensor.
    """
    if type_name == "BOOL":
        allowed, default = SOURCE_KINDS_BOOL, "binary_sensor"
    elif is_string_type(type_name):
        allowed, default = SOURCE_KINDS_STRING, "text_sensor"
    else:
        allowed, default = SOURCE_KINDS_NUMERIC, "sensor"

    if isinstance(value, dict):
        if len(value) != 1:
            raise cv.Invalid(f"'source' of '{name}' must contain exactly one of: {', '.join(SOURCE_CLASSES)}")
        (kind, reference), = value.items()
        kind = cv.one_of(*SOURCE_CLASSES, lower=True)(kind)
    else:
        kind, reference = default, value

    if kind not in allowed:
        raise cv.Invalid(
            f"'source' of '{name}' (type {type_name}) must be one of: {', '.join(allowed)}, not {kind}"
        )
    return {"kind": kind, "id": cv.use_id(SOURCE_CLASSES[kind])(reference)}


def validate_variable(config):
    """Walidacja zmiennej z encją (konfiguracja jak w integracji HA).

    Parametry wspólne (name, unique_id, offset, index, type, length, entity, format, initial_value,
    restore_value, require_value, min, max, step, zero_as_unavailable, suggested_display_precision,
    enabled_by_default, inverted) są obsługiwane tutaj. Pozostałe parametry trafiają do pełnego schematu
    encji ESPHome, dlatego dostępne są wszystkie jej opcje.
    """
    config = migrate_variable_keys(config)
    for key in (CONF_NAME, CONF_TYPE, CONF_ENTITY):
        if key not in config:
            raise cv.Invalid(f"'{key}' is required for every variable")

    name = cv.string_strict(config.pop(CONF_NAME))
    if not name.strip():
        raise cv.Invalid("Variable name cannot be empty")
    entity = cv.one_of(*ENTITY_KINDS, lower=True)(config.pop(CONF_ENTITY))
    type_name = normalize_type(config.pop(CONF_TYPE), _pop(config, CONF_LENGTH))
    _check_kind_type(entity, type_name, name)

    unique_id = _pop(config, CONF_UNIQUE_ID, cv.string_strict)
    unique_id = slugify(name) if unique_id is None else unique_id.strip()
    if not unique_id:
        raise cv.Invalid(f"Cannot derive unique_id from name '{name}'; set unique_id")

    result = {
        CONF_VAR_NAME: name,
        CONF_UNIQUE_ID: unique_id,
        CONF_VAR_TYPE: type_name,
        CONF_ENTITY: entity,
        CONF_REQUIRE_VALUE: _pop(config, CONF_REQUIRE_VALUE, cv.boolean) or False,
        CONF_RESTORE_VALUE: _pop(config, CONF_RESTORE_VALUE, cv.boolean) or False,
        CONF_ZERO_AS_UNAVAILABLE: _pop(config, CONF_ZERO_AS_UNAVAILABLE, cv.boolean) or False,
    }
    offset = _pop(config, CONF_OFFSET, cv.int_range(min=0, max=MAX_PAYLOAD_SIZE - 1))
    if offset is not None:
        result[CONF_BYTE_OFFSET] = offset
    index = _pop(config, CONF_INDEX, cv.uint16_t)
    if index is not None:
        result[CONF_INDEX] = index

    # Rodzaj encji ESPHome: STRING i typy czasowe z format: text pokazuje text_sensor.
    fmt = _pop(config, CONF_FORMAT, cv.one_of("raw", "text", lower=True))
    kind = entity
    if entity == "sensor":
        if fmt is not None and type_name not in TEMPORAL_TYPES:
            raise cv.Invalid(f"'format' applies only to sensors of temporal types ('{name}')")
        if is_string_type(type_name) or (type_name in TEMPORAL_TYPES and fmt != "raw"):
            kind = "text_sensor"
    elif fmt is not None:
        raise cv.Invalid(f"'format' applies only to sensors of temporal types ('{name}')")
    result[CONF_KIND] = kind

    # Parametry encji o innych nazwach w ESPHome
    remainder = config
    precision = _pop(remainder, CONF_SUGGESTED_DISPLAY_PRECISION, cv.int_range(min=0, max=15))
    if precision is not None:
        if kind != "sensor":
            raise cv.Invalid(f"'suggested_display_precision' applies only to numeric sensors ('{name}')")
        if "accuracy_decimals" in remainder:
            raise cv.Invalid("Use only 'suggested_display_precision' ('accuracy_decimals' is its ESPHome name)")
        remainder["accuracy_decimals"] = precision
    enabled = _pop(remainder, CONF_ENABLED_BY_DEFAULT, cv.boolean)
    if enabled is not None:
        if "disabled_by_default" in remainder:
            raise cv.Invalid("Use only 'enabled_by_default' ('disabled_by_default' is its ESPHome name)")
        remainder["disabled_by_default"] = not enabled
    inverted = _pop(remainder, CONF_INVERTED, cv.boolean) if kind != "switch" else None
    if inverted is not None:
        if kind != "binary_sensor":
            raise cv.Invalid(f"'inverted' applies only to binary_sensor and switch ('{name}')")
        if inverted:
            filters = remainder.get("filters", [])
            filters = list(filters) if isinstance(filters, list) else [filters]
            remainder["filters"] = [{"invert": None}] + filters
    if result[CONF_ZERO_AS_UNAVAILABLE] and kind != "sensor":
        raise cv.Invalid(f"'zero_as_unavailable' applies only to numeric sensors ('{name}')")

    # initial_value / restore_value / min / max / step: tylko encje zapisu
    # initial_value: encje zapisu (switch, number) oraz sensor i binary_sensor listy write; restore_value tylko
    # switch i number. Zgodność z trybem listy (read/write) sprawdza validate_config.
    initial = _pop(remainder, CONF_INITIAL_VALUE)
    if initial is not None:
        initial = cv.boolean(initial) if kind in ("switch", "binary_sensor") else cv.float_(initial)
        result[CONF_INITIAL_VALUE] = initial
    if result[CONF_RESTORE_VALUE] and kind not in ("switch", "number"):
        raise cv.Invalid(f"restore_value applies only to switch and number ('{name}')")

    # source: encja ESPHome (sensor albo binary_sensor), której stan jest wysyłany do PLC (tylko lista write)
    source = _pop(remainder, CONF_SOURCE)
    if source is not None:
        if entity not in ("sensor", "binary_sensor"):
            raise cv.Invalid(f"'source' applies only to sensor and binary_sensor variables ('{name}')")
        result[CONF_SOURCE] = validate_source(source, type_name, name)

    if kind == "number":
        low, high = TYPE_RANGES.get(type_name, (None, None))
        minimum = _pop(remainder, CONF_MIN, cv.float_)
        maximum = _pop(remainder, CONF_MAX, cv.float_)
        minimum = low if minimum is None else minimum
        maximum = high if maximum is None else maximum
        if minimum is None or maximum is None:
            raise cv.Invalid(f"'min' and 'max' are required for number variables of type {type_name} ('{name}')")
        if low is not None and not (low <= minimum <= maximum <= high):
            raise cv.Invalid(f"min/max of '{name}' must lie within the {type_name} range {low}..{high}")
        if minimum >= maximum:
            raise cv.Invalid(f"min must be lower than max ('{name}')")
        step = _pop(remainder, CONF_STEP, cv.positive_float)
        result[CONF_MIN], result[CONF_MAX] = minimum, maximum
        result[CONF_STEP] = 1.0 if step is None else step
        if CONF_INITIAL_VALUE in result and not minimum <= result[CONF_INITIAL_VALUE] <= maximum:
            raise cv.Invalid(f"initial_value must be within min..max ('{name}')")
    else:
        for key in (CONF_MIN, CONF_MAX, CONF_STEP):
            if key in remainder:
                raise cv.Invalid(f"'{key}' applies only to number variables ('{name}')")

    remainder[CONF_NAME] = name
    result[CONF_ENTITY_CONFIG] = PLATFORM_SCHEMAS[kind](remainder)
    return result


VARIABLE_SCHEMA = validate_variable


def validate_ip_address(value):
    try:
        return str(ipaddress.IPv4Address(cv.string(value)))
    except ValueError as err:  # ipaddress.AddressValueError dziedziczy po ValueError
        raise cv.Invalid("A valid IPv4 address is required") from err


def unique_stat_name(base, ip_address):
    """Nazwa sensora statystyk; przy powtórzonym list_id dodawany jest adres IP, aby nazwy były unikalne."""
    names = CORE.data.setdefault(DOMAIN, {}).setdefault("names", set())
    name = base
    suffix = ip_address.replace(".", "_")
    counter = 2
    while name in names:
        name = f"{base}_{suffix}" if counter == 2 else f"{base}_{suffix}_{counter}"
        counter += 1
    names.add(name)
    return name


def validate_config(config):
    mode = config[CONF_MODE]
    ip_address = config[CONF_IP_ADDRESS]
    variables = config[CONF_VARIABLES]

    if not variables:
        raise cv.Invalid("At least one variable must be defined")

    if mode == "write" and ip_address == "0.0.0.0":
        raise cv.Invalid("ip_address 0.0.0.0 is not allowed for mode 'write'")
    if mode == "read" and ip_address == "255.255.255.255":
        raise cv.Invalid(
            "ip_address 255.255.255.255 cannot be a sender address; use 0.0.0.0 to accept any sender"
        )

    packed = config[CONF_PACK_VARIABLES]
    allowed = READ_ENTITIES if mode == "read" else WRITE_ENTITIES

    names = {}
    unique_ids = {}
    for variable in variables:
        name = variable[CONF_VAR_NAME]
        unique_id = variable[CONF_UNIQUE_ID]
        if name in names:
            raise cv.Invalid(f"Duplicate variable name: {name}")
        if unique_id in unique_ids:
            raise cv.Invalid(
                f"Duplicate unique_id '{unique_id}' (variables '{unique_ids[unique_id]}' and '{name}'); set a unique one"
            )
        names[name] = True
        unique_ids[unique_id] = name

        if variable[CONF_ENTITY] not in allowed:
            raise cv.Invalid(
                f"A {mode} list can contain only entities: {', '.join(allowed)} (variable '{name}')"
            )
        if is_string_type(variable[CONF_VAR_TYPE]):
            if mode == "read" and variable[CONF_ENTITY] != "sensor":
                raise cv.Invalid(f"STRING variable '{name}' in a read list must use entity sensor")
            if mode == "write" and variable[CONF_ENTITY] not in ("sensor", "number"):
                raise cv.Invalid(f"STRING variable '{name}' in a write list must use entity sensor or number")
        if mode == "read":
            # Encje listy read (także switch i number) tylko pokazują wartość z PLC.
            for key in (CONF_SOURCE, CONF_INITIAL_VALUE):
                if key in variable:
                    raise cv.Invalid(f"'{key}' applies only to variables of write lists ('{name}')")
            for key in (CONF_REQUIRE_VALUE, CONF_RESTORE_VALUE):
                if variable[key]:
                    raise cv.Invalid(f"{key} applies only to variables of write lists ('{name}')")
        if variable[CONF_REQUIRE_VALUE] and packed:
            raise cv.Invalid(
                f"require_value ('{name}') applies only to unpacked lists; "
                "in a packed list use require_values of the list"
            )

    # require_values dotyczy listy write pakowanej; w liście niepakowanej służy require_value.
    if mode == "write" and not packed and config[CONF_REQUIRE_VALUES]:
        raise cv.Invalid(
            "require_values applies only to packed lists; in an unpacked list use require_value of the variables"
        )
    if mode != "write":
        config[CONF_REQUIRE_VALUES] = False

    assign_offsets_and_indices(variables, packed)
    validate_layout(variables)

    needed = max(v[CONF_BYTE_OFFSET] + get_type_size(v[CONF_VAR_TYPE]) for v in variables)
    if packed:
        payload_size = config.get(CONF_PAYLOAD_SIZE, 0)
        if payload_size and payload_size < needed:
            raise cv.Invalid(
                f"payload_size ({payload_size}) is lower than the size resulting from variable offsets ({needed})"
            )
        config[CONF_PAYLOAD_SIZE] = payload_size
    else:
        config[CONF_PAYLOAD_SIZE] = 0

    # var_count to liczba zmiennych na liście nadawcy/odbiorcy (lista pakowana). Może być większa niż
    # liczba zmiennych zdefiniowanych tutaj (pominięte zmienne), ale nie mniejsza.
    defined = len(variables)
    if not packed or config[CONF_VAR_COUNT] == 0:
        config[CONF_VAR_COUNT] = defined
    elif config[CONF_VAR_COUNT] < defined:
        raise cv.Invalid(
            f"var_count ({config[CONF_VAR_COUNT]}) cannot be lower than the number of defined variables ({defined})"
        )

    # update_interval steruje wyłącznie cykliczną wysyłką, więc w trybie "read" jest zbędny.
    # Wartość 0s oznacza "brak wysyłki cyklicznej"; ESPHome wywoływałby update() w każdej
    # iteracji pętli, dlatego 0s jest zamieniane na "never".
    interval = config[CONF_UPDATE_INTERVAL]
    if mode == "read" or getattr(interval, "total_milliseconds", interval) == 0:
        config[CONF_UPDATE_INTERVAL] = SCHEDULER_DONT_RUN

    # Sensory statystyk: lista read ma rx_packets, rx_errors i rejected_packets, lista write ma tx_packets
    # i tx_errors (nazwa z list_id, np. rx_packets_123). Sumy wszystkich list (total_*) tworzy pierwsza lista.
    stats = STATS_READ if mode == "read" else STATS_WRITE
    config[CONF_STATS] = {
        key: STATS_SENSOR_SCHEMA({CONF_NAME: unique_stat_name(f"{key}_{config[CONF_LIST_ID]}", ip_address)})
        for key in stats
    }
    data = CORE.data.setdefault(DOMAIN, {})
    if not data.get("totals_assigned"):
        data["totals_assigned"] = True
        names_used = data.setdefault("names", set())
        config[CONF_TOTALS] = {}
        for key in STATS_TOTAL:
            names_used.add(key)
            config[CONF_TOTALS][key] = STATS_SENSOR_SCHEMA({CONF_NAME: key})
    else:
        config[CONF_TOTALS] = {}

    return config


def final_validate(config):
    # Listy "read" mogą współdzielić port UDP (jedno gniazdo, ramki rozdzielane według list_ID i IP nadawcy).
    # Niedozwolone są tylko listy, których nie da się odróżnić: ten sam port, ten sam list_ID oraz
    # ten sam adres nadawcy albo któryś z nich równy 0.0.0.0 (akceptuje dowolnego nadawcę).
    if config[CONF_MODE] != "read":
        return config
    for other in fv.full_config.get().get(DOMAIN, []):
        if other == config or other[CONF_MODE] != "read":
            continue
        if other[CONF_PORT] != config[CONF_PORT] or other[CONF_LIST_ID] != config[CONF_LIST_ID]:
            continue
        ips = (config[CONF_IP_ADDRESS], other[CONF_IP_ADDRESS])
        if ips[0] == ips[1] or "0.0.0.0" in ips:
            raise cv.Invalid(
                f"Two read lists share UDP port {config[CONF_PORT]} and list_id {config[CONF_LIST_ID]} "
                f"and cannot be told apart; use a different list_id or a different (non-0.0.0.0) ip_address"
            )
    return config


CONFIG_SCHEMA = cv.All(
    migrate_legacy_keys,
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WagoNetVarComponent),
            cv.Required(CONF_IP_ADDRESS): validate_ip_address,
            cv.Optional(CONF_PORT, default=1202): cv.port,
            cv.Required(CONF_LIST_ID): cv.uint16_t,
            cv.Optional(CONF_MODE, default="read"): cv.one_of("read", "write", lower=True),
            cv.Optional(CONF_PACK_VARIABLES, default=True): cv.boolean,
            cv.Optional(CONF_VAR_COUNT, default=0): cv.uint16_t,
            cv.Optional(CONF_PAYLOAD_SIZE): cv.int_range(min=1, max=MAX_PAYLOAD_SIZE),
            cv.Optional(CONF_ON_CHANGE, default=False): cv.boolean,
            cv.Optional(CONF_MIN_INTERVAL, default="500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TIMEOUT, default="0s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_REQUIRE_VALUES, default=False): cv.boolean,
            cv.Required(CONF_VARIABLES): cv.ensure_list(VARIABLE_SCHEMA),
        }
    ).extend(cv.polling_component_schema("5s")),
    validate_config,
)

FINAL_VALIDATE_SCHEMA = final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    # register_component() ustawia także update_interval komponentu PollingComponent.
    await cg.register_component(var, config)

    cg.add(var.set_ip_address(config[CONF_IP_ADDRESS]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_list_id(config[CONF_LIST_ID]))
    cg.add(var.set_var_count(config[CONF_VAR_COUNT]))
    cg.add(var.set_direction(config[CONF_MODE]))
    cg.add(var.set_send_on_change(config[CONF_ON_CHANGE]))
    cg.add(var.set_min_interval(config[CONF_MIN_INTERVAL]))
    cg.add(var.set_pack_variables(config[CONF_PACK_VARIABLES]))
    cg.add(var.set_payload_size(config[CONF_PAYLOAD_SIZE]))
    cg.add(var.set_timeout(config[CONF_TIMEOUT]))
    cg.add(var.set_require_values(config[CONF_REQUIRE_VALUES]))

    # W C++ zmienna jest identyfikowana przez unique_id (stabilny, niezależny od nazwy encji).
    for variable in config[CONF_VARIABLES]:
        cg.add(
            var.add_variable(
                variable[CONF_UNIQUE_ID],
                variable[CONF_VAR_TYPE],
                variable[CONF_BYTE_OFFSET],
                variable.get(CONF_INDEX, 0),
                variable[CONF_REQUIRE_VALUE],
            )
        )

    for variable in config[CONF_VARIABLES]:
        unique_id = variable[CONF_UNIQUE_ID]
        entity_config = variable[CONF_ENTITY_CONFIG]
        kind = variable[CONF_KIND]
        initial = variable.get(CONF_INITIAL_VALUE)

        if kind == "sensor":
            entity = await sensor.new_sensor(entity_config)
            cg.add(var.register_sensor(unique_id, entity, variable[CONF_ZERO_AS_UNAVAILABLE]))
        elif kind == "text_sensor":
            entity = await text_sensor.new_text_sensor(entity_config)
            cg.add(var.register_text_sensor(unique_id, entity))
        elif kind == "binary_sensor":
            entity = await binary_sensor.new_binary_sensor(entity_config)
            cg.add(var.register_binary_sensor(unique_id, entity))
        elif kind == "switch":
            entity = await switch.new_switch(entity_config)
            cg.add(entity.set_parent(var, unique_id))
            cg.add(
                var.register_switch(
                    unique_id,
                    entity,
                    variable[CONF_RESTORE_VALUE],
                    initial is not None,
                    bool(initial),
                )
            )
        else:  # number
            entity = await number.new_number(
                entity_config,
                min_value=variable[CONF_MIN],
                max_value=variable[CONF_MAX],
                step=variable[CONF_STEP],
            )
            cg.add(entity.set_parent(var, unique_id))
            cg.add(
                var.register_number(
                    unique_id,
                    entity,
                    variable[CONF_RESTORE_VALUE],
                    initial is not None,
                    float(initial or 0.0),
                )
            )

    # sensor i binary_sensor listy write: wartość początkowa i źródło (encja ESPHome)
    for variable in config[CONF_VARIABLES]:
        unique_id = variable[CONF_UNIQUE_ID]
        if variable[CONF_KIND] in ("sensor", "text_sensor", "binary_sensor") and CONF_INITIAL_VALUE in variable:
            cg.add(var.set_initial_value(unique_id, float(variable[CONF_INITIAL_VALUE])))
        if CONF_SOURCE in variable:
            source = await cg.get_variable(variable[CONF_SOURCE]["id"])
            cg.add(getattr(var, f"set_source_{variable[CONF_SOURCE]['kind']}")(unique_id, source))

    stats = STATS_READ if config[CONF_MODE] == "read" else STATS_WRITE
    for key, setter in stats.items():
        sens = await sensor.new_sensor(config[CONF_STATS][key])
        cg.add(getattr(var, setter)(sens))

    for key, sensor_config in config[CONF_TOTALS].items():
        sens = await sensor.new_sensor(sensor_config)
        cg.add(getattr(var, STATS_TOTAL[key])(sens))


# Akcja "wago_netvar.send": natychmiastowa wysyłka listy write (niezależnie od on_change i update_interval).
# Zrealizowana jako LambdaAction (bez własnej klasy Action), więc nie zależy od wersji API automatyzacji ESPHome.
SEND_ACTION_SCHEMA = automation.maybe_simple_id({cv.Required(CONF_ID): cv.use_id(WagoNetVarComponent)})


@automation.register_action("wago_netvar.send", automation.LambdaAction, SEND_ACTION_SCHEMA)
async def send_action_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    lambda_ = await cg.process_lambda(Lambda(f"{parent}->send_now();"), args, return_type=cg.void)
    return cg.new_Pvariable(action_id, template_arg, lambda_)
