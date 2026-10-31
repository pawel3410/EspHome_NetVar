
# ESPHome WAGO NetVar Component

Custom ESPHome component for communication with WAGO PLC controllers using the NetVar over UDP protocol.

The component supports ESP32 and ESP8266 platforms using the Arduino framework.

## Features

- NetVar communication over UDP.
- Separate read and write modes.
- IEC 61131-3 variable types.
- Little-endian and big-endian byte order.
- Optional BOOL bit packing.
- Optional memory alignment.
- Numeric sensors.
- Binary sensors.
- Text sensors for STRING variables.
- Switches and number entities for writing values.
- Periodic transmission.
- Optional transmission on value change.
- UDP packet validation.
- Source IP and port validation.
- Packet size limited to 1500 bytes.
- Internal RX/TX diagnostics.

## Communication modes

The component supports two mutually exclusive modes.

### Read mode

```yaml
direction: read
```

The component receives UDP packets from the configured PLC.

Supported entities:

- sensor
- binary_sensor
- text_sensor

The component does not send NetVar packets in this mode.

### Write mode

```yaml
direction: write
```

The component sends UDP packets to the configured PLC.

Supported entities:

- switch
- number

The component does not process incoming NetVar packets in this mode.

The `both` mode is not supported.

## Installation

Copy the component directory into your ESPHome configuration:

```text
components/
  wago_netvar/
    __init__.py
    wago_netvar.cpp
    wago_netvar.h
```

Alternatively, configure it as an external component in ESPHome.

Example:

```yaml
external_components:
  - source:
      type: local
      path: components
    components:
      - wago_netvar
```

## Configuration

| Parameter | Type | Default | Description |
|---|---|---|---|
| id | ID | Required | Component identifier |
| ip_address | IPv4 | Required | PLC IP address |
| port | uint16 | 1202 | UDP destination and listening port |
| list_ID | uint16 | 1 | NetVar list identifier |
| var_count | uint16 | 0 | Variable count; 0 means automatic |
| direction | string | write | read or write |
| endian | string | little | little or big |
| pack_bools | bool | false | Pack BOOL values into bits |
| alignment | bool | false | Align variables in memory |
| update_interval | time | 10s | Periodic transmission interval |
| send_on_change | bool | false | Send when a value changes |
| min_interval | time | 100ms | Minimum interval between transmissions |
| variables | list | Required | Variable definitions |
| sensors | list | Optional | Numeric read entities |
| binary_sensors | list | Optional | BOOL read entities |
| text_sensors | list | Optional | STRING read entities |
| switches | list | Optional | BOOL write entities |
| numbers | list | Optional | Numeric write entities |

## Supported IEC types

| Type | Size | Description |
|---|---:|---|
| BOOL | 1 byte | Boolean |
| BYTE | 1 byte | Unsigned 8-bit |
| USINT | 1 byte | Unsigned 8-bit |
| SINT | 1 byte | Signed 8-bit |
| WORD | 2 bytes | Unsigned 16-bit |
| UINT | 2 bytes | Unsigned 16-bit |
| INT | 2 bytes | Signed 16-bit |
| DWORD | 4 bytes | Unsigned 32-bit |
| UDINT | 4 bytes | Unsigned 32-bit |
| DINT | 4 bytes | Signed 32-bit |
| TIME | 4 bytes | Unsigned 32-bit time value |
| REAL | 4 bytes | IEEE-754 float |
| LREAL | 8 bytes | IEEE-754 double |
| STRING(n) | n+1 bytes | Null-terminated string |

STRING without an explicit length defaults to STRING(80).

Supported STRING lengths are 1 to 255 characters.

## Packet format

Every UDP packet contains a 20-byte header followed by a payload.

| Offset | Size | Field |
|---|---:|---|
| 0 | 4 | Magic bytes: 00 2D 53 33 |
| 4 | 4 | Reserved |
| 8 | 2 | list_ID |
| 10 | 2 | Reserved |
| 12 | 2 | var_count |
| 14 | 2 | Total packet length |
| 16 | 2 | Sequence counter |
| 18 | 2 | Reserved |
| 20 | Variable | Payload |

The byte order of 16-bit header fields follows the configured endian setting.

The sequence counter is incremented for each successfully transmitted packet.

The receiver does not currently use the sequence counter to detect missing or duplicated packets.

## Packet size

The maximum supported UDP packet size is 1500 bytes.

