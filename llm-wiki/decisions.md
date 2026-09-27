# Design Decisions

## OCPP protocol backend (Central System role): `mobilityhouse/ocpp`

- **Status**: Decided; not yet implemented.
- **Decision**: Use [`mobilityhouse/ocpp`](https://github.com/mobilityhouse/ocpp) (PyPI `ocpp`, MIT,
  actively released, `2.0.0` Jan 2025) as the OCPP protocol layer for the Central System (CSMS) role.
- **Why Python, not C++**: The only C++ library found that implements the Central System role
  (`c-jimenez/open-ocpp`, LGPL-2.1) depends on OpenSSL, libwebsockets (server mode), and SQLite, built via
  CMake/pkg-config for a POSIX/desktop-class target — no PlatformIO package, no ESP-IDF port, no evidence
  of anyone running it on a microcontroller (which is moot now anyway, since this project no longer
  targets a microcontroller). All ESP32-appropriate C++ OCPP libraries evaluated (MicroOcpp, OpenOCPP/
  ChargeLab, libocpp/EVerest, tfocpp) are Charge-Point-only by design and don't implement the Central
  System role at all.
- **Why this library specifically**: `mobilityhouse/ocpp` explicitly models both the Charge Point and
  Central System/CSMS roles on `asyncio` + `websockets`, with the protocol layer handled — Call/
  CallResult/CallError framing, unique-ID correlation, JSON schema validation against the official OCPP
  schemas, `@on(Action.x)`-decorated handler routing — for both OCPP 1.6 and 2.0.1. It is explicitly
  "building blocks, not a complete solution": transaction bookkeeping, persistence, and the connection
  registry are still this project's own code, but the RPC/schema layer — the class of edge case that's
  painful to hand-roll — is not.
- **Alternatives rejected for the Central System role**:
  | Library                | Why rejected                                                                 |
  | ----------------------- | ----------------------------------------------------------------------------- |
  | `c-jimenez/open-ocpp`   | Real Central System support, but OpenSSL/libwebsockets/SQLite + CMake, built for POSIX/desktop, not portable to this project's target |
  | `apostoldevel/ocpp-cs`  | C++20 Central System + emulator, but requires PostgreSQL and its own epoll-based framework — a full backend appliance, heavier than needed |
  | `opencpo/opencpo-core`  | Python CSMS, but requires PostgreSQL + Redis + TimescaleDB — infrastructure well beyond "a Raspberry Pi, set and forget" |
- **Scope for the first implementation**: connection handling + status (BootNotification, Heartbeat,
  StatusNotification, Authorize) **plus transactions** (StartTransaction, StopTransaction, MeterValues).
  No TLS and no persistence-of-everything in the first pass.

## MQTT: a real, bidirectional integration bus — not just an HA discovery side effect

