# ESPHome OCPP Component

Component for controlling OCPP EV chargers from a local node.

The first goal of this project is not to implement a full charging management
system, but to provide enough OCPP support to:

- accept EV chargers as OCPP charge points,
- authorize charging in private installations,
- monitor connector and transaction state,
- control the charging current,
- split the available site power across multiple chargers.

The initial target protocol is **OCPP 1.6J**.

## Intended Architecture

The device acts as a local OCPP central system / CSMS. EV chargers connect to it
using WebSocket and send standard OCPP messages.

The component should primarily be able to work standalone without external
automation. It should still expose enough state and controls for optional
external automations, such as Home Assistant, or external dynamic
reconfiguration.

## Core Concepts

The configuration is organized around three electrical and OCPP concepts: the
site, chargers, and connectors. Each level has a different responsibility.

- The `site` describes the shared electrical installation. It defines the number
  of available phases, the phase-to-neutral voltage in `V`, optional grid limits,
  and optional grid power measurements. Site limits apply to all configured EV
  charging and are used by the allocator to decide how much current is available.
  Site current headroom and EV current draw can be exposed as scalar or per-phase
  `headroom_current` and `drawn_current` sensors.
- A `charger` describes one OCPP charge point that connects to this component.
  It is identified by its `charge_point_id`, which must match the identity used
  by the charger in the WebSocket URL. Charger-level configuration is about
  admission, the number of phases, charger-to-site phase mapping, and grouping of
  the physical connectors belonging to that charge point. Charger current draw
  can optionally be measured with `drawn_current_source` and exposed with a
  scalar `drawn_current` sensor.
- A `connector` describes one OCPP connector on a charger. It defines the OCPP
  connector ID, the connector's physical maximum current in `A`, and optionally
  its sensors, current-limit control, and restart/enable controls. Allocation
  decisions ultimately assign current to connectors.
  Connector current state can be exposed as `available_current`,
  `allocated_current`, and scalar or per-phase `drawn_current` sensors.

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

  authorization:
    mode: automatic

  site:
    phases: 3
    voltage: 230
    headroom_current:
      l1:
        name: Site Headroom Current L1
      l2:
        name: Site Headroom Current L2
      l3:
        name: Site Headroom Current L3
    drawn_current:
      name: Site EV Drawn Current
      l1:
        name: Site EV Drawn Current L1
      l2:
        name: Site EV Drawn Current L2
      l3:
        name: Site EV Drawn Current L3
    grid:
      max_power: 10000
      max_phase_imbalance: 6000
      max_current: 32
      power:
        l1: grid_power_l1
        l2: grid_power_l2
        l3: grid_power_l3

  allocation:
    strategy: equal
    min_current: 6
    update_interval: 10s
    preference: first_connected

  chargers:
    - id: garage_left
      charge_point_id: GARAGE_LEFT
      max_current: 32
      phases: 3
      phase_mapping: [L1, L2, L3]
      drawn_current_source:
        l1: garage_left_current_l1
        l2: garage_left_current_l2
        l3: garage_left_current_l3
      drawn_current:
        name: Garage Left Charger Drawn Current
      connectors:
        - id: 1
          max_current: 16
          available_current:
            name: Garage Left Available Current
          allocated_current:
            name: Garage Left Allocated Current
          drawn_current:
            l1:
              name: Garage Left Drawn Current L1
            l2:
              name: Garage Left Drawn Current L2
            l3:
              name: Garage Left Drawn Current L3
          power:
            name: Garage Left Power
          state:
            name: Garage Left State
          enabled:
            name: Garage Left Enabled
          current_limit:
            name: Garage Left Current Limit
            initial_value: 16
            restore_value: true
          restart:
            name: Garage Left Restart Session

    - id: garage_right
      charge_point_id: GARAGE_RIGHT
      max_current: 32
      phases: 1
      phase_mapping: [L2]
      drawn_current_source: garage_right_current
      drawn_current:
        name: Garage Right Charger Drawn Current
      connectors:
        - id: 1
          available_current:
            name: Garage Right Available Current
          allocated_current:
            name: Garage Right Allocated Current
          drawn_current:
            name: Garage Right Drawn Current
          power:
            name: Garage Right Power
          enabled:
            name: Garage Right Enabled
          current_limit:
            name: Garage Right Current Limit
            initial_value: 32
            restore_value: true
          restart:
            name: Garage Right Restart Session
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
configured as `10000` because `site.grid.max_power` is expressed in `W`.

