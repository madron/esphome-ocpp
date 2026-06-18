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
```

`debug_ocpp_messages` is optional per `charge_point`. When enabled, raw OCPP RX/TX payloads for that charger are logged at the ESPHome debug log level.

### Charge point options

| Option                                   | Description |
| ---                                      | --- |
| `id` (Required)                          | ESPHome ID for this charge point. |
| `charge_point_id` (Optional)             | OCPP/WebSocket identity expected from the charger. When omitted, the first free dynamic charge point slot is used. |
| `debug_ocpp_messages` (Optional)         | Logs raw OCPP RX/TX payloads at debug level. Defaults to `false`. |
| `startup_notifications_delay` (Optional) | Delay in seconds before sending `TriggerMessage` requests for missing startup notifications. `BootNotification` and `StatusNotification` are tracked independently; if both are missing, `BootNotification` is requested first and `StatusNotification` after its reply. Defaults to `300`. Set to `0` to disable. |
| `charger_info` (Optional)                | Text sensor that reports charger vendor, model, and firmware from `BootNotification`, and clears after disconnect. |
| `online` (Optional)                      | Binary sensor that is `on` after `BootNotification`, `Heartbeat`, or `StatusNotification`, and `off` after disconnect. |
| `protocol` (Optional)                    | Text sensor that reports the negotiated OCPP WebSocket protocol, and clears after disconnect. |

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
