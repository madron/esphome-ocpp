# OcppComponent

**Source:** `esphome/components/ocpp/ocpp.h:14` / `ocpp.cpp:1`

Top-level ESPHome component. Owns the `OcppServer` and the list of `ChargePoint` instances.

## Responsibilities

- **`setup()`**: Sets itself as the server listener, configures max client count (one per charge point), starts the WebSocket server. Calls `mark_failed()` if the server cannot bind.
- **`loop()`**: Drives `OcppServer::loop()` (accept/read clients), calls `ChargePoint::loop(now)` on each charge point, then drains one outbound message via `send_queued_message_()`.
- **`get_setup_priority()`**: Returns `setup_priority::WIFI - 1.0f` — runs after WiFi is up.
- **`dump_config()`**: Logs the charger URL and all charge point IDs.

## Server Listener Interface (`OcppServerListener`)

`OcppComponent` implements all four callbacks:

| Callback | Behavior |
| --- | --- |
| `select_websocket_protocol()` | Assigns a free charge point slot via `assign_charge_point_for_connection_()`, then delegates to `select_supported_protocol()` with the charge point's forced protocol |
| `on_websocket_connected()` | Calls `ChargePoint::on_connected(connection_id, protocol)` |
| `on_websocket_disconnected()` | Calls `ChargePoint::on_disconnected()`. If the charge point has no explicit `charge_point_id` (dynamic slot), clears the connection ID |
| `on_websocket_text()` | Forwards to `ChargePoint::handle_ocpp_text()` |

## Outbound Scheduling

`send_queued_message_()` (`ocpp.cpp:91`):
- Round-robin across charge points starting from `next_outbound_charge_point_`
- Calls `ChargePoint::pop_queued_message()` on each
- Sends at most one message per loop iteration
- Uses `OcppServer::send_text(connection_id, message)` to deliver

## Charge Point Assignment

`assign_charge_point_for_connection_()` (`ocpp.cpp:119`):
- Checks if `connection_id` already matches a charge point
- If not, finds the first charge point with an empty `connection_id_` and assigns it
- Returns `nullptr` if all slots are taken → WebSocket handshake rejected