### Top-Level Options

| Option                              | Description |
| ---                                 | --- |
| `id` (Optional)                     | Component ID. Configure it when referenced by automations. |
| `server` (Optional)                 | Local OCPP WebSocket server configuration. Defaults are listed below. |
| `authorization` (Optional)          | Authorization configuration. Defaults are listed below. |
| `site` (Required)                   | Electrical site topology and optional power sources used for load sharing and protection. |
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
ws://<esp-ip>:9000/ocpp
```

For example:

```text
ws://192.168.1.50:9000/ocpp
```

Some chargers append their own charge point identity to the configured backend URL.
In that case, the effective WebSocket URL becomes:

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

The required `site` section describes the local electrical installation.
`phases` and `voltage` are site-specific and required, even for sites that are
not connected to the grid. Optional power-source subsections, such as `grid`,
describe supply limits that the allocator must respect.

In many European countries, the electricity provider defines the contractual
limit in kilowatts rather than amps. In that case, configure the grid limit as
power in `W` rather than as current in `A`.

### Single-Phase Site

```yaml
ocpp:
  site:
    phases: 1
    voltage: 230
    grid:
      max_power: 6000
      max_current: 32
```

`grid.max_power` is the maximum total power that may be drawn from the grid, not
the power reserved for EV charging. If no dynamic grid power measurement is
configured, the component can only calculate static current headroom from that
grid limit:

```text
static_current_headroom = grid.max_power / voltage
```

### Three-Phase Site

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    grid:
      max_power: 10000
      max_phase_imbalance: 6000
      max_current: 32
```

### Site Options

| Option                         | Description |
| ---                            | --- |
| `phases` (Required)            | Number of electrical phases at the site.<br>Available values: `1` or `3`. |
| `voltage` (Required)           | Phase-to-neutral voltage in `V`, for example `230`. |
| `policy` (Optional)            | Site energy policy used by allocation. Defaults to `normal`.<br>Available values:<br>`normal` uses all grid headroom available under configured limits; <br>`solar` only increases charging from exported solar surplus and can reduce charging when the site is no longer exporting enough power. |
| `solar` (Optional)             | Solar-policy tuning options. Defaults to an internal export margin of `300` W and no user-facing number entity. |
| `storage` (Optional)           | Storage/battery measurements used by solar policy and future policies. Defaults to not configured. |
| `headroom_current` (Optional)  | Sensor or sensor group that receives total site current headroom in `A`. In `normal` policy this mirrors `grid.headroom_current`; in `solar` policy it represents solar-surplus headroom after the export margin and storage discharge correction. Configure the same scalar or per-phase shape as `drawn_current`. Defaults to not configured. |
| `drawn_current` (Optional)     | Sensor or sensor group that receives the total EV current drawn at the site in `A`. Configure `drawn_current.name` for the scalar maximum phase current, any of `drawn_current.l1`, `drawn_current.l2`, and `drawn_current.l3` for per-phase sensors, or both scalar and per-phase sensors in the same block. Defaults to not configured. |
| `grid` (Optional)              | Grid connection limits and measurements. Defaults to not configured, for example on sites that are not connected to the grid. |

### Site Headroom Current

The optional site `headroom_current` sensors expose site-level current headroom in
`A`, using the same scalar and per-phase shape as `drawn_current`. In `normal`
policy this is equal to `site.grid.headroom_current`. In `solar` policy it is the
policy-limited solar-surplus headroom and may become negative when charging should
be reduced to restore the configured export margin or stop storage discharge.

