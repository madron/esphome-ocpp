# ESPHome OCPP Component

ESPHome component for controlling OCPP EV chargers from a local ESPHome node.

The first goal of this project is not to implement a full charging management
system, but to provide enough OCPP support to let ESPHome:

- accept EV chargers as OCPP charge points,
- authorize charging in private installations,
- monitor connector and transaction state,
- control the charging current,
- split the available site power across multiple chargers.

The initial target protocol is **OCPP 1.6J**.

## Intended Architecture

The ESPHome device acts as a local OCPP central system / CSMS. EV chargers
connect to the ESPHome node using WebSocket and send standard OCPP messages.

The component should keep policy decisions simple and expose enough state and
controls for ESPHome automations or Home Assistant to decide what should happen.

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

  authorization:
    mode: automatic

  site:
    phases: 3
    voltage: 230
    max_power: 10000
    max_phase_imbalance: 6000
    max_current_per_phase: 32

  allocation:
    strategy: equal
    min_current: 6
    update_interval: 10s
    preference: least_charged

  chargers:
    - id: garage_left
      charge_point_id: GARAGE_LEFT
      connectors:
        - id: 1
          phases: 3
          max_current: 16
          current:
            name: Garage Left Current
          power:
            name: Garage Left Power

    - id: garage_right
      charge_point_id: GARAGE_RIGHT
      connectors:
        - id: 1
          phases: 1
          phase: L1
          max_current: 32
          current:
            name: Garage Right Current
          power:
            name: Garage Right Power
```

## Configuration Reference

In the tables below, each option is marked as `(Required)`, `(Optional)`, or
`(Conditionally required)`. Default values are listed in the description.

### Units of Measure

- Power: Watts (`W`)
- Current: Amperes (`A`)
- Voltage: Volts (`V`)
- Energy: kilowatt-hours (`kWh`)

Electricity providers often describe contractual limits in kilowatts. Convert
those values to Watts in the YAML configuration. For example, `10 kW` should be
configured as `10000` because `max_power` is expressed in `W`.

### Top-Level Options

| Option                              | Description |
| ---                                 | --- |
| `id` (Optional)                     | ESPHome ID for this component. Configure it when referenced by automations. |
| `server` (Optional)                 | Local OCPP WebSocket server configuration. Defaults are listed below. |
| `authorization` (Optional)          | Authorization configuration. Defaults are listed below. |
| `site` (Required)                   | Electrical site limits used for load sharing. |
| `allocation` (Optional)             | Strategy used to split available power. Defaults are listed below. |
| `chargers` (Required)               | Configured chargers and their electrical parameters. |

## Server Configuration

The `server` section configures the local OCPP WebSocket endpoint.
If omitted, the component should use the defaults shown below.

```yaml
ocpp:
  server:
    port: 9000
    path: /ocpp
```

### Server Options

| Option            | Description |
| ---               | --- |
| `port` (Optional) | TCP port used by the local WebSocket server. Defaults to `9000`. |
| `path` (Optional) | Base WebSocket path used by chargers. Defaults to `/ocpp`. Must start with `/`. |

Chargers will typically be configured with a backend URL such as:

```text
ws://<esp-ip>:9000/ocpp/<charge_point_id>
```

For example:

```text
ws://192.168.1.50:9000/ocpp/GARAGE_LEFT
```

## Authorization

This component is intended for private installations. RFID cards or user-level
authentication are not required for the first version.

For local power management, charger admission and session authorization are
separate decisions. Unknown chargers should not be accepted by default because
OCPP can identify the charger, but it cannot reliably tell how the charger was
electrically installed, which phases it uses, or which physical current limit
applies.

```yaml
ocpp:
  authorization:
    mode: automatic
```

### Authorization Options

| Option            | Description |
| ---               | --- |
| `mode` (Optional) | Charging-session authorization mode. Defaults to `automatic`.<br>Available values:<br>`automatic` accepts authorization requests from charge points without validating user identity; <br>`disabled` rejects charging authorization. |

## Site Power Model

The `site` section describes the electrical limits that the allocator must
respect.

In many European countries, the electricity provider defines the contractual
limit in kilowatts rather than amps. In that case, configure the site limit as
power in `W` rather than as current in `A`.

### Single-Phase Site

```yaml
ocpp:
  site:
    phases: 1
    voltage: 230
    max_power: 6000
