# Architecture

This project is two separate deployables that together let ESPHome-based EV charger hardware
participate in OCPP, with results visible in Home Assistant. They communicate over open, documented
protocols only — there is no shared code, shared process, or required broker between them beyond what
each protocol itself needs.

```
┌─────────────────────────────┐        OCPP 1.6/2.0.1        ┌───────────────────────────────────┐
│  Charge point node(s)       │◄──────over WebSocket─────────►│  Central System (CSMS)            │
│  esphome/components/ocpp/   │        (direct, no broker)    │  Python, companion service         │
│  ESP32 + MicroOcpp (client) │                                │  (not yet implemented)             │
│  relay / meter / connector  │◄────ESPHome Native API─────────┤  aioesphomeapi (client role)       │
└─────────────────────────────┘   (direct, no broker,          └───────────────────┬────────────────┘
                                    only for non-OCPP ancillary                     │
┌─────────────────────────────┐    ESPHome nodes, e.g. a                           │ MQTT discovery
│  Ancillary ESPHome node(s)  │◄───whole-home energy meter)                        │ (outbound only)
│  e.g. whole-home energy     │                                                    ▼
│  meter, unrelated to OCPP   │                                          ┌───────────────────┐
└─────────────────────────────┘                                          │  Home Assistant /   │
                                                                          │  MQTT broker         │
                                                                          └───────────────────┘
```

## Components

### 1. Charge point client — `esphome/components/ocpp/` (this repo, ESP32/ESPHome)

- **Role**: OCPP 1.6/2.0.1 *charge point* (client), one per physical charger/connector set.
- **Runtime**: ESP32, ESP-IDF framework (`dev.yaml:7`), ESPHome `Component` lifecycle.
- **Protocol backend**: [MicroOcpp](https://github.com/matth-x/MicroOcpp) — see `llm-wiki/decisions.md`
  for why it was chosen and the ESP-IDF integration details (platform binding, filesystem, ArduinoJson
  version skew).
- **Hardware I/O**: bridges MicroOcpp's callback API to ordinary ESPHome `switch_`/`sensor::Sensor`/
  `binary_sensor` entities the user configures in YAML (contactor relay, energy meter, plug-detect).
- **Talks to**: the Central System, over OCPP-J (WebSocket + JSON-RPC), and nothing else. It has no
  awareness of MQTT, Home Assistant, or the CSMS's implementation language — from its perspective the
  CSMS is just an OCPP backend URL.
- **Gets HA integration "for free"** the normal ESPHome way (its own MQTT discovery / native API), which
  is unaffected by anything below — see `llm-wiki/components/ocpp.md`.

### 2. Central System (CSMS) — companion service, **not yet implemented**

- **Role**: OCPP 1.6/2.0.1 *Central System*, accepting connections from one or more charge point clients
  (this repo's ESPHome nodes, or any other OCPP-J charge point).
- **Runtime target**: a Raspberry Pi-class host (or similar), **not** the ESP32. A full CSMS (multiple
  long-lived connections, persistence, business logic) does not fit a microcontroller's resource
  envelope or ESPHome's programming model — see `llm-wiki/decisions.md` for the evaluation of
  embedding it on ESP32 (rejected) and of the candidate C++ libraries (open-ocpp supports the Central
  System role but needs OpenSSL/libwebsockets/SQLite — no ESP-IDF port exists).
- **Language/stack**: Python, using [`mobilityhouse/ocpp`](https://github.com/mobilityhouse/ocpp) (MIT,
  `pip install ocpp`) for the protocol layer (message framing, ID correlation, JSON schema validation,
  `@on()`-decorated handlers) for both OCPP 1.6 and 2.0.1, including transactions
  (StartTransaction/StopTransaction/MeterValues).
- **Reliability model ("set and forget", no local dashboard)**:
  - Runs on a read-only root filesystem with a RAM (tmpfs/OverlayFS) overlay for everything transient
    (`raspi-config` → *Performance Options* → *Overlay File System*, or DietPi as a turnkey base) —
    eliminates most SD/SSD wear and makes an unplanned power loss safe.
  - The one thing that must survive a reboot — the charge-point/transaction registry — lives in a single
    small dedicated writable partition (flash-friendly filesystem, e.g. F2FS; SQLite in WAL mode with
    reduced fsync frequency), isolated from the read-only OS.
  - Hardware watchdog enabled for unattended recovery from hangs.
  - No built-in web dashboard — considered expendable (debugging-only). Home Assistant's own
    auto-generated dashboard (via MQTT discovery, below) is the primary UI.
- **HA integration**: [`ha-mqtt-discoverable`](https://github.com/unixorn/ha-mqtt-discoverable)
  (Apache-2.0) publishes the CSMS's own entities (per-connector status, active transaction, energy
  delivered, etc.) to the MQTT broker using Home Assistant's documented MQTT discovery protocol
  (`homeassistant/<component>/<object_id>/config`). This is the **only** place MQTT is used in the whole
  system, and only in the outbound direction (CSMS → HA).
- **Talking to ESPHome nodes that are *not* OCPP charge points** (e.g. a whole-home energy meter used for
  smart/dynamic charging decisions): direct, via
  [`aioesphomeapi`](https://github.com/esphome/aioesphomeapi) (the same library Home Assistant core uses
  internally) against the target device's Native API port (6053). No MQTT broker, and no Home Assistant
  instance, sits in this path — it's a direct TCP connection.

## Explicit non-goals

- The Central System does **not** run on the ESP32 and is **not** an ESPHome component. The `server:` /
  `charge_points:` YAML block that briefly appeared in `docs/README.md` during design discussion was
  incorrect and has been removed — that configuration belongs to the separate Python service, not to
  `esphome/components/ocpp/`'s YAML schema.
- The CSMS does not implement the ESPHome Native API as a *server* (i.e. it does not try to make itself
  discoverable by Home Assistant as if it were an ESPHome device) — only as a *client*, to read/control
  other ESPHome nodes.
- No local web dashboard is planned for the CSMS.
- No feature parity requirement with the C++ Central System libraries evaluated (open-ocpp's TLS/PKI/
  ISO15118 depth) — scope is "basic" plus transactions, per `llm-wiki/decisions.md`.

## Where decisions are recorded

`llm-wiki/decisions.md` has the detailed evaluation and reasoning for every choice above (library
comparisons, ESP-IDF integration constraints, why Python for the CSMS, why MQTT is scoped to one
direction only). This file is the map; that file is the log.
