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
      phases: 3
      max_current: 32
      debug_ocpp_messages: true
      debug_ocpp_exclude_actions:
        - MeterValues
      startup_notifications_delay: 300
      charger_info:
        name: Garage Charger Info
      connectors:
        - connector_id: 1
          phase_mapping: [l1, l2, l3]
          current:
            name: Garage Current
          log_meter_values: true
          current_limit:
            name: Garage Current Limit
            max_value: 16
          needed_current_l1:
            name: Garage Needed Current L1
          needed_current_l2:
            name: Garage Needed Current L2
          needed_current_l3:
            name: Garage Needed Current L3
          control_current:
            name: Garage Allocated Current
          power:
            name: Garage Power
          total_energy:
            name: Garage Total Energy
          session_energy:
            name: Garage Session Energy
          session_time:
            name: Garage Session Time
          voltage:
            name: Garage Voltage
          status:
            name: Garage Status
          error:
            name: Garage Error
```

`debug_ocpp_messages` is optional per `charge_point`. When enabled, raw OCPP RX/TX payloads for that charger are logged at the ESPHome debug log level. Use `debug_ocpp_exclude_actions` to keep debug logging enabled while suppressing noisy action payloads, such as `MeterValues`, and their known related responses.

Connector `current`, `power`, `total_energy`, and `voltage` sensors are populated from OCPP `MeterValues` messages whose `connectorId` for OCPP 1.6, or `evseId` for OCPP 2.0.1, matches the connector's `connector_id`. The component asks the charger to report `Current.Import`, `Power.Active.Import`, `Energy.Active.Import.Register`, and `Voltage`. If the charger omits one of those values, the corresponding sensor is published as unavailable/unknown instead of `0` so unsupported values are not confused with real zero measurements. Energy is exposed in `kWh`.

Connector `session_energy` and `session_time` reset to `0` when a car is plugged in. While the car remains plugged in, `session_energy` reports the difference from the connector's total energy reading at session start in `kWh`, and `session_time` reports elapsed whole seconds. When the car is unplugged, both sensors stop updating and keep the values from the last completed session.

Set connector `log_meter_values: true` to log a compact info-level summary of present sampled values, for example `A99999 MeterValues 1 Current: 10 A - Power: 6940 W - Energy: 7358900 Wh`. If a charger includes `phase`, the phase is shown next to that sampled value, for example `Current: L1=10 A, L2=10 A, L3=10 A`.

### Current control model

Current control is organized as a demand/allocation model. A connector owns its local limits and state, calculates the current it needs on each phase, and notifies the site when an allocation input changes. The site is the only layer that decides the final current allocation across all connectors and charge points. Charge points then execute the OCPP commands requested by the site.

The connector's needed current is derived from `current_limit`, charge point `max_current`, and the connector's active phases. For each active phase, the connector exposes the effective needed current; inactive phases need `0 A`. If the active phases are not known yet, current sharing should safely assume that the connector may use all configured phases.

| Concept                     | Name                           | Owner        | Meaning |
| ---                         | ---                            | ---          | --- |
| Hard charge-point cap       | `max_current`                  | Charge point | Physical or installation maximum for the whole charge point in `A`. |
| Connector safety cap        | `current_limit`                | Connector    | Local maximum for one connector in `A`; useful for safety limits or automations. |
| Connector demand            | `needed_current_l1`/`l2`/`l3`  | Connector    | Effective current needed by the connector on each phase after local limits and active-phase detection. |
| Allocated connector current | `control_current`              | Site         | Current in `A` allocated by the site and applied through OCPP commands. |

When a connector's needed current, measured current, status, transaction state, active phases, or another allocation-relevant value changes, the site recalculates allocations for all connectors. The site can then ask the related charge point to send `SetChargingProfile`, `RemoteStartTransaction`, or `RemoteStopTransaction` as needed. The OCPP command methods belong to the charge point, but the allocation decision belongs to the site.

For three-phase charge points, connector `phase_mapping` describes how connector pins map to charge point supply phases. A rotated mapping such as `[l2, l3, l1]` means the connector's L1 pin is supplied by charge point phase L2. The phase mapping is used when translating connector-local active phases into the phase currents that the site allocator must consider.

| Connector 1 need | Connector 2 need | `max_current` | Site allocation result |
| ---              | ---              | ---           | ---                    |
| `20 A`           | `32 A`           | `32 A`        | `16 A` / `16 A`        |
| `6 A`            | `32 A`           | `32 A`        | `6 A` / `26 A`         |
| `10 A`           | `10 A`           | `32 A`        | `10 A` / `10 A`        |
| `0 A`            | `32 A`           | `32 A`        | `0 A` / `32 A`         |

OCPP charging profiles cannot request a charging current below `6 A`. When the allocated value is greater than `0 A` but lower than `6 A`, the site treats the connector as disabled and applies `0 A` instead of sending an invalid sub-`6 A` charging profile.

Connector `status` and `error` text sensors are populated from `StatusNotification` messages whose `connectorId` matches the connector's `connector_id`. `errorCode: NoError` is exposed as an empty string.

### Charge point options

| Option                                   | Description |
| ---                                      | --- |
| `id` (Required)                          | ESPHome ID for this charge point. |
| `phases` (Required)                      | Number of supply phases available to this charge point. Must be `1`, `2`, or `3`. |
| `max_current` (Required)                 | Maximum configured current for this charge point in `A`. Must be at least `6 A` times the number of configured connectors; no upper limit is enforced. |
| `charge_point_id` (Optional)             | OCPP/WebSocket identity expected from the charger. When omitted, the first free dynamic charge point slot is used. |
| `connectors` (Optional)                  | List of OCPP connectors for this charge point. Defaults to one connector with `connector_id: 1`. Connector IDs must be unique within the charge point. |
| `debug_ocpp_messages` (Optional)         | Logs raw OCPP RX/TX payloads at debug level. Defaults to `false`. |
| `debug_ocpp_exclude_actions` (Optional)  | List of exact, case-sensitive OCPP action names excluded from raw debug payload logging. Known related responses are also excluded. Defaults to an empty list. |
| `startup_notifications_delay` (Optional) | Delay in seconds before sending `TriggerMessage` requests for missing startup notifications. `BootNotification` and `StatusNotification` are tracked independently; if both are missing, `BootNotification` is requested first and `StatusNotification` after its reply. Defaults to `300`. Set to `0` to disable. |
| `charger_info` (Optional)                | Text sensor that reports charger vendor, model, and firmware from `BootNotification`, and clears after disconnect. |
| `online` (Optional)                      | Binary sensor that is `on` after `BootNotification`, `Heartbeat`, or `StatusNotification`, and `off` after disconnect. |
| `protocol` (Optional)                    | Text sensor that reports the negotiated OCPP WebSocket protocol, and clears after disconnect. |

### Connector options

| Option                          | Description |
| ---                             | --- |
| `id` (Optional)                 | ESPHome internal ID for this connector. Usually omit this and let ESPHome generate it. |
| `connector_id` (Optional)       | Numeric OCPP connector ID used to match `MeterValues.connectorId` in OCPP 1.6 or `MeterValues.evseId` in OCPP 2.0.1. Defaults to `1`. Must be unique within the charge point. |
| `phase_mapping` (Optional)      | Ordered list mapping connector pins to charge point supply phases. Use values `l1`, `l2`, and `l3`, for example `[l2, l3, l1]` for a rotated three-phase connector. Defaults to the first configured connector phases in order. |
| `log_meter_values` (Optional)   | Logs a compact info-level summary of received `MeterValues` sampled values for this connector. Defaults to `false`. |
| `current` (Optional)            | Sensor populated from `Current.Import` `MeterValues` in `A`. Missing values are published as unavailable/unknown. |
| `current_limit` (Optional)      | Number entity for the connector current limit in `A`. Range is `0` to `max_value` when set, otherwise `0` to the charge point `max_current`, with a step of `1 A`. `max_value` must be less than or equal to the charge point `max_current`. |
| `needed_current_l1` (Optional)  | Sensor populated with the connector needed current on phase L1 in `A` after local limits and active-phase detection. |
| `needed_current_l2` (Optional)  | Sensor populated with the connector needed current on phase L2 in `A` after local limits and active-phase detection. |
| `needed_current_l3` (Optional)  | Sensor populated with the connector needed current on phase L3 in `A` after local limits and active-phase detection. |
| `control_current` (Optional)    | Sensor populated with the current in `A` allocated by the site and applied through OCPP commands. |
| `power` (Optional)              | Sensor populated from `Power.Active.Import` `MeterValues` in `W`. Missing values are published as unavailable/unknown. |
| `total_energy` (Optional)       | Sensor populated from the connector lifetime `Energy.Active.Import.Register` `MeterValues` in `kWh`. OCPP `Wh` values are converted to `kWh`. Missing values are published as unavailable/unknown. |
| `session_energy` (Optional)     | Sensor reset to `0 kWh` when a car is plugged in. While plugged in, it reports the difference from the total energy baseline at session start in `kWh`; after unplugging, it keeps the last session value. |
| `session_time` (Optional)       | Sensor reset to `0` seconds when a car is plugged in. While plugged in, it reports elapsed session time in whole seconds; after unplugging, it keeps the last session value. |
| `voltage` (Optional)            | Sensor populated from `Voltage` `MeterValues` in `V`. Missing values are published as unavailable/unknown. |
| `status` (Optional)             | Text sensor populated from `StatusNotification.status` for OCPP 1.6 or `StatusNotification.connectorStatus` for OCPP 2.0.1. Clears after disconnect. |
| `error` (Optional)              | Text sensor populated from `StatusNotification.errorCode` when the charger provides it. `NoError` is published as an empty string. Clears after disconnect. |

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
