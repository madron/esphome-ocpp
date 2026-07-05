# Codegen (`__init__.py`)

**Source:** `esphome/components/ocpp/__init__.py:1`

ESPHome Python codegen that defines the YAML schema, validates configuration, and generates C++ code.

## Dependencies

```python
DEPENDENCIES = ["network"]
AUTO_LOAD = ["binary_sensor", "json", "number", "sensor", "socket", "text_sensor"]
```

## Schema Hierarchy

```
CONFIG_SCHEMA (cv.All)
 ├── SERVER_SCHEMA
 │    ├── port (default 9000)
 │    └── path (default "/")
 ├── SITE_SCHEMA (Required)
 │    ├── phases (Required, 1-3)
 │    └── phase_voltage (Required, ≥1)
 └── CHARGE_POINT_SCHEMA[] (Optional, default [])
      ├── charge_point_id (Optional, validated)
      ├── phases (Required, 1-3, ≤ site phases)
      ├── phase_mapping (Optional, validated)
      ├── max_current (Required, ≥6)
      ├── connectors (Optional, default [{}])
      │    └── CONNECTOR_SCHEMA
      │         ├── connector_id (default 1)
      │         ├── phases, phase_mapping, log_meter_values
      │         └── all sensor/number/text_sensor/binary_sensor definitions
      ├── debug_ocpp_messages, debug_ocpp_exclude_actions
      ├── startup_notifications_delay (default 300s, max 4294967)
      ├── online, protocol, charger_info (optional sensors)
      └── force_protocol (optional, "ocpp1.6" or "ocpp2.0.1")
```

## Validation Pipeline

Applied as `cv.All(...)` wrappers on `CONFIG_SCHEMA`:

| Validator | Function | Behavior |
| --- | --- | --- |
| Socket accounting | `consume_sockets()` | Allocates `1 + len(charge_points)` sockets via `socket.consume_sockets()` |
| Cross-entity validation | `validate_charge_points()` | Checks duplicate charge_point_ids, connector_ids within a CP, phase mapping consistency, min max_current (6A × connectors), current_limit max_value ≤ max_current, current_limit initial_value ≤ max_value |

### Phase Mapping Validation (`validate_phase_mapping()`)

- Entries must be in range `[1, parent_phases]`
- No duplicates
- Length must exactly match the entity's phase count
- All entries must be available on the parent (site for CP, CP for connector)

### Server Path Validation (`validate_server_path()`)

- Must start with `/`
- Trailing slashes are stripped
- Empty path after stripping defaults to `/`

## `to_code()` Mapping

| YAML | C++ Method | Notes |
| --- | --- | --- |
| `server.port` | `set_server_port()` | |
| `server.path` | `set_server_path()` | |
| `site.phase_voltage` | `ChargePoint::set_phase_voltage()` | Propagated from site to charge point |
| `charge_points[].phases` | `ChargePoint::set_phases()` | |
| `charge_points[].phase_mapping` | `ChargePoint::set_phase_mapping()` | |
| `charge_points[].max_current` | `ChargePoint::set_max_current()` | Also set on each connector |
| `charge_points[].charge_point_id` | `ChargePoint::set_charge_point_id()` | Sets `connection_id_` = `charge_point_id_` |
| `charge_points[].force_protocol` | `ChargePoint::set_force_protocol()` | |
| `charge_points[].connectors` | `ChargePoint::add_connector()` | Composes phase mapping through CP |
| Connector sensors | `Connector::set_*_sensor()` | Each optional sensor has its own setter |
| `current_limit` | `CurrentLimit` number entity + `Connector::set_current_limit_number()` | `max_value` from config or falls back to `max_current`; `initial_value` sets `NumberTraits::set_initial_value()` |
| `requested_current` | `RequestedCurrent` number entity + `Connector::set_requested_current_number()` | Range 0 to `max_current`, step 0.1 |
| `debug_ocpp_exclude_actions` | `ChargePoint::add_debug_ocpp_exclude_action()` | One call per action |
| `debug_ocpp_messages` | `ChargePoint::set_debug_ocpp_messages()` | |
| `startup_notifications_delay` | `ChargePoint::set_startup_notifications_delay()` | YAML seconds → C++ milliseconds |

## Supported Protocols

```python
SUPPORTED_PROTOCOLS = ["ocpp1.6", "ocpp2.0.1"]
```
