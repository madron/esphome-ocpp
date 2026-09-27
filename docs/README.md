# esphome-ocpp — Central System

This project is a local OCPP 1.6/2.0.1 Central System (CSMS): a Python service that accepts connections
from OCPP-compliant EV chargers and exposes their status to Home Assistant.

**Not yet implemented.** There is no installable service, configuration file, or CLI yet — this
directory will hold the user-facing configuration reference, deployment guide, and Home Assistant
integration notes once the CSMS exists. Until then, see `llm-wiki/architecture.md` (system design) and
`llm-wiki/decisions.md` (library and deployment choices) in this repository for the current plan.

## Planned scope

- Accept OCPP-J (WebSocket) connections from charge points; handle BootNotification, Heartbeat,
  StatusNotification, Authorize, and transactions (StartTransaction, StopTransaction, MeterValues).
- Publish per-connector/transaction entities to Home Assistant via MQTT discovery.
- Read/control ancillary (non-charger) ESPHome devices directly over their Native API, for use in
  charging decisions (e.g. a whole-home energy meter), without needing an MQTT broker for that link.
- Run unattended on a Raspberry Pi-class host with minimal writes to local storage.