Example for per-phase site headroom-current sensors:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    headroom_current:
      l1:
        name: Site Headroom Current L1
      l2:
        name: Site Headroom Current L2
      l3:
        name: Site Headroom Current L3
    grid:
      max_power: 10000
      max_current: 32
```

For a single-phase site, configure the scalar form or only
`headroom_current.l1`. The scalar form publishes the highest site phase value;
with a single-phase site this is the same as `L1`.

### Site Drawn Current

The optional site `drawn_current` sensors expose the total EV charging current in
`A` for the whole site. The value is calculated by summing all charger
`drawn_current` values and applying each charger's `phase_mapping`, so the
per-phase values are reported in physical site phase order.

There is no `drawn_current_source` at the site level. Configure charger-level
`drawn_current_source` sensors when real measurements are available, or rely on
connector `drawn_current` values when the charger reports current through OCPP.

Example for a scalar site drawn-current sensor:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    drawn_current:
      name: Site EV Drawn Current
```

Example for per-phase site drawn-current sensors:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    drawn_current:
      l1:
        name: Site EV Drawn Current L1
      l2:
        name: Site EV Drawn Current L2
      l3:
        name: Site EV Drawn Current L3
```

For a single-phase site, configure the scalar form or only `drawn_current.l1`.
The scalar form publishes the highest site phase current; with a single-phase
site this is the same as `L1`.

The scalar and per-phase sensors can also be configured together:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    drawn_current:
      name: Site EV Drawn Current
      l1:
        name: Site EV Drawn Current L1
      l2:
        name: Site EV Drawn Current L2
      l3:
        name: Site EV Drawn Current L3
```

For simple names, the phase entries may also use the string shorthand:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    drawn_current:
      name: site_drawn_current
      l1: site_drawn_current_l1
      l2: site_drawn_current_l2
      l3: site_drawn_current_l3
```

### Site Energy Policy

The site `policy` controls which energy should be made available to chargers.

- `normal` is the default. It uses the current grid headroom under `site.grid`
  limits, which is the behavior used before solar policy support was added.
- `solar` only uses surplus power that is being exported to the grid. Grid limits
  are still enforced, but allocation is additionally capped by the measured export
  power.

Solar policy requires signed `site.grid.power` measurements. Positive grid power
means import from the grid, and negative grid power means export to the grid. When
the site is not exporting enough, solar policy can produce negative headroom so
the current allocator lowers the charging limit instead of holding the previous
limit.

Example with a user-tunable export margin:

```yaml
ocpp:
  site:
    phases: 1
    voltage: 230
    policy: solar
    solar:
      export_margin_power:
        name: Solar Export Margin Power
        min_value: 0
        max_value: 1000
        step: 50
        initial_value: 300
    grid:
      max_power: 6000
      max_current: 32
      power:
        l1: grid_power_l1
```

`solar.export_margin_power` is a number entity in `W`. It configures how much
export should remain after EV charging. Values around `200` to `500` are useful
for installations where a battery or inverter tries to keep grid power close to
`0` before the charger has time to react. If this number is omitted, solar policy
uses an internal default of `300` and no number entity is created.

### Site Storage

The optional `site.storage` section describes a battery/storage system. Solar
policy uses `storage.power` when available to detect the case where the battery is
discharging faster than the charger can reduce power. Storage power uses this sign
convention: positive values mean the storage is discharging into the site, and
negative values mean the storage is charging.

When storage power is configured and positive, solar policy subtracts that
discharge power from available solar headroom. If no storage power sensor is
configured, solar policy falls back to the `solar.export_margin_power` workaround
described above.

Example with aggregate storage power and state of charge:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    policy: solar
    storage:
      power:
        aggregate: battery_power
      capacity: 10
      soc: battery_soc
```

Use `capacity` in `kWh`. Configure either `soc` in `%` or `energy` in `kWh`, not
both. When one is provided, the component calculates the other internally from
`capacity` so future policies can use either representation.

### Storage Options

