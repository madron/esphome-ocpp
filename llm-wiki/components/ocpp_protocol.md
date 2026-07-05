# OcppProtocol

**Source:** `esphome/components/ocpp/protocol.h:16` / `protocol.cpp:1`

Handles JSON serialization/deserialization of OCPP-J messages and dispatches by protocol version.

## Protocol Version Enum

| Value | WebSocket Subprotocol |
| --- | --- |
| `OCPP_1_6` | `ocpp1.6` |
| `OCPP_2_0_1` | `ocpp2.0.1` |

`set_websocket_protocol()` (`protocol.cpp:239`): Empty string or `"ocpp1.6"` → OCPP 1.6. `"ocpp2.0.1"` → OCPP 2.0.1.

## Parse: `parse_message()` (`protocol.cpp:302`)

1. Parse JSON → `JsonArray` frame
2. Extract `MessageTypeId`, dispatch to CALL or response
3. For CALL_RESULT / CALL_ERROR: check `unique_id` for known internal IDs:
   - `"get-configuration"` → `GetConfigurationResponse`
   - Prefix `"change-config-"` → `ChangeConfigurationResponse`
   - Otherwise → generic `OcppMessage`
4. For CALL: extract `action` + `payload` JSON:

| Action | Parser Function | Notes |
| --- | --- | --- |
| `BootNotification` | `parse_boot_notification_1_6()` / `parse_boot_notification_2_0_1()` | 2.0.1 reads from `chargingStation` sub-object |
| `Authorize` | `parse_authorize()` | 1.6 only |
| `StartTransaction` | `parse_start_transaction()` | 1.6 only |
| `StopTransaction` | `parse_stop_transaction()` | 1.6 only |
| `MeterValues` | `parse_meter_values()` | Reads from `evseId` (2.0.1) or `connectorId` (1.6) |
| `StatusNotification` | `parse_status_notification()` | Reads `connectorStatus` (2.0.1) or `status` (1.6) |
| Other | generic `OcppCall` | Will be rejected with NotImplementedError by ChargePoint |

### MeterValues Parsing Detail (`protocol.cpp:202`)

- Reads `connectorId` (1.6) or `evseId` (2.0.1) for connector routing
- Iterates `meterValue[]` → `sampledValue[]`
- Extracts: `value`, `measurand`, `unit` (from `unit` or `unitOfMeasure.unit`), `phase`
- Applies `unitOfMeasure.multiplier` as `10^multiplier` power-of-10 scaling

### GetConfiguration Parsing (`protocol.cpp:158`)

Iterates `configurationKey[]` looking for known keys:
- `MeterValueSampleInterval`
- `MeterValuesSampledData`
- `ConnectorSwitch3to1PhaseSupported`

## Make: Server → Charger Requests

All `make_*` methods are version-gated. Methods returning `""` for unsupported versions cause the caller to skip sending.

| Method | OCPP 1.6 | OCPP 2.0.1 | Frame type |
| --- | --- | --- | --- |
| `make_boot_notification_response()` | ✅ | ✅ | CALL_RESULT |
| `make_heartbeat_response()` | ✅ | ✅ | CALL_RESULT |
| `make_meter_values_response()` | ✅ | ✅ | CALL_RESULT |
| `make_status_notification_response()` | ✅ | ✅ | CALL_RESULT |
| `make_authorize_response()` | ✅ | ❌ | CALL_RESULT |
| `make_start_transaction_response()` | ✅ | ❌ | CALL_RESULT |
| `make_stop_transaction_response()` | ✅ | ❌ | CALL_RESULT |
| `make_set_charging_profile_request()` | ✅ (requires transaction_id) | ❌ | CALL |
| `make_remote_start_transaction_request()` | ✅ | ❌ | CALL |
| `make_remote_stop_transaction_request()` | ✅ (requires transaction_id) | ❌ | CALL |
| `make_get_configuration_request()` | ✅ | ❌ | CALL |
| `make_change_configuration_request()` | ✅ | ❌ | CALL |
| `make_trigger_boot_notification()` | ✅ | ✅ | CALL |
| `make_trigger_status_notification()` | ✅ | ✅ | CALL |
| `make_ocpp_error()` | ✅ | ✅ | CALL_ERROR |

## JSON Helpers (`protocol.cpp`)

| Function | Purpose |
| --- | --- |
| `json_escape()` | Escapes `"` and `\` for safe JSON string embedding |
| `json_string_or_empty()` | Extracts string from `JsonVariant`, returns `""` if not a string |
| `json_float_or_nan()` | Extracts float from variant (supports int/double/string with `strtof`) |
| `json_uint_or_default()` | Extracts unsigned int, returns default for negative/missing |
| `apply_metric_multiplier()` | Applies OCPP 2.0.1 `unitOfMeasure.multiplier` |
| `sampled_value_unit()` | Gets unit from OCPP 1.6 `unit` field or 2.0.1 `unitOfMeasure.unit` |
| `json_number()` | Float → string for JSON embedding |