The header occupies 20 bytes.

Therefore, the maximum payload size is:

```text
1500 - 20 = 1480 bytes
```

The configuration validator checks the calculated payload size.

The receiver rejects packets larger than 1500 bytes.

The receiver also checks:

- Magic bytes.
- Source IP address.
- Source UDP port.
- list_ID.
- var_count.
- Declared total packet length.
- Actual payload length.

## Variable layout

Variables are arranged in the order defined in the YAML configuration.

When alignment is enabled, variables are aligned to their configured type alignment.

When BOOL packing is enabled, consecutive BOOL variables occupy individual bits of the same byte.

The same layout settings must be used by the PLC and ESPHome.

## Example: Reading data from PLC

```yaml
esphome:
  name: wago-reader

esp32:
  board: esp32dev
  framework:
    type: arduino

wifi:
  ssid: "YOUR_WIFI"
  password: "YOUR_PASSWORD"

logger:

external_components:
  - source:
      type: local
      path: components
    components:
      - wago_netvar

wago_netvar:
  id: netvar
  ip_address: "192.168.1.10"
  port: 1202
  list_ID: 1
  direction: read
  endian: little
  pack_bools: false
  alignment: false

  variables:
    - name: "Actual_Temp"
      type: REAL

    - name: "Pump_Status"
      type: BOOL

    - name: "Device_Name"
      type: STRING(32)

    - name: "Energy_Total"
      type: UDINT

  sensors:
    - variable: "Actual_Temp"
      name: "Actual Temperature"
      unit_of_measurement: "C"
      accuracy_decimals: 2

    - variable: "Energy_Total"
      name: "Energy Total"
      accuracy_decimals: 0

  binary_sensors:
    - variable: "Pump_Status"
      name: "Pump Status"

  text_sensors:
    - variable: "Device_Name"
      name: "Device Name"
```

## Example: Writing data to PLC

```yaml
esphome:
  name: wago-writer

esp32:
  board: esp32dev
  framework:
    type: arduino

wifi:
  ssid: "YOUR_WIFI"
  password: "YOUR_PASSWORD"

logger:

external_components:
  - source:
      type: local
      path: components
    components:
      - wago_netvar

wago_netvar:
  id: netvar
  ip_address: "192.168.1.10"
  port: 1202
  list_ID: 2
  direction: write
  endian: little
  pack_bools: false
  alignment: false

  update_interval: 10s
  send_on_change: true
  min_interval: 200ms

  variables:
    - name: "Pump_Command"
      type: BOOL

    - name: "Temperature_Setpoint"
      type: REAL

    - name: "Operating_Mode"
      type: INT

  switches:
    - variable: "Pump_Command"
      name: "Pump Command"

  numbers:
    - variable: "Temperature_Setpoint"
      name: "Temperature Setpoint"
      min_value: 0
      max_value: 100
      step: 0.5

    - variable: "Operating_Mode"
      name: "Operating Mode"
      min_value: 0
      max_value: 10
      step: 1
```

## var_count

The `var_count` parameter is optional.

When set to zero, the component automatically calculates the number of configured variables.

When explicitly configured, the value must match the number of variables in the YAML definition.

## Diagnostics

The component maintains internal counters for:

- Received packets.
- Transmitted packets.
- Receive errors.
- Transmit errors.
- Rejected packets.

Debug logging can be enabled using the ESPHome logger configuration.

Example:

```yaml
logger:
  level: DEBUG
```

## Important limitations

- Only one communication direction is supported per component instance.
- The configured PLC IP and UDP port are used to validate incoming packet sources.
- The PLC must use the configured source UDP port in read mode.
- STRING uses a null-terminated representation.
- Numeric ESPHome sensors use float values. Large integer values can therefore lose precision when published.
- ESPHome number entities use float values and cannot represent all 64-bit integer values exactly.
- LREAL is decoded internally as double, but ESPHome sensor entities publish float.
- The sequence counter is transmitted but not used for packet ordering validation.
- The maximum complete UDP packet size is 1500 bytes.

## Compatibility

The component is intended for WAGO PLC systems using NetVar over UDP.

The PLC and ESPHome configurations must use matching:

- NetVar list identifiers.
- Variable ordering.
- Variable types.
- Endian settings.
- BOOL packing settings.
- Memory alignment settings.

Always test communication against the actual PLC configuration before using the component to control machinery.
