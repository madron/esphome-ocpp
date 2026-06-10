# ESPHome OCPP Component

Component for controlling OCPP EV chargers from a local node.

The initial target protocol is **OCPP 1.6J**.

## Core Concepts

The configuration is organized around three electrical and OCPP concepts: the
site, chargers, and connectors. Each level has a different responsibility.

- The `site` describes the shared electrical installation. It defines the number
  of available phases, the phase-to-neutral voltage in `V`.
- A `charger` describes one OCPP charge point that connects to this component.
  It is identified by its `charge_point_id`, which must match the identity used
  by the charger in the WebSocket URL. Charger-level configuration is about
  admission, the number of phases, charger-to-site phase mapping, and grouping of
  the physical connectors belonging to that charge point.
- A `connector` describes one OCPP connector on a charger. It defines the OCPP
  connector ID, the connector's physical maximum current in `A`, and optionally
  its sensors, current-limit control, and restart/enable controls.

In short, the site owns shared electrical constraints, chargers represent OCPP
devices, and connectors represent the individually controlled charging outlets.

## Example Configuration

Example for a three-phase installation where the electricity provider
defines a 10 kW contractual limit, allows a maximum imbalance of 6 kW between
phases, and two chargers are configured:

```yaml
ocpp:
  id: ocpp_server

  server:
    port: 9000
    path: /ocpp

  site:
    phases: 3
    voltage: 230

allocation:
    strategy: equal
    min_current: 6

  chargers:
    - id: garage_left
      charge_point_id: GARAGE_LEFT
      max_current: 32
      phases: 3
      phase_mapping: [L1, L2, L3]
      connectors:
        - id: 1
```

### Units of Measure

- Power: Watts (`W`)
- Current: Amperes (`A`)
- Voltage: Volts (`V`)
- Energy: kilowatt-hours (`kWh`)