```

The component can convert the available power to current internally:

```text
available_current = max_power / voltage
```

### Three-Phase Site

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    max_power: 10000
    max_phase_imbalance: 6000
    max_current_per_phase: 32
```

### Site Options

| Option                             | Description |
| ---                                | --- |
| `phases` (Required)                | Number of electrical phases at the site.<br>Available values: `1` or `3`. |
| `voltage` (Optional)               | Phase-to-neutral voltage. Defaults to `230`. |
| `max_power` (Required)             | Maximum total site power available for EV charging. |
| `max_phase_imbalance` (Optional)   | Required for three-phase sites when the provider defines an imbalance limit.<br>Defaults to `0`. |
| `max_current_per_phase` (Optional) | Physical current limit per phase, for example `32`.<br>Defaults to no current-specific limit. |

## Dynamic Grid Power Measurements

For real load balancing, the component should be able to account for the current
non-EV site load. This can be provided by existing ESPHome sensors.

Single-phase example:

```yaml
ocpp:
  site:
    phases: 1
    voltage: 230
    max_power: 6000
    grid_power:
      total: grid_power_total
```

Three-phase example with per-phase metering:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    max_power: 10000
    max_phase_imbalance: 6000
    grid_power:
      l1: grid_power_l1
      l2: grid_power_l2
      l3: grid_power_l3
```

The referenced sensors should represent the current grid/site power per phase.
The OCPP component can then compute how much additional power is available for
EV charging.

Three-phase example with total-only metering:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    max_power: 10000
    max_phase_imbalance: 6000
    grid_power:
      total: grid_power_total
```

On a three-phase site, `grid_power.total` can be used when the meter only
reports aggregate site power. In that mode, the component estimates per-phase
site load by dividing the total measured power by `3`. Phase imbalance limits
remain operational, but they are calculated from this estimate instead of real
per-phase measurements.

Per-phase metering is strongly recommended for three-phase installations. With
total-only metering, per-phase current limits and phase imbalance limits cannot
be guaranteed because the component cannot know how the non-EV site load is
actually distributed across `L1`, `L2`, and `L3`. Installers must always protect
all lines with correctly rated protective devices so that, in the worst case, a
circuit breaker or fuse can interrupt an overload.

### Grid Power Options

| Option                        | Description |
| ---                           | --- |
| `grid_power.total` (Optional) | Sensor ID for total grid/site power. For `phases: 1`, this is the direct site load measurement. For `phases: 3`, this can be used as a fallback when only total metering is available; estimated per-phase load is calculated as `total / 3`.<br>Defaults to not configured. |
| `grid_power.l1` (Optional)    | Sensor ID for grid/site power on phase `L1`. Strongly recommended for `phases: 3`.<br>Defaults to none. |
| `grid_power.l2` (Optional)    | Sensor ID for grid/site power on phase `L2`. Strongly recommended for `phases: 3`.<br>Defaults to none. |
| `grid_power.l3` (Optional)    | Sensor ID for grid/site power on phase `L3`. Strongly recommended for `phases: 3`.<br>Defaults to none. |

For accurate three-phase dynamic load balancing, configure all of
`grid_power.l1`, `grid_power.l2`, and `grid_power.l3`. If only
`grid_power.total` is configured on a three-phase site, the allocator uses the
`total / 3` estimate described above. If no `grid_power` sensors are configured,
the allocator can only use the static limits from the `site` section.

## Allocation

The `allocation` section defines how available power is split between active
charging sessions.

```yaml
ocpp:
  allocation:
    strategy: equal
    min_current: 6
    update_interval: 10s
    preference: least_charged
```

If there is not enough available current to keep every active connector at or
above `min_current`, the allocator can keep only a subset of sessions active.
For example, with `16 A` available and `min_current: 6`, only two connectors can
charge at the same time. `preference` decides which sessions keep charging; the
remaining sessions are paused or left waiting. `first_connected` keeps existing
sessions ahead of newer ones, so it behaves like denying new sessions when the
available current is already fully allocated.

`least_charged` requires live OCPP `MeterValues` containing
`Energy.Active.Import.Register` during the transaction. If a charger does not
provide live energy meter values, use a preference that does not depend on
metered session energy, such as `first_connected`, `last_connected`, or
`round_robin`.

### Allocation Options