| Option                       | Description |
| ---                          | --- |
| `power` (Optional)           | Storage power sensor configuration under `site.storage.power`. Positive values mean discharging into the site; negative values mean charging. Defaults to not configured. |
| `capacity` (Optional)        | Storage capacity in `kWh`. Required when `soc` or `energy` is configured. Defaults to not configured. |
| `soc` (Optional)             | Sensor ID for storage state of charge in `%`. Mutually exclusive with `energy`. Requires `capacity`. Defaults to not configured. |
| `energy` (Optional)          | Sensor ID for storage energy currently present in `kWh`. Mutually exclusive with `soc`. Requires `capacity`. Defaults to not configured. |

### Storage Power Options

| Option                        | Description |
| ---                           | --- |
| `power.l1` (Optional)         | Sensor ID for storage power on phase `L1`. Use this for single-phase sites and configure it together with `power.l2` and `power.l3` for accurate three-phase storage metering.<br>Defaults to none. |
| `power.l2` (Optional)         | Sensor ID for storage power on phase `L2`. Configure it together with `power.l1` and `power.l3` for accurate three-phase storage metering.<br>Defaults to none. |
| `power.l3` (Optional)         | Sensor ID for storage power on phase `L3`. Configure it together with `power.l1` and `power.l2` for accurate three-phase storage metering.<br>Defaults to none. |
| `power.aggregate` (Optional)  | Sensor ID for aggregate storage power. Use only as a fallback for three-phase sites when the storage meter reports aggregate power but not per-phase power; estimated per-phase storage power is calculated as `aggregate / 3`. Do not combine with `power.l1`, `power.l2`, or `power.l3`.<br>Defaults to not configured. |

### Grid Connection

The optional `site.grid` section describes limits and measurements for the grid
connection. Any configured grid limit is enforced by the built-in equal
allocator. `site.storage` can provide battery measurements for the solar policy
and for future site policies.

### Grid Options

| Option                              | Description |
| ---                                 | --- |
| `max_current` (Required)            | Physical grid current limit per phase in `A`, for example `32`. |
| `max_power` (Required)              | Maximum total power that may be drawn from the grid in `W`, for example `10000` for a 10 kW electrical supply. This is the grid draw limit, not the power reserved for EV charging; available charging power depends on other site loads. |
| `max_phase_imbalance` (Optional)    | Grid phase imbalance limit in `W`, when the provider defines one.<br>Defaults to no imbalance-specific limit. |
| `headroom_current` (Optional)       | Sensor or sensor group that receives pure grid current headroom in `A`, calculated only from grid limits and signed `site.grid.power` measurements. Configure the same scalar or per-phase shape as `drawn_current`. Defaults to not configured. |
| `power` (Optional)                  | Grid power sensor configuration under `site.grid.power`. If omitted, the component assumes all of `grid.max_power` is available for charging. If other loads draw power from the grid and no power sensor is configured, configure a lower `max_power` to account for those loads. |

Published `grid.headroom_current` sensors are diagnostic grid values. Connector
allocation recalculates current headroom for the charger's actual load shape: a
known single-phase charger uses its configured `phase_mapping`; a three-phase
charger uses all three phases unless phase-specific current measurements show
that the active car is drawing from exactly one phase.

## Dynamic Grid Power Measurements

For real load balancing, the component should be able to account for the current
non-EV grid load. This can be provided by existing sensors under `site.grid.power`.
Use signed grid power values: positive values mean power is imported from the
grid, and negative values mean power is exported to the grid.

Single-phase example:

```yaml
ocpp:
  site:
    phases: 1
    voltage: 230
    grid:
      max_power: 6000
      max_current: 32
      power:
        l1: grid_power_l1
```

For a single-phase site, use `grid.power.l1` for the site/grid power sensor.
`grid.power.aggregate` is not needed because `L1` is the only phase.

Three-phase example with per-phase metering:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    grid:
      max_power: 10000
      max_phase_imbalance: 6000
      max_current: 32
      power:
        l1: grid_power_l1
        l2: grid_power_l2
        l3: grid_power_l3
