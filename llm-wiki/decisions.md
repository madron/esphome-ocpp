# Design Decisions

## OCPP protocol backend: MicroOcpp

- **Status**: Adopted. Build dependency wired up; not yet called from any code.
- **Decision**: Use [matth-x/MicroOcpp](https://github.com/matth-x/MicroOcpp) (PlatformIO package
  `matth-x/MicroOcpp`, pinned `1.2.0`) as the OCPP 1.6/2.0.1 protocol implementation behind
  `esphome/components/ocpp/`, instead of writing an OCPP-J client from scratch.
- **Why**: MIT-licensed, portable C/C++, native ESP32 support for both Arduino and ESP-IDF, small
  footprint (~121 KB flash / ~12-22 KB heap at `-Os`), and a non-blocking `loop()` API that maps
  directly onto ESPHome's `Component::loop()` (`esphome/components/ocpp/ocpp.h:14`). Hardware I/O is
  wired through callback setters (`setConnectorPluggedInput`, `setEnergyMeterInput`,
  `onTransactionStart`/`onTransactionStop`, ...), which lets `OcppComponent` bridge to ordinary
  ESPHome `switch_`/`sensor::Sensor`/`binary_sensor` entities instead of duplicating hardware logic.
- **Alternatives considered**:
  | Library                | Why rejected                                                                 |
  | ----------------------- | ----------------------------------------------------------------------------- |
  | tfocpp (Tinkerforge)     | No clear OSS license in the repo; narrower profile coverage (Core + Smart Charging only) |
  | OpenOCPP (ChargeLab)     | Apache-2.0 and ESP-IDF-native, kept as a fallback candidate, but less mature/smaller community than MicroOcpp |
  | open-ocpp (c-jimenez)    | LGPL-3.0; depends on OpenSSL, libwebsockets, SQLite — too heavy for a microcontroller image |
  | libocpp (EVerest)        | Targets a Linux/EVerest gateway deployment (Boost, SQLite); not designed for bare-metal ESP32 |
  | ArduinoOcpp forks        | Predecessor/forks of MicroOcpp itself, stale, superseded by upstream MicroOcpp |

### Integration notes (ESP-IDF specifics)

This project builds ESP32 with `framework: type: esp-idf` (`dev.yaml:7`), not Arduino, which requires
non-default wiring for MicroOcpp:

- **Platform binding**: MicroOcpp's `MicroOcpp/Platform.h` defaults to
  `MO_PLATFORM_ARDUINO` (pulls in `Arduino.h`). The ESP-IDF-native binding must be selected explicitly
  with the build flag `-DMO_PLATFORM=MO_PLATFORM_ESPIDF`, set in
  `esphome/components/ocpp/__init__.py:33`. Side effect: that flag also derives
  `MO_USE_FILEAPI = ESPIDF_SPIFFS` and `MO_FILENAME_PREFIX = "/mo_store/"`
  (`MicroOcpp/Core/FilesystemAdapter.h`), so it commits the project to SPIFFS-backed persistence, and
  it routes MicroOcpp's console output to `esp_log_write(..., "MicroOcpp", ...)` instead of ESPHome's
  logger.
- **Filesystem**: MicroOcpp's `EspIdfFilesystemAdapter`
  (`MicroOcpp/Core/FilesystemAdapter.cpp`) calls `esp_vfs_spiffs_register()` with
  `partition_label = MO_PARTITION_LABEL` (default `"mo"`). Two ESPHome defaults work against this, so
  enabling the IDF `spiffs` component is **necessary but not sufficient**:
  1. **Partition**: ESPHome's ESP-IDF partition table is `otadata, phy_init, app0, app1, nvs` only
     (`esphome/components/esp32/__init__.py:2462`); there is no SPIFFS partition, so the mount returns
     `ESP_ERR_NOT_FOUND` ("Failed to find SPIFFS partition") and MicroOcpp falls back to **no
     persistence**. A partition labelled `mo` must be added via `esp32.add_partition()` or a custom
     `partitions:` CSV — note this shrinks *both* OTA app slots (`_get_app_partition_size()` splits the
     remaining flash in two).
  2. **Directory support**: ESPHome defaults `disable_vfs_support_dir: true`
     (`esphome/components/esp32/__init__.py:1394`), i.e. `CONFIG_VFS_SUPPORT_DIR=n`, which makes
     `opendir`/`readdir`/`closedir` newlib stubs that always fail — visible as linker warnings in the
     current build. MicroOcpp implements `ftw_root()` with those calls and uses it on the default path:
     `Model/ConnectorBase/Connector.cpp:90` (OCPP 1.6 transaction journal discovery),
     `Model/Transactions/TransactionStore.cpp:664,723`, `Core/FilesystemUtils.cpp:119`
     (`remove_if`, used by `Operations/ClearCache.cpp:28` and `Model/Boot/BootService.cpp:230,251`), and
     `Model/Diagnostics/DiagnosticsService.cpp:371`. Without `esp32.require_vfs_dir()`, file
     open/stat/unlink still work, so the failure is **silent degradation**, not a hard error: stored
     transactions are not recovered after reboot, ClearCache cannot purge cache files, and GetLog cannot
     enumerate stored diagnostics.
  - Status: neither prerequisite is implemented yet; see Next steps below. `ocpp.cpp` currently only
    includes the header, so nothing mounts at runtime.
