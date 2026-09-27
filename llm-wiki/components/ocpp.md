# `ocpp` component

Custom ESPHome component: `esphome/components/ocpp/`.

- `ocpp.h` — `OcppComponent : public Component`, declares `setup()`/`loop()`/`dump_config()`
  (`esphome/components/ocpp/ocpp.h:11`).
- `ocpp.cpp` — currently empty lifecycle methods. Includes `<MicroOcpp.h>` for build verification only;
  no MicroOcpp API is called yet (`esphome/components/ocpp/ocpp.cpp:9`).
- `__init__.py` — YAML schema currently only accepts `id:` (`cv.GenerateID()`). Registers the component
  and wires the MicroOcpp build dependency (library, ESP-IDF platform flag, `spiffs` IDF component) —
  see `esphome/components/ocpp/__init__.py`.

## Protocol backend

Uses [MicroOcpp](https://github.com/matth-x/MicroOcpp) as the OCPP client implementation. See
`llm-wiki/decisions.md` for why it was chosen, the ESP-IDF-specific build wiring it requires, and the
integration steps still outstanding (WebSocket `Connection`, config schema, callback wiring).

## Build verification

`./esphome-wrapper compile` (against `dev.yaml`) is the way to confirm the component still compiles
against the current ESP32 ESP-IDF framework/board. This wrapper decrypts local SOPS secrets, so run it
directly rather than invoking `esphome` from the venv.