```

For a three-phase site, per-phase metering is the recommended configuration.
The referenced sensors should represent the current grid/site power on each
phase. The OCPP component can then compute how much additional power is
available for EV charging on `L1`, `L2`, and `L3` by subtracting the measured
site load from the configured grid limits.

Three-phase example with aggregate-only metering:

```yaml
ocpp:
  site:
    phases: 3
    voltage: 230
    grid:
      max_power: 10000
      max_phase_imbalance: 6000
      max_current: 32
      power:
        aggregate: grid_power_aggregate
```

On a three-phase site, `grid.power.aggregate` can be used as an explicit fallback
when the meter only reports aggregate site power. In that mode, the component
estimates per-phase site load by dividing the total measured power by `3`.
Phase imbalance limits remain operational, but they are calculated from this
estimate instead of real per-phase measurements. Do not configure
`grid.power.aggregate` together with `grid.power.l1`, `grid.power.l2`, or
`grid.power.l3`.

Per-phase metering is strongly recommended for three-phase installations. With
aggregate-only metering, per-phase current limits and phase imbalance limits
cannot be guaranteed because the component cannot know how the non-EV site load
is actually distributed across `L1`, `L2`, and `L3`. Installers must always
protect all lines with correctly rated protective devices so that, in the worst
case, a circuit breaker or fuse can interrupt an overload.

### Grid Power Options

| Option                        | Description |
| ---                           | --- |
| `power.l1` (Optional)         | Sensor ID for grid/site power on phase `L1`. Use this for single-phase sites and configure it together with `power.l2` and `power.l3` for accurate three-phase metering.<br>Defaults to none. |
| `power.l2` (Optional)         | Sensor ID for grid/site power on phase `L2`. Configure it together with `power.l1` and `power.l3` for accurate three-phase metering.<br>Defaults to none. |
| `power.l3` (Optional)         | Sensor ID for grid/site power on phase `L3`. Configure it together with `power.l1` and `power.l2` for accurate three-phase metering.<br>Defaults to none. |
| `power.aggregate` (Optional)  | Sensor ID for aggregate grid/site power. Use only as a fallback for three-phase sites when the meter reports aggregate power but not per-phase power; estimated per-phase load is calculated as `aggregate / 3`. Do not combine with `power.l1`, `power.l2`, or `power.l3`.<br>Defaults to not configured. |

For accurate three-phase dynamic load balancing, configure all of
`grid.power.l1`, `grid.power.l2`, and `grid.power.l3`. If only
`grid.power.aggregate` is configured on a three-phase site, the allocator uses
the `aggregate / 3` estimate described above and knows that phase-specific
protection is approximate. Avoid creating external template sensors that divide
an aggregate meter by `3` and expose the result as real per-phase measurements,
because that hides the approximation from the component. If no `grid.power`
sensors are configured, the allocator can only use the static limits from
`site.grid`.

## Allocation

The `allocation` section defines how charging current is assigned to active
charging sessions. The component currently has one implemented strategy: equal
sharing.

```yaml
ocpp:
  allocation:
    strategy: equal
    min_current: 6
    update_interval: 10s
    preference: first_connected