| Option                       | Description |
| ---                          | --- |
| `strategy` (Optional)        | Power sharing strategy. Defaults to `equal`.<br>Available value for v1: `equal`. |
| `min_current` (Optional)     | Minimum AC charging current per active connector. Defaults to `6`. |
| `update_interval` (Optional) | How often current limits are recalculated and sent. Defaults to `10s`. |
| `preference` (Optional)      | Which sessions are preferred when not all active sessions can receive at least `min_current`. Defaults to `first_connected`.<br>Available values:<br>`first_connected` prefers older sessions and leaves newer sessions waiting when capacity is full; <br>`last_connected` prefers newer sessions; <br>`least_charged` prefers sessions with the lowest delivered `kWh` and requires live OCPP `Energy.Active.Import.Register` meter values; <br>`round_robin` rotates active charging slots over time. |

## Chargers and Connectors

Chargers are identified by their OCPP `charge_point_id`. Connectors are modeled
explicitly, even if most domestic chargers have only one connector.

```yaml
ocpp:
  chargers:
    - id: garage_left
      charge_point_id: GARAGE_LEFT
      connectors:
        - id: 1
          phases: 3
          max_current: 16
          current:
            name: Garage Left Current
          power:
            name: Garage Left Power
```

### Charger Options

| Option                       | Description |
| ---                          | --- |
| `id` (Required)              | ESPHome internal ID for this charger.<br>Example: `garage_left`. |
| `charge_point_id` (Required) | OCPP identity expected in the WebSocket URL. |
| `connectors` (Required)      | List of OCPP connectors. Each item must define at least `id`, `phases`, and `max_current`. |

### Connector Options

| Option                           | Description |
| ---                              | --- |
| `id` (Required)                  | OCPP connector ID. Usually `1` for single-connector chargers. |
| `phases` (Required)              | Number of phases used by this connector. Values: `1` or `3`. |
| `phase` (Conditionally required) | Required for single-phase connectors on three-phase sites.<br>Values: `L1`, `L2`, or `L3`. |
| `phase_mapping` (Optional)       | Three-phase mapping. Defaults to `[L1, L2, L3]`.<br>Example: `[L2, L3, L1]`. |
| `max_current` (Required)         | Physical maximum current in `A`, for example `16` or `32`. |
| `current` (Optional)             | ESPHome sensor that receives this connector's latest OCPP `Current.Import` value from `MeterValues`, in `A`. Defaults to not configured. |
| `power` (Optional)               | ESPHome sensor that receives this connector's latest OCPP `Power.Active.Import` value from `MeterValues`, in `W`. Defaults to not configured. |

### Phase Mapping

For a single-phase connector on a three-phase site:

```yaml
connectors:
  - id: 1
    phases: 1
    phase: L2
    max_current: 32
```

For a three-phase connector with explicit phase order:

```yaml
connectors:
  - id: 1
    phases: 3
    phase_mapping: [L1, L2, L3]
    max_current: 16
```

Explicit phase mapping is useful when installers rotate phases between chargers
to improve load balancing.

## OCPP Messages for the First Version

The first implementation should focus on the minimal OCPP 1.6J messages
needed for local power management.

Charger to ESPHome:

- `BootNotification`
- `Heartbeat`
- `StatusNotification`
- `Authorize`
- `StartTransaction`
- `StopTransaction`
- `MeterValues`

ESPHome to charger:

- `SetChargingProfile`
- `ClearChargingProfile`
- `RemoteStartTransaction`
- `RemoteStopTransaction`
- `ChangeAvailability`
- `Reset`

`SetChargingProfile` is important because current limiting and load sharing are
the primary goals of this component.

## Minimal v1 Goals

The first useful version should be able to:

1. Accept charger WebSocket connections.
2. Accept `BootNotification` and `Heartbeat`.
3. Track connector state from `StatusNotification`.
4. Authorize private charging for configured chargers according to the authorization policy.
5. Track transactions using `StartTransaction` and `StopTransaction`.
6. Read power in `W` and energy in `kWh` from `MeterValues`.
7. Calculate current limits from the configured site constraints.
8. Split available charging power between active connectors.
9. Apply limits using OCPP smart charging profiles.

## Out of Scope for v1

The following features are intentionally not part of the first version:

- RFID/user management
- billing or session cost calculation
- reservations
- firmware updates
- diagnostics upload
- public charging workflows
- OCPP 2.0.1
- TLS/client certificate management
- complex solar surplus optimization
