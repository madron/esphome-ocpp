# llm-wiki — ESPHome OCPP Component

Agent-readable reference for the ESPHome OCPP external component. Read this file first to navigate the wiki.

## Index

| File | Content |
| --- | --- |
| [architecture.md](architecture.md) | High-level firmware architecture, class hierarchy, data flow, and OCPP protocol lifecycle |
| [components/ocpp_component.md](components/ocpp_component.md) | `OcppComponent` — top-level ESPHome component, `setup()`/`loop()` responsibilities |
| [components/charge_point.md](components/charge_point.md) | `ChargePoint` — OCPP charger abstraction, message handling, OCPP command orchestration |
| [components/connector.md](components/connector.md) | `Connector` — per-outlet state, meter values, session tracking, current control |
| [components/ocpp_message.md](components/ocpp_message.md) | `OcppMessage` hierarchy — version-agnostic message model and `MeterValues` phase inference |
| [components/ocpp_protocol.md](components/ocpp_protocol.md) | `OcppProtocol` — JSON parsing/making, protocol version dispatch |
| [components/ocpp_server.md](components/ocpp_server.md) | `OcppServer` — raw WebSocket server, HTTP handshake, frame encode/decode |
| [codegen.md](codegen.md) | Python codegen (`__init__.py`) — YAML schema, validation rules, `to_code()` mapping |
| [decisions.md](decisions.md) | Log of architectural and design decisions |

## Source Layout

```
esphome/components/ocpp/
  __init__.py       # YAML schema + codegen
  ocpp.h/cpp        # OcppComponent (top-level)
  charge_point.h/cpp # ChargePoint
  connector.h/cpp    # Connector, CurrentLimit
  message.h          # OcppMessage hierarchy + MeterValues
  protocol.h/cpp     # OcppProtocol (parse/make)
  server.h/cpp       # OcppServer (WebSocket)
```

## Test Layout

```
tests/
  test_charge_point.cpp/py
  test_connector.cpp/py
  test_message.cpp/py
  test_protocol.cpp/py
  assertions.cpp
  cpp_test_case.py
```
