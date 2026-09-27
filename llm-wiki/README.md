# llm-wiki — esphome-ocpp

Agent-readable reference for this project. Read this file first to navigate the wiki.

The project is two separate deployables: an ESPHome charge-point client component (this repo's
`esphome/`) and a companion OCPP Central System service (Python, not yet implemented). Start with
`architecture.md` for how they fit together before drilling into a specific file.

- [`architecture.md`](architecture.md) — system-level map: both components, the protocols between them
  (OCPP-over-WebSocket, ESPHome Native API, MQTT discovery), and explicit non-goals.
- [`decisions.md`](decisions.md) — design decision log: OCPP backend library choice for the ESP32 client
  and its ESP-IDF integration constraints, and the Central System's language/library/deployment choices.
- [`components/ocpp.md`](components/ocpp.md) — the `ocpp` custom ESPHome component: files,
  responsibilities, and current implementation status.
