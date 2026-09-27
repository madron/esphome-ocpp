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

### Next steps (not yet implemented)

1. Implement a `MicroOcpp::Connection` over ESP-IDF's `esp_websocket_client` (no Arduino WebSocket
   dependency).
2. Add the `mo` SPIFFS partition (`esp32.add_partition("mo", "data", "spiffs", size)`) and call
   `esp32.require_vfs_dir()` so MicroOcpp's persistence actually works; size the partition against the
   reduced OTA app slots.
3. Decide the logging story: either keep MicroOcpp's raw `esp_log` "MicroOcpp" tag, or route it through
   `mocpp_set_console_out()` into ESPHome's logger for level filtering and web-server log streaming.
4. Extend `esphome/components/ocpp/__init__.py`'s `CONFIG_SCHEMA` with the OCPP backend URL, charge-box
   ID, auth key, and `id:` references to the relay/sensor/binary_sensor entities MicroOcpp needs.
5. Wire `OcppComponent::setup()`/`loop()` (`esphome/components/ocpp/ocpp.cpp`) to initialize MicroOcpp
   and call `mocpp_loop()`.
6. Update `docs/README.md` and add/extend a `docs/` topic file once the YAML schema gains user-facing
   options (this is a user-facing change per the dual-docs rules, unlike the current dependency wiring).
