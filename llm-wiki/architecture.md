# Architecture

This project is a single service — an OCPP Central System (CSMS) — communicating with the outside world
over three independent, open protocols. Not yet implemented; this is the design.

```
┌─────────────────────────────┐        OCPP 1.6/2.0.1        ┌───────────────────────────────────┐
│  OCPP charge point(s)        │◄──────over WebSocket─────────►│  Central System (CSMS)            │
│  (any OCPP-J charger,        │        (direct, no broker)    │  Python, this repository           │
│  not built by this project)  │                                │  (not yet implemented)             │
└─────────────────────────────┘                                └───────────────────┬────────────────┘
                                                                                     │
┌─────────────────────────────┐    ESPHome Native API                              │
│  Ancillary ESPHome node(s)  │◄───(aioesphomeapi, direct,                         │ MQTT (pub/sub:
│  e.g. whole-home energy      │    no broker, no HA needed)                        │ state + commands)
│  meter, unrelated to OCPP    │                                                    ▼
└─────────────────────────────┘                                          ┌───────────────────┐
                                                                          │  MQTT broker ◄────►│
                                                                          │  Home Assistant and/ │
                                                                          │  or other subscribers│
                                                                          └───────────────────┘
```

## What this project is

A Python service implementing the OCPP **Central System (CSMS)** role: it accepts WebSocket connections
from OCPP 1.6/2.0.1 charge points, handles the protocol (BootNotification, Heartbeat, StatusNotification,
Authorize, and transactions — StartTransaction/StopTransaction/MeterValues), keeps a small persistent
registry of known chargers/connectors/transactions, and exposes the result to Home Assistant.

## What this project is not

- **Not a charge-point firmware.** It does not build or flash EV charger hardware; it manages whatever
  OCPP-J-compliant chargers connect to it.
- **Not tied to ESPHome for its own identity.** The CSMS does not implement the ESPHome Native API as a
  *server* (it doesn't try to make itself discoverable as if it were an ESPHome device) — only as a
  *client*, to read/control other ESPHome nodes that aren't OCPP chargers.
- **Not tied to Home Assistant specifically.** MQTT is a first-class, general-purpose integration bus of
  the CSMS (state publishing and remote control), not an HA-only side channel; HA discovery is one
  consumer of it, layered on top via `ha-mqtt-discoverable`. The CSMS does not depend on HA Core being
  present, and does not use HA's native (non-MQTT) API.
- **No web dashboard planned.** Home Assistant's own auto-generated dashboard (via the MQTT discovery
  entities below) is the primary UI.

## Components / protocols

### OCPP server (charge points ↔ CSMS)

- Library: [`mobilityhouse/ocpp`](https://github.com/mobilityhouse/ocpp) (PyPI `ocpp`, MIT) — handles
  OCPP-J message framing, unique-ID correlation, JSON schema validation, and `@on(Action.x)`-decorated
  handler routing for OCPP 1.6 and 2.0.1. See `decisions.md` for why this library and not a C++
  alternative.
- Scope: connection handling, status (BootNotification/Heartbeat/StatusNotification/Authorize), and
  transactions (StartTransaction/StopTransaction/MeterValues). No TLS and no full persistence-of-everything
  in the first pass.
- Persistence: a small dedicated writable store for the charger/connector/transaction registry (see
  Deployment below) — everything else about the host stays read-only.

### MQTT integration (CSMS ↔ broker, bidirectional)

- MQTT is a real, general-purpose pub/sub integration of the CSMS, not just an HA discovery side effect:
  the CSMS **publishes** state (per-connector status, active transaction, energy delivered) to its own
  topics, and **subscribes** to command topics so it can be controlled by other MQTT clients/automations
  — not only through OCPP itself (e.g. a RemoteStartTransaction equivalent triggered over MQTT). Any
  MQTT-capable subscriber can use this, with or without Home Assistant.
- Library: [`ha-mqtt-discoverable`](https://github.com/unixorn/ha-mqtt-discoverable) (Apache-2.0, built on
  `paho-mqtt`) sits on top of that real MQTT connection and additionally publishes Home Assistant's
  documented discovery payloads (`homeassistant/<component>/[<node_id>/]<object_id>/config`) so HA
  auto-creates entities pointing at the same state/command topics — it is a convenience layer for HA
  auto-discovery, not a replacement for general MQTT pub/sub. Without a real, working MQTT client
  underneath it, `ha-mqtt-discoverable` has nothing to attach discovery payloads to and is useless on its
  own.

### Direct communication with ancillary ESPHome devices (CSMS ↔ ESPHome, no broker)

- Library: [`aioesphomeapi`](https://github.com/esphome/aioesphomeapi) (`esphome` org) — the same client
  Home Assistant core uses internally for ESPHome's Native API (protobuf/TCP, port 6053), usable fully
  standalone.
- Purpose: reading/controlling ESPHome devices that are *not* themselves OCPP charge points — e.g. a
  whole-home energy meter feeding smart/dynamic charging decisions — directly, without MQTT or HA in the
  path.

### Deployment / reliability

- Target: a Raspberry Pi-class host, not a microcontroller — a full CSMS (multiple long-lived
  connections, persistence, business logic) doesn't fit a microcontroller's resource envelope.
- Read-only root filesystem with a RAM/OverlayFS overlay for everything transient (`raspi-config` →
  *Performance Options* → *Overlay File System*, or DietPi as a turnkey base), so unplanned writes/power
  loss can't corrupt the OS or wear the storage.
- The one thing that must survive a reboot — the charger/transaction registry — lives in one small
  dedicated writable partition (flash-friendly filesystem, SQLite in WAL mode with reduced fsync
  frequency), isolated from the read-only OS.
- Hardware watchdog enabled for unattended recovery from hangs — matching the "set and forget, no moving
  parts" bar the user wants.

## Where decisions are recorded

`llm-wiki/decisions.md` has the detailed evaluation and reasoning for every choice above (library
comparisons against C++ alternatives, why Python, and how MQTT and `ha-mqtt-discoverable` relate).
