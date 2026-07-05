# Connector

**Source:** `esphome/components/ocpp/connector.h:32` / `connector.cpp:1`

Represents one OCPP connector (charging outlet) within a `ChargePoint`. Owns all per-outlet sensors, session tracking, and current control state.

## Key Constants

| Constant | Value | Location |
| --- | --- | --- |
| `DEFAULT_CONNECTOR_ID` | 1 | `connector.h:34` |
| `MIN_CHARGING_PROFILE_CURRENT` | 6.0 A | `connector.cpp:11` |

## State Model

| Field | Type | Meaning |
| --- | --- | --- |
| `connector_id_` | `uint32_t` | OCPP connector ID (matches `MeterValues.connectorId` / `StatusNotification.connectorId`) |
| `phases_` | `uint8_t` | Number of configured supply phases |
| `phase_mapping_` | `array<uint8_t, 3>` | Site-relative phase mapping (composed at `add_connector()` time) |
| `max_current_` | `uint32_t` | Charge point `max_current` (installation limit) |
| `current_limit_` | `float` | Local safety cap in A (settable via `CurrentLimit` number entity) |
| `requested_current_` | `float` | Demand current in A (settable via `RequestedCurrent` number entity) |
| `control_current_` | `float` | Effective allocated current: `min(requested, limit, max)`, clamped to 0 if <6A |
| `active_phases_` | `uint8_t` | Detected active phase count (0 = unknown, latched from meter values) |
| `status_` | `string` | Latest OCPP status string |
| `plugged_` | `bool` | Whether car is plugged in (derived from status) |
| `active_transaction_id_` | `uint32_t` | Active OCPP transaction ID (0 = none) |

## Plugged Detection

`get_plugged_from_status()` (`connector.cpp:13`): Returns `true` for statuses `Preparing`, `Charging`, `SuspendedEVSE`, `SuspendedEV`, `Finishing`, `Occupied`.

## Current Control

### `calculate_control_current()` (free function, `connector.cpp:23`)

```
control_current = min(requested_current, current_limit, max_current)
if control_current in (0, 6) → 0
```

### `update_needed_current_()` (`connector.cpp:137`)

Needed current per phase, published as `needed_current_l1/l2/l3`:
- If plugged and not `SuspendedEV`: `needed = min(current_limit, max_current)`, <6A → 0
- If `active_phases_ == 0`: assume all configured phases
- Distribute across active phases, then apply `map_phases()` for site-relative mapping
- If not plugged or `SuspendedEV`: all needed currents = 0

### `update_control_current_()` (`connector.cpp:183`)

Computes `control_current_` and notifies `ConnectorListener::on_connector_control_current_changed()` if changed.

## Session Tracking

| Event | Action | Location |
| --- | --- | --- |
| `on_session_start()` (`connector.cpp:274`) | Latch `session_start_energy_`, reset `session_time_` to 0, reset `session_start_millis_` | Called when `plugged_` transitions to `true` |
| `on_session_stop()` (`connector.cpp:285`) | Final `update_session_time_()` | Called when `plugged_` transitions to `false` |
| `update_session_energy_()` (`connector.cpp:290`) | `session_energy = total_energy - session_start_energy_`, publish | Called from `publish_meter_values()` |
| `update_session_time_()` (`connector.cpp:305`) | `session_time = (now - session_start_millis) / 1000` | Called from `loop()` while plugged, and `on_session_stop()` |

## Meter Values Processing

`publish_meter_values()` (`connector.cpp:314`):
1. Creates a copy of the `MeterValues` struct
2. Calls `calculate_phase_values()` with latched active phases (or `NAN` if not yet known)
3. If plugged and `active_phases_ == 0`, latches newly inferred active phases from the meter values
4. If plugged and active phases changed, recalculates phase values with the latched count
5. Optionally logs compact summary if `log_meter_values_` is `true`
6. Publishes all configured sensors: `current`, `current_l1/l2/l3`, `power`, `total_energy`, `voltage`, `voltage_l1/l2/l3`, `active_phases`
7. Calls `update_session_energy_()` with the energy value

## Status Notification Processing

`publish_status_notification()` (`connector.cpp:358`):
1. Updates `last_update_millis_`
2. Extracts error code (`"NoError"` → empty string)
3. Derives `plugged_` from status → `set_plugged_()` → triggers session start/stop
4. If status changed → calls `update_needed_current_()` (needed current depends on plugged/suspended state)
5. Publishes status and error text sensors

## Phase Mapping

`map_phases()` (`connector.cpp:125`) maps connector-local phase currents to site-relative phase currents using `phase_mapping_[]`.

At setup time, `ChargePoint::add_connector()` composes the connector's `phase_mapping` through the charge point's mapping to get site-relative mapping, then overwrites the connector's mapping.

## Sensor Wiring

All sensors are optional (set to `nullptr` by default). The Python codegen wires configured YAML sensors via setter methods:

| Sensor | ESPHome Type | Setter |
| --- | --- | --- |
| `current`, `current_l1/l2/l3`, `power`, `total_energy`, `voltage`, `voltage_l1/l2/l3`, `active_phases`, `control_current`, `needed_current_l1/l2/l3`, `session_energy`, `session_time` | `sensor::Sensor` | `set_*_sensor()` |
| `status`, `error` | `text_sensor::TextSensor` | `set_status_text_sensor()`, `set_error_text_sensor()` |
| `active_transaction`, `plugged` | `binary_sensor::BinarySensor` | `set_active_transaction_binary_sensor()`, `set_plugged_binary_sensor()` |
| `current_limit` | `CurrentLimit` (Number) | `set_current_limit_number()` |
| `requested_current` | `RequestedCurrent` (Number) | `set_requested_current_number()` |

## Number Entities

### `CurrentLimit` (`connector.h:181`)

Extends `number::Number`. `control(value)` calls `Connector::set_current_limit(value)`. Range: `0` to `current_limit_max_` (default: `max_current`).

### `RequestedCurrent` (`connector.h:191`)

Extends `number::Number`. `control(value)` calls `Connector::set_requested_current(value)`. Range: `0` to `max_current`, step 0.1 A.