```

Equal allocation starts from site current headroom calculated for the connector's
load shape and the current already used by active charging sessions. The
available site headroom is shared equally between active connectors and added to
each connector's current draw. With the current single-connector implementation,
this means the active connector can use its measured or assumed EV current plus
the current still available at the site. A single-phase load can therefore use
more current than a balanced three-phase load when `grid.max_phase_imbalance`,
`grid.max_current`, and `grid.max_power` allow it.

Each allocation cycle calculates two connector current states. `available_current`
is the raw current in `A` that the allocator calculated for the connector.
`allocated_current` is the effective current in `A` after charger-operational
constraints have been applied. If the calculated value is below `min_current`,
`allocated_current` is `0`, and the component stops the transaction with
`RemoteStopTransaction` instead of sending a too-low `SetChargingProfile` value.
Positive `allocated_current` values are sent to the charger using
`SetChargingProfile`.

`preference` expresses which sessions should keep charging first when there is
not enough current to keep all cars above `min_current`. It is accepted as a
forward-compatible configuration option for future multi-connector allocation;
with the current single-connector implementation it has no effect.

`update_interval` is also accepted as a forward-compatible allocation option. The
current implementation recalculates allocation when relevant site or connector
state changes. Whether a periodic interval remains useful should be revisited
before multi-connector scheduling is finalized.

### Allocation Options

| Option                       | Description |
| ---                          | --- |
| `strategy` (Optional)        | Power sharing strategy. Defaults to `equal`.<br>Available values: `equal`. |
| `min_current` (Optional)     | Minimum AC charging current per active connector in `A`. Defaults to `6`. |
| `update_interval` (Optional) | Forward-compatible interval for future periodic allocation updates. Defaults to `10s`; currently accepted but not used for scheduling. |
| `preference` (Optional)      | Forward-compatible preference for choosing which sessions keep charging when not all active sessions can receive at least `min_current`. Defaults to `first_connected`; currently accepted but has no effect while only one connector is supported.<br>Available values:<br>`first_connected` prefers older sessions; <br>`last_connected` prefers newer sessions; <br>`least_charged` prefers sessions with the lowest delivered `kWh` and will require live OCPP `Energy.Active.Import.Register` meter values; <br>`round_robin` rotates active charging slots over time. |

## Chargers and Connectors

Chargers are identified by their OCPP `charge_point_id`. Connectors are modeled
explicitly, even if most domestic chargers have only one connector.

```yaml
ocpp:
  chargers:
    - id: garage_left
      charge_point_id: GARAGE_LEFT
      max_current: 32
      phases: 3
      phase_mapping: [L1, L2, L3]
      drawn_current_source:
        l1: garage_left_current_l1
        l2: garage_left_current_l2
        l3: garage_left_current_l3
      drawn_current:
        name: Garage Left Charger Drawn Current
      connectors:
        - id: 1
          max_current: 16
          available_current:
            name: Garage Left Available Current
          allocated_current:
            name: Garage Left Allocated Current
          drawn_current:
            l1:
              name: Garage Left Drawn Current L1
            l2:
              name: Garage Left Drawn Current L2
            l3:
              name: Garage Left Drawn Current L3
          power:
            name: Garage Left Power
          state:
            name: Garage Left State
          enabled:
            name: Garage Left Enabled
          current_limit:
            name: Garage Left Current Limit
            initial_value: 16
            restore_value: true
          restart:
            name: Garage Left Restart Session
```

### Charger Options

| Option                            | Description |
| ---                               | --- |
| `id` (Required)                   | Internal ID for this charger.<br>Example: `garage_left`. |
| `charge_point_id` (Required)      | OCPP identity expected in the WebSocket URL. |
| `max_current` (Required)          | Physical charger current limit per phase in `A`, for example `16` or `32`. |
| `phases` (Required)               | Number of phases used by this charger. Values: `1` or `3`. |
| `phase_mapping` (Optional)        | Charger-to-site phase mapping. Defaults to `[L1]` for `phases: 1` and `[L1, L2, L3]` for `phases: 3`.<br>For single-phase chargers, configure one phase, for example `[L2]`. For three-phase chargers, configure all three phases in physical order, for example `[L2, L3, L1]`. |
| `drawn_current_source` (Optional) | Existing sensor ID, or per-phase sensor IDs, used as the preferred source for charger drawn current in `A`. When one source sensor is configured, its value is applied to all charger phases. When omitted, charger drawn current is calculated by summing connector `drawn_current` values by phase. Defaults to not configured. |
| `drawn_current` (Optional)        | Sensor that receives this charger's drawn current in `A`. The component tracks charger drawn current internally as `L1`, `L2`, and `L3`, and publishes the maximum phase value to this scalar sensor. Defaults to not configured. |
| `connectors` (Required)           | List of OCPP connectors. Each item must define at least `id`. |

### Charger Drawn Current

`drawn_current_source` is the preferred measurement input for charger current draw.
Use it when the charger or an external meter exposes real per-phase current sensors:

```yaml
chargers:
  - id: garage_left
    drawn_current_source:
      l1: garage_left_current_l1
      l2: garage_left_current_l2
      l3: garage_left_current_l3
    drawn_current:
      name: Garage Left Charger Drawn Current
