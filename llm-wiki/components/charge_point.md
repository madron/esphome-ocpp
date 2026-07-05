# ChargePoint

**Source:** `esphome/components/ocpp/charge_point.h:27` / `charge_point.cpp:1`

Represents one OCPP charger. Owns its `OcppProtocol` instance, outbound message queue, and a list of `Connector` instances.

## Key Constants

| Constant | Value | Location |
| --- | --- | --- |
| `DEFAULT_MAX_QUEUED_MESSAGES` | 8 | `charge_point.h:29` |
| `DEFAULT_STARTUP_NOTIFICATIONS_DELAY_MS` | 300000 (5 min) | `charge_point.h:30` |
| `DEFAULT_CALL_TIMEOUT_MS` | 90000 (90s) | `charge_point.h:31` |

## Lifecycle

### Connection

- **`on_connected(connection_id, protocol, now_millis)`** (`charge_point.cpp:89`): Stores connection ID, sets `OcppProtocol` version from the negotiated WebSocket subprotocol. Publishes protocol sensor. Resets charger info. Sets `connected_ = true`.
- **`on_disconnected()`** (`charge_point.cpp:100`): Resets all tracking state, clears in-flight call, clears message queue, publishes unavailable on all connectors, publishes empty protocol/charger_info sensors. Sets `online_ = false`.

### Message Handling

**`handle_ocpp_text()`** (`charge_point.cpp:119`):
1. Parses JSON → `OcppMessage` via `OcppProtocol::parse_message()`
2. Optionally logs debug payload (respecting exclude list)
3. Dispatches to `handle_ocpp_message_()` → `handle_ocpp_call_()` (CALL), `handle_ocpp_call_reply_()` (CALL_RESULT/CALL_ERROR)

**`handle_ocpp_call_()`** dispatch table (`charge_point.cpp:211`):

| Action | Handler | Side Effects |
| --- | --- | --- |
| `BootNotification` | inline | Accept, set online, publish charger info, send `GetConfiguration` |
| `Heartbeat` | inline | Respond with current time, set online |
| `Authorize` (1.6 only) | `handle_authorize_()` | Accept (always `"Accepted"`) |
| `StartTransaction` (1.6 only) | `handle_start_transaction_()` | Assign local transaction ID, send charging profile |
| `StopTransaction` (1.6 only) | `handle_stop_transaction_()` | Clear active transaction on connector |
| `MeterValues` | inline (via `publish_meter_values_()`) | Route to `Connector::publish_meter_values()`, recover transaction ID if needed |
| `StatusNotification` | inline (via `publish_status_notification_()`) | Route to `Connector::publish_status_notification()`, trigger RemoteStart if plug-in detected |
| Other | inline | Send `NotImplemented` OCPP error |

### Startup Notification Triggers

After `startup_notifications_delay` since connection (`charge_point.cpp:528`):
- If `BootNotification` was never received → send `TriggerMessage` for BootNotification
- After BootNotification reply → send `TriggerMessage` for StatusNotification
- Only one in-flight CALL at a time; respects serial CALL gating

### Configuration Flow (OCPP 1.6)

After BootNotification acceptance:
1. Send `GetConfiguration` requesting `MeterValueSampleInterval`, `MeterValuesSampledData`, `ConnectorSwitch3to1PhaseSupported`
2. On response → send `ChangeConfiguration` for `MeterValuesSampledData` with fallback chain:
   - `"Current.Import,Power.Active.Import,Energy.Active.Import.Register,Voltage"`
   - `"Current.Import,Power.Active.Import,Energy.Active.Import.Register"`
   - `"Current.Import,Power.Active.Import"`
   - `"Current.Import"`
3. On success/rejection → send `ChangeConfiguration` for `MeterValueSampleInterval` = `"5"`

## OCPP Commands to Charger

| Method | Purpose | Guard |
| --- | --- | --- |
| `send_connector_control_current_()` | `SetChargingProfile` with new current limit | Must be connected, must have active transaction, control_current > 0 |
| `send_connector_remote_start_transaction_()` | `RemoteStartTransaction` with idTag `"free"` | Must be connected |
| `send_connector_remote_stop_transaction_()` | `RemoteStopTransaction` | Must be connected, must have active transaction |

## ConnectorListener Callback

**`on_connector_control_current_changed()`** (`charge_point.cpp:284`):
- `0 → nonzero`: send `RemoteStartTransaction`
- `nonzero → 0`: send `RemoteStopTransaction`
- `nonzero → nonzero`: send `SetChargingProfile` with new current

## Online Tracking

`online_` becomes `true` on any of: `BootNotification`, `Heartbeat`, `Authorize`, `StartTransaction`, `StopTransaction`, `MeterValues`, `StatusNotification`. Becomes `false` on `on_disconnected()`. Published via optional `online` binary sensor.

## Debug Logging

When `debug_ocpp_messages` is `true`, raw payloads are logged at debug level. Actions in `debug_ocpp_exclude_actions_` (and their known related responses) are suppressed. Large messages are split into 360-char chunks (`charge_point.cpp:33`).
