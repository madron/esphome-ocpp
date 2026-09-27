# esphome-ocpp

An OCPP 1.6/2.0.1 Central System (CSMS) — a Python service for managing OCPP-compliant EV chargers
locally, without a commercial/cloud backend, with Home Assistant integration over MQTT discovery.

Not yet implemented — see [`llm-wiki/architecture.md`](llm-wiki/architecture.md) for the design and
[`llm-wiki/decisions.md`](llm-wiki/decisions.md) for the reasoning behind it.

## Scope

- OCPP Central System role only: accepts connections from OCPP charge points, handles
  BootNotification/Heartbeat/StatusNotification/Authorize and transactions
  (StartTransaction/StopTransaction/MeterValues).
- Publishes its own entities to Home Assistant via MQTT discovery.
- Talks directly to ancillary (non-charger) ESPHome devices — e.g. a whole-home energy meter for
  smart-charging decisions — over ESPHome's Native API, without a broker.
- Deployment target is a Raspberry Pi-class host, run "set and forget" (read-only root, minimal writes).