```

If only one current sensor is available, configure it directly. The same value is
then applied to all charger phases:

```yaml
chargers:
  - id: garage_right
    drawn_current_source: garage_right_current
    drawn_current:
      name: Garage Right Charger Drawn Current
```

When `drawn_current_source` is omitted, charger drawn current is calculated by
summing the configured connector `drawn_current` values by phase. The published
charger `drawn_current` sensor is scalar and reports the maximum of the three
internal phase values.

### Connector Options

| Option                           | Description |
| ---                              | --- |
| `id` (Required)                  | OCPP connector ID. Usually `1` for single-connector chargers. |
| `max_current` (Optional)         | Physical connector current limit per phase in `A`, for example `16` or `32`.<br>Defaults to the charger's `max_current`. |
| `available_current` (Optional)   | Sensor that receives the raw current in `A` calculated as available for this connector before charger-operational constraints are applied. Defaults to not configured. |
| `allocated_current` (Optional)   | Sensor that receives the effective current in `A` allocated to this connector after charger-operational constraints are applied. Defaults to not configured. |
| `drawn_current` (Optional)       | Sensor or sensor group that receives the actual current drawn by the vehicle/charger in `A`. Configure `drawn_current.name` for the scalar maximum phase current, any of `drawn_current.l1`, `drawn_current.l2`, and `drawn_current.l3` for per-phase sensors, or both scalar and per-phase sensors in the same block. Defaults to not configured. |
| `current` (Optional)             | Backward-compatible scalar sensor that receives this connector's latest non-phase-specific OCPP `Current.Import` value from `MeterValues`, in `A`. For phase-aware site calculations, prefer `drawn_current`. Defaults to not configured. |
| `power` (Optional)               | Sensor that receives this connector's latest OCPP `Power.Active.Import` value from `MeterValues`, in `W`. Defaults to not configured. |
| `state` (Optional)               | Text sensor that receives the connector plug/charging state derived from OCPP `StatusNotification`. Defaults to not configured. |
| `enabled` (Optional)             | Switch for enabling or disabling charging on this connector. Defaults to enabled when omitted. Turning the switch off stops the active charging session when one is known. Turning it on starts a new charging session when none is active. |
| `current_limit` (Optional)       | Number for this connector's requested charging current limit in `A`. Defaults: `min_value: 6`, `max_value: max_current`, `step: 1`. When changed during an active transaction, the component updates the charger's current limit; otherwise it stores the value. The stored value is also applied when a transaction starts or when the connector resumes `Charging` after being suspended. If omitted, a restart starts charging without an explicit current limit. |
| `restart` (Optional)             | Button that restarts the charging session. |

### Connector Current State Sensors

The optional connector current state sensors separate calculation, charger command,
and measured draw:

- `available_current` is the raw calculated current available to the connector in
  `A`.
- `allocated_current` is the current in `A` that is effectively assigned to the
  connector after applying charger constraints.
- `drawn_current` is the current in `A` actually drawn by the vehicle/charger.
  The scalar sensor publishes the maximum of the internally tracked phase
  currents. Per-phase sensors can also expose `l1`, `l2`, and `l3` separately.

### Connector State Text Sensor

The optional connector `state` text sensor exposes the latest OCPP connector
status as a simplified state string:

| State         | Source OCPP status |
| ---           | --- |
| `unplugged`   | `Available` |
| `plugged`     | `Preparing` |
| `charging`    | `Charging` |
| `paused`      | `SuspendedEV`, `SuspendedEVSE` |
| `finishing`   | `Finishing` |
| `reserved`    | `Reserved` |
| `unavailable` | `Unavailable` |
| `faulted`     | `Faulted` |
| `unknown`     | Any other or missing status |

```yaml
connectors:
  - id: 1
    state:
      name: Garage Left State
```

Example for a scalar drawn-current sensor:

```yaml
connectors:
  - id: 1
    available_current:
      name: Garage Right Available Current
    allocated_current:
      name: Garage Right Allocated Current
    drawn_current:
      name: Garage Right Drawn Current