- **Decision**: the CSMS maintains a real MQTT client (pub/sub, via `paho-mqtt`, which is also
  `ha-mqtt-discoverable`'s own transport dependency) as a general integration bus: it **publishes** state
  (per-connector status, active transaction, energy delivered) to its own topics, and **subscribes** to
  command topics so it can be controlled by other MQTT clients/automations — not only through OCPP itself.
  This is useful independently of Home Assistant: any MQTT-capable subscriber can consume state or issue
  commands.
- **Decision**: layer [`ha-mqtt-discoverable`](https://github.com/unixorn/ha-mqtt-discoverable)
  (Apache-2.0, actively released through Nov 2025, `v0.25.2`) on top of that real MQTT client to
  additionally publish Home Assistant's documented discovery payloads
  (`homeassistant/<component>/[<node_id>/]<object_id>/config`), so HA auto-creates entities pointing at
  the same state/command topics. This is a convenience layer for HA auto-discovery, **not** a substitute
  for general MQTT support — without a real, working MQTT connection underneath it, `ha-mqtt-discoverable`
  has no topics to attach discovery payloads to and is useless on its own.
- **Why this doesn't require ESPHome or Home Assistant Core**: HA's MQTT discovery is an open, documented
  protocol — ESPHome is just one of ~20 listed implementers, not a prerequisite for it. Any service
  publishing correctly shaped discovery payloads to the broker is picked up identically, and the
  underlying state/command topics remain usable by non-HA subscribers regardless.
- **Home Assistant's native (non-MQTT) API is explicitly out of scope** for the CSMS's own identity: no
  requirement to make the CSMS discoverable via that protocol, and no dependency on HA Core being present
  at all — the MQTT bus and its HA discovery layer both work with just a broker.

## Direct communication with ancillary ESPHome devices: `aioesphomeapi`, not MQTT

- **Decision**: for any ESPHome device the CSMS needs to read/control that is *not* itself an OCPP charge
  point (e.g. a whole-home energy meter feeding smart/dynamic charging decisions), use
  [`aioesphomeapi`](https://github.com/esphome/aioesphomeapi) (`esphome` org, actively maintained, v46.x)
  against that device's Native API port (6053) directly.
- This is the same library Home Assistant core uses internally, but works fully standalone — no HA
  instance or MQTT broker in the path, a direct peer-to-peer TCP connection, matching ESPHome's own
  description of the Native API as built "to communicate with clients directly."
- Combined with OCPP-over-WebSocket to charge points, **no MQTT broker sits between the CSMS and any
  ESPHome device in either direction** — the ESPHome Native API and the CSMS's own MQTT bus (above) are
  separate links used for separate purposes, not layered on top of each other.

## Deployment/reliability: Raspberry Pi-class host, "set and forget"

- **Decision**: run the CSMS on a Raspberry Pi (or similar SBC) with a **read-only root filesystem and a
  RAM/OverlayFS overlay** for everything transient (`raspi-config` → *Performance Options* → *Overlay File
  System*, or DietPi as a turnkey base with `log2ram`-style defaults) — a well-documented, off-the-shelf
  technique, not custom engineering.
- **Rationale**: this removes the *unplanned* writes (OS churn, logs, journaling) that actually kill SD
  cards and corrupt filesystems on power loss — the same "flash and forget" reliability bar an ESPHome
  device meets, applied to a Linux host. The CSMS's own actual write volume (BootNotification, Heartbeat,
  StatusNotification, StartTransaction/StopTransaction, periodic MeterValues) is small (KB/day for a
  handful of chargers), so isolating it to one small dedicated writable partition (flash-friendly
  filesystem, e.g. F2FS; SQLite in WAL mode with reduced fsync frequency) is enough to make wear a
  non-issue regardless of whether that partition sits on a microSD, an industrial/high-endurance SD card,
  or a small USB SSD.
- A hardware watchdog should be enabled for unattended recovery from hangs.
- **No local web dashboard** is planned. Home Assistant's own auto-generated dashboard (via the MQTT
  discovery entities above) is the primary UI.

## Next steps (not yet implemented)

1. Pick the persistence schema for the charger/connector/transaction registry (SQLite, WAL mode) and the
   dedicated-partition deployment story on the target Pi.
2. Implement the `mobilityhouse/ocpp` CSMS: connection handling, `@on()` handlers for BootNotification/
   Heartbeat/StatusNotification/Authorize/StartTransaction/StopTransaction/MeterValues.
3. Implement the `ha-mqtt-discoverable` publisher for the CSMS's own entities.
4. Implement the `aioesphomeapi` client for ancillary ESPHome devices, once a concrete use case (e.g. a
   specific energy meter) is identified.
5. Write the Raspberry Pi deployment guide (`docs/`) once the service itself exists — read-only root
   setup, dedicated writable partition, watchdog.
6. Update `docs/README.md` with the actual configuration reference once the CSMS has one — this is a
   user-facing change per the dual-docs rules.
