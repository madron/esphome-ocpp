# ESPHome OCPP Component

Component for controlling OCPP EV chargers from a local node.

The initial target protocol is **OCPP 1.6J**.

## Core Concepts

The configuration is organized around three electrical and OCPP concepts: the
site, chargers, and connectors. Each level has a different responsibility.

- The `site` describes the shared electrical installation. It defines the number
  of available phases, the phase-to-neutral voltage in `V`.
- A `charge_point` describes one OCPP charge point that connects to this component.
  It is identified by its `charge_point_id`, which must match the identity used
  by the charger in the WebSocket URL. Charger-level configuration is about
  admission, the number of phases, charger-to-site phase mapping, and grouping of
  the physical connectors belonging to that charge point.
- A `connector` describes one OCPP connector on a charger. It defines the OCPP
  connector ID, the connector's physical maximum current in `A`, and optionally
  its sensors, current-limit control, and restart/enable controls.

In short, the site owns shared electrical constraints, chargers represent OCPP
devices, and connectors represent the individually controlled charging outlets.

## Example Configuration

```yaml
ocpp:
  id: ocpp_id

  server:
    port: 9000

  charge_points:
    - id: garage_left
      charge_point_id: A99999
      debug_ocpp_messages: true
      startup_notifications_delay: 300
      charger_info:
        name: Garage Charger Info
      connectors:
        - connector_id: 1
          current:
            name: Garage Current
          power:
            name: Garage Power
          energy:
            name: Garage Energy
          voltage:
            name: Garage Voltage
          status:
            name: Garage Status
          error:
            name: Garage Error
```

`debug_ocpp_messages` is optional per `charge_point`. When enabled, raw OCPP RX/TX payloads for that charger are logged at the ESPHome debug log level.

Connector `current`, `power`, `energy`, and `voltage` sensors are populated from OCPP `MeterValues` messages whose `connectorId` matches the connector's `connector_id`. The component asks the charger to report `Current.Import`, `Power.Active.Import`, `Energy.Active.Import.Register`, and `Voltage`. If the charger omits one of those values, the corresponding sensor is published as unavailable/unknown instead of `0` so unsupported values are not confused with real zero measurements. Energy is exposed in `kWh`.

Connector `status` and `error` text sensors are populated from `StatusNotification` messages whose `connectorId` matches the connector's `connector_id`. `errorCode: NoError` is exposed as an empty string.

### Charge point options

| Option                                   | Description |
| ---                                      | --- |
| `id` (Required)                          | ESPHome ID for this charge point. |
| `charge_point_id` (Optional)             | OCPP/WebSocket identity expected from the charger. When omitted, the first free dynamic charge point slot is used. |
| `connectors` (Optional)                  | List of OCPP connectors for this charge point. Defaults to one connector with `connector_id: 1`. Connector IDs must be unique within the charge point. |
| `debug_ocpp_messages` (Optional)         | Logs raw OCPP RX/TX payloads at debug level. Defaults to `false`. |
| `startup_notifications_delay` (Optional) | Delay in seconds before sending `TriggerMessage` requests for missing startup notifications. `BootNotification` and `StatusNotification` are tracked independently; if both are missing, `BootNotification` is requested first and `StatusNotification` after its reply. Defaults to `300`. Set to `0` to disable. |
| `charger_info` (Optional)                | Text sensor that reports charger vendor, model, and firmware from `BootNotification`, and clears after disconnect. |
| `online` (Optional)                      | Binary sensor that is `on` after `BootNotification`, `Heartbeat`, or `StatusNotification`, and `off` after disconnect. |
| `protocol` (Optional)                    | Text sensor that reports the negotiated OCPP WebSocket protocol, and clears after disconnect. |

### Connector options

| Option                            | Description |
| ---                               | --- |
| `id` (Optional)                   | ESPHome internal ID for this connector. Usually omit this and let ESPHome generate it. |
| `connector_id` (Optional)         | Numeric OCPP connector ID used in `MeterValues.connectorId`. Defaults to `1`. Must be unique within the charge point. |
| `current` (Optional)              | Sensor populated from `Current.Import` `MeterValues` in `A`. Missing values are published as unavailable/unknown. |
| `power` (Optional)                | Sensor populated from `Power.Active.Import` `MeterValues` in `W`. Missing values are published as unavailable/unknown. |
| `energy` (Optional)               | Sensor populated from `Energy.Active.Import.Register` `MeterValues` in `kWh`. OCPP `Wh` values are converted to `kWh`. Missing values are published as unavailable/unknown. |
| `voltage` (Optional)              | Sensor populated from `Voltage` `MeterValues` in `V`. Missing values are published as unavailable/unknown. |
| `status` (Optional)               | Text sensor populated from `StatusNotification.status` for OCPP 1.6 or `StatusNotification.connectorStatus` for OCPP 2.0.1. Clears after disconnect. |
| `error` (Optional)                | Text sensor populated from `StatusNotification.errorCode` when the charger provides it. `NoError` is published as an empty string. Clears after disconnect. |

### Charger configuration

With the default server path `/`, configure the charger OCPP/WebSocket server URL as:
```text
ws://<esp-ip>:9000
```

### Units of Measure

- Power: Watts (`W`)
- Current: Amperes (`A`)
- Voltage: Volts (`V`)
- Energy: kilowatt-hours (`kWh`)