```

Example for a connector on a three-phase charger:

```yaml
connectors:
  - id: 1
    available_current:
      name: Garage Left Available Current
    allocated_current:
      name: Garage Left Allocated Current
    drawn_current:
      l1:
        name: Garage Left Drawn Current L1
      l2:
        name: Garage Left Drawn Current L2
      l3:
        name: Garage Left Drawn Current L3
```

The scalar and per-phase connector sensors can be configured together in the same
way:

```yaml
connectors:
  - id: 1
    drawn_current:
      name: Garage Left Drawn Current
      l1:
        name: Garage Left Drawn Current L1
      l2:
        name: Garage Left Drawn Current L2
      l3:
        name: Garage Left Drawn Current L3
```

### Connector Enabled Switch

The optional `enabled` switch is intended for automations or Home Assistant
controls that should stop and start charging on a connector.

```yaml
connectors:
  - id: 1
    max_current: 16
    enabled:
      name: Garage Left Enabled
```

If `enabled` is omitted, the connector is treated as enabled. When the switch is
turned off and an active transaction is known, the current charging session is
stopped. When the switch is turned on again and no active transaction is known, a
new charging session is started.

When charging should resume, a new transaction is started and the normal current
allocation logic applies.

If there is not enough available current when the connector is enabled, the
transaction can stay effectively paused or waiting until enough current is
available, depending on the charger.

### Connector Restart Button

The optional `restart` button is a manual recovery control for restarting a
connector session.

```yaml
connectors:
  - id: 1
    max_current: 16
    restart:
      name: Garage Left Restart Session
```

This is useful when the charger is connected but the component does not know the
current transaction ID.

When the connector is disabled, `restart` will not start a new transaction. If an
active transaction is known, it may still stop that transaction, but automatic
restart is suppressed until the connector is enabled.

### Connector Current Limit Number

The optional `current_limit` number stores the preferred current limit for the
connector and can also update an active transaction.

```yaml
connectors:
  - id: 1
    max_current: 16
    current_limit:
      name: Garage Left Current Limit
      min_value: 6
      max_value: 16
      step: 1
      initial_value: 16
      restore_value: true
    restart:
      name: Garage Left Restart Session
```

If `max_value` is omitted, it defaults to the lower of the charger and connector
`max_current` values. If `initial_value` is omitted, it defaults to `min_value`.
Set `restore_value: true` to restore the last selected current limit from flash;
when no stored value exists yet, `initial_value` is used. The current value of
`current_limit` is treated as this connector's preferred upper limit. The
effective limit may be lower when needed to respect configured power-source
limits. If `current_limit` is not configured, the connector uses its configured
`max_current` as the preferred upper limit.

| Option                     | Description |
| ---                        | --- |
| `name` (Required)          | Name of the current-limit number entity. |
| `min_value` (Optional)     | Minimum selectable current in `A`. Defaults to `6`. |
| `max_value` (Optional)     | Maximum selectable current in `A`. Defaults to the lower of the charger and connector `max_current` values. |
| `step` (Optional)          | Step size in `A`. Defaults to `1`. |
| `initial_value` (Optional) | Startup value in `A` when no restored value is available. Defaults to `min_value`. |
| `restore_value` (Optional) | Whether to restore the last selected value from flash. Defaults to `false`. |

### Phase Mapping

For a single-phase charger on a three-phase site:

```yaml
chargers:
  - id: garage_right
    charge_point_id: GARAGE_RIGHT
    max_current: 32
    phases: 1
    phase_mapping: [L2]
    connectors:
      - id: 1
```

For a three-phase charger with explicit phase order:

```yaml
chargers:
  - id: garage_left
    charge_point_id: GARAGE_LEFT
    max_current: 32
    phases: 3
    phase_mapping: [L1, L2, L3]
    connectors:
      - id: 1
        max_current: 16
```

Explicit charger-level phase mapping is useful when installers rotate phases
between chargers to improve load balancing.
