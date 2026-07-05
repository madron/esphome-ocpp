# ESPHome OCPP Component

Component for controlling OCPP EV chargers from a local node.

The initial target protocol is **OCPP 1.6J**.

## Core Concepts

The configuration is organized around three electrical and OCPP concepts: the
site, chargers, and connectors. Each level has a different responsibility.

- The `site` describes the shared electrical installation. It defines the number
  of available phases and the phase-to-neutral voltage in `V`.
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

  site:
    phases: 3
    phase_voltage: 230

  charge_points:
    - id: garage_left
      charge_point_id: A99999
      phases: 3
      phase_mapping: [1, 2, 3]
      max_current: 32
      debug_ocpp_messages: true
      debug_ocpp_exclude_actions:
        - MeterValues
      startup_notifications_delay: 300
      charger_info:
        name: Garage Charger Info
      connectors:
        - connector_id: 1
          phase_mapping: [1, 2, 3]
          current:
            name: Garage Current
          current_l1:
            name: Garage Current L1
          current_l2:
            name: Garage Current L2
          current_l3:
            name: Garage Current L3
          log_meter_values: true
          current_limit:
            name: Garage Current Limit
            max_value: 16
            initial_value: 16
          needed_current_l1:
            name: Garage Needed Current L1
          needed_current_l2:
            name: Garage Needed Current L2
          needed_current_l3:
            name: Garage Needed Current L3
          control_current:
            name: Garage Allocated Current
          active_phases:
            name: Garage Active Phases
          plugged:
            name: Garage Plugged
          power:
            name: Garage Power
          total_energy:
            name: Garage Total Energy
          session_energy:
            name: Garage Session Energy
          session_time:
            name: Garage Session Time
          active_transaction:
            name: Garage Active Transaction
          voltage:
            name: Garage Voltage
          voltage_l1:
            name: Garage Voltage L1
          voltage_l2:
            name: Garage Voltage L2
          voltage_l3:
            name: Garage Voltage L3
          status:
            name: Garage Status
          error:
            name: Garage Error