- **Transitive dependencies**: PlatformIO's library manager resolves MicroOcpp's declared dependencies
  automatically: `bblanchon/ArduinoJson@6.20.1` and `links2004/WebSockets@2.4.1`. The WebSockets library
  is only needed by MicroOcpp's optional Arduino connection helper and is not used under
  `MO_PLATFORM_ESPIDF`; a project-specific `Connection` implementation over ESP-IDF's native
  `esp_websocket_client` will be required before the library is actually used (see Next steps below).
- **ArduinoJson version skew (known hazard)**: ESPHome pins `bblanchon/arduinojson` **7.4.2** as an IDF
  managed component (`esphome/components/json/__init__.py:18`), while MicroOcpp 1.2.0 targets
  ArduinoJson **6.20.1**. Both end up in the build. Observed in this project's compile log:
  MicroOcpp's `Core/Memory.h:418` (`using JsonDoc = DynamicJsonDocument;`) compiles only through
  ArduinoJson 7's deprecation shim (`ArduinoJson/compatibility.hpp:125`), producing
  `-Wdeprecated-declarations` warnings. Consequences: the v6 capacity concept (`MO_MAX_JSON_CAPACITY`
  4096) is ignored by v7's elastic `JsonDocument`, so allocation behaviour differs from what MicroOcpp
  benchmarked; a future ArduinoJson 8 (which drops the shim) or an ESPHome version bump will break this
  build; and MicroOcpp's public API exposes ArduinoJson's `JsonObject` in callbacks
  (`setOnReceiveRequest`, `setRequestHandler`, `setOnSendConf` in `MicroOcpp.h`), so if ESPHome-side and
  library-side translation units ever disagree on the ArduinoJson major version, that becomes an
  ODR/ABI violation rather than a compile error. Keep an eye on this when ESPHome or MicroOcpp is
  upgraded.
- **Verification**: `./esphome-wrapper compile` (with `dev.yaml`) builds clean with these flags —
  `esphome/components/ocpp/ocpp.cpp:9` includes `<MicroOcpp.h>` but calls no MicroOcpp API yet.

### Superseding the SPIFFS path: custom `Connection`/`FilesystemAdapter` instead

The SPIFFS partition + `require_vfs_dir()` prerequisites above are **not** the recommended path. MicroOcpp
exposes both platform bindings as clean, documented extension points meant to be replaced per-project,
without editing a single line of the library:

- `MicroOcpp::Connection` (`MicroOcpp/Core/Connection.h`) is a 5-method abstract class
  (`loop`, `sendTXT`, `setReceiveTXTcallback`, `getLastConnected`, `isConnected`). The maintainer's own
  guidance (matth-x/MicroOcpp#219, #322) is to subclass it for any transport (Ethernet, GSM/AT-commands,
  the separate `matth-x/MicroOcppMongoose` adapter) — writing one over ESP-IDF's native
  `esp_websocket_client` was already planned (see below) and needs no other change.
- `MicroOcpp::FilesystemAdapter` (`MicroOcpp/Core/FilesystemAdapter.h`) is a 4-method abstract class
  (`stat`, `open`, `remove`, `ftw_root`). `mocpp_initialize(connection, ..., filesystem, ...)` takes a
  `std::shared_ptr<FilesystemAdapter>` directly — `makeDefaultFilesystemAdapter()` is only the *default
  argument*, not a requirement. Passing a custom adapter instance bypasses SPIFFS entirely.
- Setting the build flag `-DMO_USE_FILEAPI=DISABLE_FS` (`MicroOcpp/Core/FilesystemAdapter.h`) removes the
  SPIFFS/LittleFS/POSIX branches from `FilesystemAdapter.cpp`'s compilation altogether (it compiles down
  to a one-line stub), so `esp_spiffs.h`, the `mo` partition, and `esp32.require_vfs_dir()` are **not
  needed at all** — `include_builtin_idf_component("spiffs")` in `__init__.py` can be dropped once a
  custom adapter is in place.
