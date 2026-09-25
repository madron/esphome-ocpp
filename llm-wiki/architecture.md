# Architecture

## Overview

The ESPHome OCPP component turns an ESP32 into a **local OCPP central system** for EV chargers. It runs a WebSocket server that EV chargers connect to, then speaks OCPP 1.6J or 2.0.1 to control charging sessions.

The component is structured around three hierarchical concepts matching the OCPP electrical model:

```
Site (shared electrical installation)
 └── ChargePoint (one OCPP charger device)
      ├── Connector 1 (individual charging outlet)
      └── Connector N
```

## Class Hierarchy

```
OcppComponent (esp::Component + OcppServerListener)
 ├── OcppServer            — raw TCP/WebSocket server
 │    └── ClientSessions   — connected charger sockets
 └── ChargePoint[]         — one per configured/connected charger
      ├── OcppProtocol     — JSON parse/make, version dispatch
       ├── Connector[]      — per-outlet state + sensors
       │    └── CurrentLimit       (number entity)
       └── QueuedMessage[]  — outbound message queue
```

## Data Flow

### Inbound (charger → ESP)

1. `OcppServer::loop()` accepts TCP connections and reads WebSocket frames
2. HTTP upgrade handshake → protocol negotiation → `OcppServerListener::on_websocket_connected()`
3. Text frames → `OcppServerListener::on_websocket_text()` → `OcppComponent::on_websocket_text()`
4. `OcppComponent` finds the `ChargePoint` by `connection_id`
5. `ChargePoint::handle_ocpp_text()` → `OcppProtocol::parse_message()` → typed `OcppMessage`
6. `ChargePoint::handle_ocpp_call_()` dispatches by action name
7. Results routed to `Connector::publish_meter_values()` or `Connector::publish_status_notification()`

### Outbound (ESP → charger)

1. `ChargePoint` builds JSON via `OcppProtocol::make_*()` → `QueuedMessage`
2. Messages stored in `ChargePoint::messages_` (bounded queue, default 8)
3. `OcppComponent::loop()` calls `send_queued_message_()` round-robin across charge points
4. CALL messages serialized one at a time (in-flight call tracking)
5. `OcppServer::send_text()` → WebSocket frame encode → TCP write

## OCPP Protocol Lifecycle

```
WebSocket connect
  ↓
BootNotification ← [charger]    → Accepted, interval=300s
  ↓
GetConfiguration → charger      (MeterValueSampleInterval, MeterValuesSampledData, etc.)
  ↓
ChangeConfiguration → charger   (set MeterValuesSampledData, with fallback chain)
ChangeConfiguration → charger   (set MeterValueSampleInterval = 5s)
  ↓
  [steady state: Heartbeat, StatusNotification, MeterValues, Authorize,
   StartTransaction, StopTransaction]
  ↓
  [server-initiated: SetChargingProfile, RemoteStartTransaction,
   RemoteStopTransaction, TriggerMessage]
```

### Startup Sequence

1. `OcppComponent::setup()` — starts `OcppServer` listen socket
2. `OcppComponent::get_setup_priority()` returns `WIFI - 1` (runs after WiFi)
3. Server waits for charger WebSocket connections
4. On connect: negotiate protocol (ocpp1.6 / ocpp2.0.1), assign charge point slot
5. On BootNotification: accept → send GetConfiguration → configure meter values
6. After `startup_notifications_delay` seconds: send TriggerMessage for any missing BootNotification / StatusNotification

## Current Control Model

Demand/allocation model. A connector owns its local limits and state, calculates the current it needs on each phase, and notifies the site when an allocation input changes. The site is the only layer that decides the final current allocation across all connectors and charge points. Charge points then execute the OCPP commands requested by the site.

| Concept | Field | Owner | Meaning |
| --- | --- | --- | --- |
| Installation cap | `max_current` | Charge point | Physical or installation maximum for the whole charge point in `A`. Acts as a safety cap, but real safety must be enforced by an electrical technician via electrical protections (e.g., circuit breakers). |
| User manual limit | `current_limit` | Connector (number entity) | User-settable manual limit for one connector in `A`; allows deliberately slowing the charge. Not safety-related and not intended for automations. |
| Effective needed current | `needed_current_l1/l2/l3` | Connector (computed) | Current needed by the connector on each phase after local limits and active-phase detection. Inactive phases need `0 A`. If active phases are unknown, current sharing assumes the connector may use all configured phases. |
| Allocated current | `control_current` | Site (computed) | Current in `A` allocated by the site and applied through OCPP commands. |

`control_current = min(current_limit, max_current)`. Values >0 but <6A clamp to 0A (OCPP minimum).

When a connector's needed current, measured current, status, transaction state, active phases, or another allocation-relevant value changes, the site recalculates allocations for all connectors. When `control_current` changes, `ConnectorListener::on_connector_control_current_changed()` fires on the charge point, which then sends `SetChargingProfile`, `RemoteStartTransaction`, or `RemoteStopTransaction` as appropriate. The OCPP command methods belong to the charge point, but the allocation decision belongs to the site.

### Allocation Examples

| Connector 1 need | Connector 2 need | `max_current` | Site allocation result |
| --- | --- | --- | --- |
| `20 A` | `32 A` | `32 A` | `16 A` / `16 A` |
| `6 A` | `32 A` | `32 A` | `6 A` / `26 A` |
| `10 A` | `10 A` | `32 A` | `10 A` / `10 A` |
| `0 A` | `32 A` | `32 A` | `0 A` / `32 A` |

### Phase Mapping in Allocation

For multi-phase installations, charge point `phase_mapping` describes how charge point phases map to site phases. A rotated mapping such as `[2, 3, 1]` means charge point phase 1 is supplied by site phase 2. Connector `phase_mapping` follows the same rule relative to its parent charge point. Phase mappings are used when translating connector-local active phases into the phase currents that the site allocator must consider.

## Phase Mapping

Phase mappings cascade: **site → charge point → connector**.

- Charge point `phase_mapping: [2, 3, 1]` means CP phase 1 → site phase 2
- Connector `phase_mapping: [2, 3, 1]` means connector phase 1 → CP phase 2
- At `add_connector()` time, the connector's phase mapping is composed through the charge point mapping to get site-relative mapping

## Message Queue

- `ChargePoint` maintains a bounded queue (`messages_`, default max 8)
- CALL messages blocked while an in-flight CALL is pending (`in_flight_call_`)
- CALL_RESULT / CALL_ERROR pass through immediately
- In-flight calls time out after `DEFAULT_CALL_TIMEOUT_MS` (90s)
- `OcppComponent::send_queued_message_()` drains one message per loop iteration, round-robin across charge points

## Supported OCPP Actions

| Direction | OCPP 1.6 | OCPP 2.0.1 |
| --- | --- | --- |
| Charger → Server | `BootNotification`, `Heartbeat`, `Authorize`, `StartTransaction`, `StopTransaction`, `MeterValues`, `StatusNotification` | `BootNotification`, `Heartbeat`, `MeterValues`, `StatusNotification` |
| Server → Charger | `SetChargingProfile`, `RemoteStartTransaction`, `RemoteStopTransaction`, `TriggerMessage`, `GetConfiguration`, `ChangeConfiguration` | same (version-gated per method) |
