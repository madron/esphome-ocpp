# llm-wiki — esphome-ocpp

Agent-readable reference for this project: a local OCPP 1.6/2.0.1 Central System (CSMS), not yet
implemented. Read this file first to navigate the wiki.

- [`architecture.md`](architecture.md) — system design: what the CSMS talks to, over which protocol, and
  why (OCPP-over-WebSocket to charge points, ESPHome Native API to ancillary devices, MQTT discovery to
  Home Assistant), plus explicit non-goals.
- [`decisions.md`](decisions.md) — design decision log: language/library choices for the CSMS and its
  Home Assistant/ESPHome integration, and the deployment/reliability model.