- A custom adapter can implement `ftw_root()` by walking its own manifest of known keys/filenames instead
  of calling `opendir`/`readdir` — sidestepping ESPHome's `CONFIG_VFS_SUPPORT_DIR=n` default rather than
  fighting it. Backing store is a free choice: raw `esp_partition_read/write/erase` (no extra IDF
  component, smallest footprint) is the most "ESPHome-standard" option, since ESPHome itself never uses a
  filesystem abstraction (its own `preferences:` component talks to flash/NVS directly).
- Console/timer/RNG are independently swappable the same way, without needing `MO_PLATFORM_ESPIDF` at
  all: `MO_CUSTOM_CONSOLE` + `mocpp_set_console_out()` can route MicroOcpp's logs through ESPHome's
  `ESP_LOGx`, and `MO_CUSTOM_TIMER`/`MO_CUSTOM_RNG` can route the clock/RNG through ESPHome's own
  `millis()`/`random_uint32()` (`MicroOcpp/Platform.h`) — again with zero changes to MicroOcpp itself.
- Scope of new code: two small adapter classes (`Connection` over `esp_websocket_client`,
  `FilesystemAdapter` over `esp_partition_*` or similar), roughly 300-600 LOC combined, replacing nothing
  in the upstream library. Everything else — all ~35 `Operations/*` handlers, the entire `Model/*` layer
  (Authorization, Availability, Boot, Certificates, Diagnostics, FirmwareManagement, Heartbeat, Metering,
  RemoteControl, Reservation, Reset, SmartCharging, Transactions, Variables), and `Core/*`'s
  protocol/request/configuration machinery — is used unmodified from upstream. That's roughly 23,000 of
  MicroOcpp's ~24,000 source lines (>95%) reused as-is; this is an adapter swap, not a rewrite.
- Not addressed by this: the ArduinoJson 6.20.1-vs-7.4.2 skew above is orthogonal to the
  Connection/FilesystemAdapter choice (it comes from `<ArduinoJson.h>` used throughout the library) and
  remains an open item regardless of which platform bindings are used.

### Next steps (not yet implemented)

1. Implement a `MicroOcpp::Connection` over ESP-IDF's `esp_websocket_client` (no Arduino WebSocket
   dependency).
2. Implement a custom `MicroOcpp::FilesystemAdapter` (e.g. over `esp_partition_*`) and pass it to
   `mocpp_initialize()` explicitly; set `-DMO_USE_FILEAPI=DISABLE_FS` and drop
   `include_builtin_idf_component("spiffs")` — supersedes the SPIFFS/partition/`require_vfs_dir()` path
   above.
3. Decide the logging story: either keep MicroOcpp's raw `esp_log` "MicroOcpp" tag, or route it through
   `mocpp_set_console_out()` into ESPHome's logger for level filtering and web-server log streaming.
4. Extend `esphome/components/ocpp/__init__.py`'s `CONFIG_SCHEMA` with the OCPP backend URL, charge-box
   ID, auth key, and `id:` references to the relay/sensor/binary_sensor entities MicroOcpp needs.
5. Wire `OcppComponent::setup()`/`loop()` (`esphome/components/ocpp/ocpp.cpp`) to initialize MicroOcpp
   and call `mocpp_loop()`.
6. Update `docs/README.md` and add/extend a `docs/` topic file once the YAML schema gains user-facing
   options (this is a user-facing change per the dual-docs rules, unlike the current dependency wiring).

## Central System (CSMS): separate Python service, not an ESP32/ESPHome component

- **Status**: Design decided; not yet implemented. See `llm-wiki/architecture.md` for the system-level
  picture this fits into.
- **Decision**: The OCPP Central System role is **not** built into `esphome/components/ocpp/` or run on
  the ESP32. It is a separate companion service, in Python, intended for a Raspberry Pi-class host.
- **Why not on ESP32 / not in C++**: The only C++ library found that implements the Central System role
  (`c-jimenez/open-ocpp`, LGPL-2.1) depends on OpenSSL, libwebsockets (server mode), and SQLite, built via
  CMake/pkg-config for a POSIX/desktop-class target — no PlatformIO package, no ESP-IDF port, no evidence
  of anyone running it on a microcontroller. Porting that dependency stack to ESP-IDF would be a larger
  and less certain effort than the hand-rolled OCPP-J server the user had already found painful to write
  from scratch. All ESP32-appropriate C++ OCPP libraries evaluated (MicroOcpp, OpenOCPP/ChargeLab,
  libocpp/EVerest, tfocpp) are Charge-Point-only by design.
