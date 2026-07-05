# Design Decisions

Log of architectural and design decisions for the ESPHome OCPP component.

## 1. Server, Not Client

**Decision:** The ESPHome node runs a WebSocket **server** that chargers connect to, rather than being a WebSocket client.

**Rationale:** OCPP chargers are designed to connect to a central system. Running the server on the ESP eliminates the need for an external OCPP server (e.g., on a Raspberry Pi or cloud service).

## 2. Hand-Rolled WebSocket Implementation

**Decision:** `OcppServer` implements the HTTP upgrade handshake and WebSocket framing from scratch, using ESPHome's `socket` abstraction.

**Rationale:** The ESP-IDF Arduino ecosystem has limited WebSocket server library support that fits ESPHome's component model. A hand-rolled implementation keeps dependencies minimal and allows precise control over memory allocation and connection lifecycle.

## 3. One In-Flight CALL at a Time

**Decision:** `ChargePoint` tracks a single `in_flight_call_` and blocks new CALL messages while one is pending, with a 90-second timeout.

**Rationale:** Many OCPP chargers do not support pipelining multiple server-initiated CALLs. The serial constraint avoids confusing chargers and simplifies reply tracking.

## 4. Authorize Always Accepted

**Decision:** `Authorize` requests are always responded to with `"Accepted"`. No idTag validation is performed.

**Rationale:** The component is designed for local/home use where authorization is not needed. The charger can be configured with a free-access idTag.

## 5. Transaction IDs Assigned Locally

**Decision:** For OCPP 1.6 `StartTransaction`, the server assigns its own monotonically increasing transaction ID rather than delegating to the charger.

**Rationale:** The component is the source of truth for transaction state. Local IDs simplify tracking and allow recovery from charger-side transaction IDs via `MeterValues`.

## 6. Phase Mapping Composition at Setup

**Decision:** Connector phase mappings are composed through charge point mappings at `add_connector()` time, converting connector-local mapping to site-relative mapping once.

**Rationale:** Avoids cascading multiplication at runtime. The connector always works in site-relative phase space after setup.

## 7. 6A Minimum Charging Profile Current

**Decision:** When `control_current` is >0 but <6A, it is clamped to 0A.

**Rationale:** OCPP 1.6 charging profiles cannot specify a limit below 6A. Sending a sub-6A profile would be rejected or undefined. Treating the connector as disabled (0A) is the safe behavior.

## 8. MeterValuesSampledData Fallback Chain

**Decision:** `MeterValuesSampledData` is configured via `ChangeConfiguration` with a fallback chain from most-desired to least-desired measurand list.

**Rationale:** Not all chargers support all measurands. The fallback chain (full set → no energy → no voltage → current only) maximizes data collection while gracefully degrading.

## 9. Plugged Detection from Status

**Decision:** `Connector::plugged_` is derived from OCPP `StatusNotification.status` rather than requiring a dedicated sensor.

**Rationale:** Status strings `Preparing`, `Charging`, `SuspendedEVSE`, `SuspendedEV`, `Finishing`, `Occupied` all indicate a plugged state. This avoids needing charger-specific plug detection logic.

## 10. Session Energy Baseline from Total Energy

**Decision:** `session_energy` is computed as `total_energy - session_start_energy_`, where `session_start_energy_` is latched when the car is plugged in.

**Rationale:** Uses the charger's own energy meter as the source of truth. No need for an independent energy counter on the ESP.

## 11. Active Phase Inference

**Decision:** When per-phase current data is not available, active phases are inferred from the power/current/voltage ratio: `active_phases ≈ P / (V × I)`, rounded to the nearest integer.

**Rationale:** Many single-socket chargers report aggregate current without per-phase breakdown. The ratio method reliably distinguishes 1-phase from 3-phase charging in practice (single-phase ≈ P/(V×I) ≈ 1.0, three-phase ≈ 3.0).

## 12. Dynamic Charge Point Slots

**Decision:** Charge points without an explicit `charge_point_id` are assigned the first available slot when a charger connects.

**Rationale:** Useful for single-charger setups or when charger identity is not known in advance. The slot is released on disconnect for dynamic slots.

## 13. Version-Agnostic Message Model

**Decision:** `OcppMessage` subtypes represent the intersection of fields across OCPP 1.6 and 2.0.1, with version-specific parsing producing these common types.

**Rationale:** Allows `ChargePoint` handling code to be mostly protocol-version-independent. Version-specific code is confined to `OcppProtocol::parse_message()` and the `make_*()` methods.