```

`debug_ocpp_messages` is optional per `charge_point`. When enabled, raw OCPP RX/TX payloads for that charger are logged at the ESPHome debug log level. Use `debug_ocpp_exclude_actions` to keep debug logging enabled while suppressing noisy action payloads, such as `MeterValues`, and their known related responses.

Connector `current`, `current_l1`, `current_l2`, `current_l3`, `power`, `total_energy`, `voltage`, `voltage_l1`, `voltage_l2`, and `voltage_l3` sensors are populated from OCPP `MeterValues` messages whose `connectorId` for OCPP 1.6, or `evseId` for OCPP 2.0.1, matches the connector's `connector_id`. The per-phase `current_l*` and `voltage_l*` sensors report individual phase readings when the charger provides them; the aggregate `current` and `voltage` sensors report the total or representative value. The component asks the charger to report `Current.Import`, `Power.Active.Import`, `Energy.Active.Import.Register`, and `Voltage`. If the charger omits one of those values, the corresponding sensor is published as unavailable/unknown instead of `0` so unsupported values are not confused with real zero measurements. Energy is exposed in `kWh`.

Connector `session_energy` and `session_time` reset to `0` when a car is plugged in. While the car remains plugged in, `session_energy` reports the difference from the connector's total energy reading at session start in `kWh`, and `session_time` reports elapsed whole seconds. When the car is unplugged, both sensors stop updating and keep the values from the last completed session.

Connector `active_phases` reports the number of active charging phases detected from the most recent `MeterValues` message. The value is `NaN` until the first meter values arrive while the connector is plugged in; once latched, it is updated when the charger reports a different phase count.

Set connector `log_meter_values: true` to log a compact info-level summary of present sampled values, for example `A99999 MeterValues 1 Current: 10 A - Power: 6940 W - Energy: 7358900 Wh`. If a charger includes `phase`, the phase is shown next to that sampled value, for example `Current: L1=10 A, L2=10 A, L3=10 A`.

Connector `plugged` is a binary sensor that is `on` when the connector status indicates a plugged-in vehicle (`Preparing`, `Charging`, `SuspendedEVSE`, `SuspendedEV`, `Finishing`, `Occupied`), and `off` otherwise. It is derived from the same `StatusNotification` messages that populate `status` and `error`.

Connector `status` and `error` text sensors are populated from `StatusNotification` messages whose `connectorId` matches the connector's `connector_id`. `errorCode: NoError` is exposed as an empty string.

Connector `active_transaction` is a binary sensor that turns `on` when the connector has a non-zero active OCPP transaction ID and `off` otherwise. This is mainly useful for debugging transaction recovery and charging-profile edge cases.

### Server options

| Option          | Description |
| ---             | --- |
| `port` (Optional) | TCP port for the WebSocket server. Defaults to `9000`. |
| `path` (Optional) | WebSocket server URI path. Must start with `/`. Defaults to `/`. Charger URLs must match this path. |

### Site options

| Option                             | Description |
| ---                                | --- |
| `site` (Required)                  | Shared electrical installation settings. |
| `phases` (Required)                | Number of site phases available to charge points. Must be `1`, `2`, or `3`. |
| `phase_voltage` (Required)         | Integer phase-to-neutral voltage in `V`, used for phase inference from meter values. |

### Charge point options

| Option                                   | Description |
| ---                                      | --- |
| `id` (Required)                          | ESPHome ID for this charge point. |
| `phases` (Required)                      | Number of supply phases available to this charge point. Must be `1`, `2`, or `3`, and less than or equal to site `phases`. |
| `phase_mapping` (Optional)               | Ordered integer list mapping charge point phases to site phases, for example `[2, 3, 1]` for a rotated three-phase charge point. Entries must be unique, available on the site, and exactly match the charge point phase count. Defaults to `[1, 2, 3]`, `[1, 2]`, or `[1]` according to `phases`. |
| `max_current` (Required)                 | Maximum configured current for this charge point in `A`. Must be at least `6 A` times the number of configured connectors; no upper limit is enforced. |
| `charge_point_id` (Optional)             | OCPP/WebSocket identity expected from the charger. When omitted, the first free dynamic charge point slot is used. |
| `connectors` (Optional)                  | List of OCPP connectors for this charge point. Defaults to one connector with `connector_id: 1`. Connector IDs must be unique within the charge point. |
| `debug_ocpp_messages` (Optional)         | Logs raw OCPP RX/TX payloads at debug level. Defaults to `false`. |
| `debug_ocpp_exclude_actions` (Optional)  | List of exact, case-sensitive OCPP action names excluded from raw debug payload logging. Known related responses are also excluded. Defaults to an empty list. |
| `force_protocol` (Optional)              | Force a specific OCPP protocol version instead of negotiating from the charger's offered protocols. Must be `ocpp1.6` or `ocpp2.0.1`. |
| `startup_notifications_delay` (Optional) | Delay in seconds before sending `TriggerMessage` requests for missing startup notifications. `BootNotification` and `StatusNotification` are tracked independently; if both are missing, `BootNotification` is requested first and `StatusNotification` after its reply. Defaults to `300`. Set to `0` to disable. |
| `charger_info` (Optional)                | Text sensor that reports charger vendor, model, and firmware from `BootNotification`, and clears after disconnect. |
| `online` (Optional)                      | Binary sensor that is `on` after `BootNotification`, `Heartbeat`, or `StatusNotification`, and `off` after disconnect. |
| `protocol` (Optional)                    | Text sensor that reports the negotiated OCPP WebSocket protocol, and clears after disconnect. |

### Connector options

| Option                          | Description |
| ---                             | --- |
| `id` (Optional)                 | ESPHome internal ID for this connector. Usually omit this and let ESPHome generate it. |
| `connector_id` (Optional)       | Numeric OCPP connector ID used to match `MeterValues.connectorId` in OCPP 1.6 or `MeterValues.evseId` in OCPP 2.0.1. Defaults to `1`. Must be unique within the charge point. |
| `phases` (Optional)             | Number of connector phases. Must be less than or equal to the parent charge point `phases`. Defaults to the parent charge point phase count. |
| `phase_mapping` (Optional)      | Ordered integer list mapping connector phases to parent charge point phases, for example `[2, 3, 1]` for a rotated three-phase connector. Entries must be unique, available on the charge point, and exactly match the connector phase count. Defaults to `[1, 2, 3]`, `[1, 2]`, or `[1]` according to `phases`. |
| `log_meter_values` (Optional)   | Logs a compact info-level summary of received `MeterValues` sampled values for this connector. Defaults to `false`. |
| `current` (Optional)            | Sensor populated from `Current.Import` `MeterValues` in `A`. Missing values are published as unavailable/unknown. |
| `current_l1` (Optional)         | Sensor populated from `Current.Import` `MeterValues` phase L1 in `A`. Missing values are published as unavailable/unknown. |
| `current_l2` (Optional)         | Sensor populated from `Current.Import` `MeterValues` phase L2 in `A`. Missing values are published as unavailable/unknown. |
| `current_l3` (Optional)         | Sensor populated from `Current.Import` `MeterValues` phase L3 in `A`. Missing values are published as unavailable/unknown. |
| `current_limit` (Optional)      | Number entity for the connector current limit in `A`. Range is `0` to `max_value` when set, otherwise `0` to the charge point `max_current`, with a step of `1 A`. `max_value` must be less than or equal to the charge point `max_current`. Optional `initial_value` sets the value published at boot when no restored state exists; must be less than or equal to `max_value` (or `max_current` if `max_value` is omitted). |
| `needed_current_l1` (Optional)  | Sensor populated with the connector needed current on phase L1 in `A` after local limits and active-phase detection. |
| `needed_current_l2` (Optional)  | Sensor populated with the connector needed current on phase L2 in `A` after local limits and active-phase detection. |
| `needed_current_l3` (Optional)  | Sensor populated with the connector needed current on phase L3 in `A` after local limits and active-phase detection. |
| `control_current` (Optional)    | Sensor populated with the current in `A` allocated by the site and applied through OCPP commands. |
| `active_phases` (Optional)      | Sensor reporting the detected number of active charging phases. `NaN` until detection completes from the first `MeterValues` while plugged in. |
| `power` (Optional)              | Sensor populated from `Power.Active.Import` `MeterValues` in `W`. Missing values are published as unavailable/unknown. |
| `total_energy` (Optional)       | Sensor populated from the connector lifetime `Energy.Active.Import.Register` `MeterValues` in `kWh`. OCPP `Wh` values are converted to `kWh`. Missing values are published as unavailable/unknown. |
| `session_energy` (Optional)     | Sensor reset to `0 kWh` when a car is plugged in. While plugged in, it reports the difference from the total energy baseline at session start in `kWh`; after unplugging, it keeps the last session value. |
| `session_time` (Optional)       | Sensor reset to `0` seconds when a car is plugged in. While plugged in, it reports elapsed session time in whole seconds; after unplugging, it keeps the last session value. |
| `active_transaction` (Optional) | Binary sensor that is `on` when the connector currently has a non-zero active OCPP transaction ID, and `off` otherwise. Useful for debugging transaction recovery. |
| `plugged` (Optional)            | Binary sensor that is `on` when the connector status indicates a plugged-in vehicle (`Preparing`, `Charging`, `SuspendedEVSE`, `SuspendedEV`, `Finishing`, `Occupied`), and `off` otherwise. |
| `voltage` (Optional)            | Sensor populated from `Voltage` `MeterValues` in `V`. Missing values are published as unavailable/unknown. |
| `voltage_l1` (Optional)         | Sensor populated from `Voltage` `MeterValues` for phase L1 in `V`. Missing values are published as unavailable/unknown. |
| `voltage_l2` (Optional)         | Sensor populated from `Voltage` `MeterValues` for phase L2 in `V`. Missing values are published as unavailable/unknown. |
| `voltage_l3` (Optional)         | Sensor populated from `Voltage` `MeterValues` for phase L3 in `V`. Missing values are published as unavailable/unknown. |
| `status` (Optional)             | Text sensor populated from `StatusNotification.status` for OCPP 1.6 or `StatusNotification.connectorStatus` for OCPP 2.0.1. Clears after disconnect. |
| `error` (Optional)              | Text sensor populated from `StatusNotification.errorCode` when the charger provides it. `NoError` is published as an empty string. Clears after disconnect. |

### Connector status values

The `status` text sensor reports the OCPP connector status. The following values are defined by the OCPP specification:

| Status          | Description |
| ---             | --- |
| `Available`     | Connector is available for use (no vehicle plugged in). |
| `Preparing`     | Connector is preparing for charging (vehicle plugged in, pre-conditioning or authentication in progress). |
| `Charging`      | Connector is actively charging a vehicle. |
| `SuspendedEVSE` | Charging is suspended by the EVSE (charger). The vehicle is still connected but not drawing current. |
| `SuspendedEV`   | Charging is suspended by the EV (vehicle). The vehicle is still connected but not drawing current. |
| `Finishing`     | Charging session is finishing (vehicle still plugged in, charging complete or nearly complete). |
| `Reserved`      | Connector is reserved for a specific user or vehicle (not available for general use). |
| `Unavailable`   | Connector is unavailable (out of service, maintenance, or communication lost). |
| `Faulted`       | Connector has a fault and requires attention. Check the `error` sensor for details. |

### Charger configuration

With the default server path `/`, configure the charger OCPP/WebSocket server URL as:
```text
ws://<esp-ip>:9000
```

If a custom `server.path` is set (e.g. `/ocpp`), the charger URL must include that path:
```text
ws://<esp-ip>:9000/ocpp
```

### Units of Measure

- Power: Watts (`W`)
- Current: Amperes (`A`)
- Voltage: Volts (`V`)
- Energy: kilowatt-hours (`kWh`)