- **Why Python**: [`mobilityhouse/ocpp`](https://github.com/mobilityhouse/ocpp) (PyPI `ocpp`, MIT,
  actively released, `2.0.0` Jan 2025) explicitly models both the Charge Point and Central System/CSMS
  roles on `asyncio` + `websockets`, with the protocol layer handled (Call/CallResult/CallError framing,
  unique-ID correlation, JSON schema validation against the official OCPP schemas, `@on(Action.x)`
  handler routing) for OCPP 1.6 and 2.0.1 — closing the "lots of edge cases" gap from hand-rolled OCPP-J
  without open-ocpp's non-portable dependency stack. It is explicitly "building blocks, not a complete
  solution" — transaction bookkeeping, persistence, and the connection registry are still the CSMS's own
  code, but the RPC/schema layer is not.
- **Scope for the first implementation**: connection handling + status (BootNotification, Heartbeat,
  StatusNotification, Authorize) **plus transactions** (StartTransaction, StopTransaction, MeterValues).
  No persistence-of-everything, no TLS, in the first pass.

### Home Assistant integration: MQTT discovery outbound only, no ESPHome dependency

- Losing ESPHome's automatic MQTT/HA integration by moving the CSMS off ESPHome is not a real gap: HA's
  MQTT discovery (`homeassistant/<component>/[<node_id>/]<object_id>/config`) is an open, documented
  protocol that ESPHome is just one of ~20 listed implementers of, not a prerequisite for it.
- **Decision**: use [`ha-mqtt-discoverable`](https://github.com/unixorn/ha-mqtt-discoverable)
  (Apache-2.0, actively released through Nov 2025, `v0.25.2`) to publish the CSMS's own entities
  (per-connector status, active transaction, energy delivered) to HA. This is the **only** direction MQTT
  is used in the whole system — CSMS → HA, outbound. It is not used between the CSMS and any ESPHome
  device.
- **Home Assistant's native (non-MQTT) API is explicitly out of scope** for the CSMS: no requirement to
  make the CSMS discoverable via that protocol, and no dependency on Home Assistant Core being present at
  all — MQTT discovery works with just a broker.

### Direct CSMS ↔ ESPHome communication: `aioesphomeapi`, not MQTT

- **Decision**: for any ESPHome node the CSMS needs to talk to that is *not* itself an OCPP charge point
  (e.g. a whole-home energy meter used for smart/dynamic charging decisions), use
  [`aioesphomeapi`](https://github.com/esphome/aioesphomeapi) (`esphome` org, actively maintained, v46.x)
  against that device's Native API port (6053) directly. This is the same library Home Assistant core
  uses internally, but works standalone with no HA instance or MQTT broker in the path — a direct,
  peer-to-peer TCP connection, matching ESPHome's own description of the Native API as built "to
  communicate with clients directly."
- Combined with the OCPP-over-WebSocket link to charge-point nodes, **no MQTT broker sits between the
  CSMS and any ESPHome device in either direction** — MQTT's only remaining role is the CSMS's outbound
  announcement to Home Assistant, above.

### Deployment/reliability: Raspberry Pi-class host, "set and forget"

- **Decision**: run the CSMS on a Raspberry Pi (or similar SBC) with a **read-only root filesystem and a
  RAM/OverlayFS overlay** for everything transient (`raspi-config` → *Performance Options* → *Overlay File
  System*, or DietPi as a turnkey base with `log2ram`-style defaults) — this is a well-documented,
  off-the-shelf technique, not custom engineering.
- **Rationale**: this removes the *unplanned* writes (OS churn, logs, journaling) that actually kill SD
  cards and corrupt filesystems on power loss — closer to ESPHome's own "flash and forget" reliability
  model than a stock Raspberry Pi OS install. The CSMS's own actual write volume (BootNotification,
  Heartbeat, StatusNotification, StartTransaction/StopTransaction, periodic MeterValues) is small (KB/day
  for a handful of chargers), so isolating it to one small dedicated writable partition (flash-friendly
  filesystem, e.g. F2FS; SQLite in WAL mode with reduced fsync frequency) is enough to make wear a
  non-issue regardless of whether that partition sits on a microSD, an industrial/high-endurance SD card,
  or a small USB SSD.
- A hardware watchdog should be enabled for unattended recovery from hangs, to match the "no moving
  parts, no maintenance" bar ESPHome nodes already meet.
- **No local web dashboard** is planned for the CSMS — it was used mostly for debugging on ESPHome nodes
  and is considered expendable here; Home Assistant's own auto-generated dashboard (via the MQTT
  discovery entities above) is the primary UI.

### Correction: `docs/README.md`'s `server:`/`charge_points:` block was wrong

An earlier design pass added a `server:`/`charge_points:` block to `docs/README.md`'s example
`ocpp:` YAML, implying the Central System would be configured as part of the ESPHome component. That
contradicts every decision above (the CSMS is a separate Python service, not ESPHome YAML) and has been
removed from `docs/README.md`. The charge-point registry concept it was gesturing at belongs to the
CSMS's own (not yet implemented) configuration, not to `esphome/components/ocpp/`'s `CONFIG_SCHEMA`.
