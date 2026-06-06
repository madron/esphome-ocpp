# Development Notes

This file contains implementation notes for contributors. Keep user-facing setup,
configuration, and operational hints in the root `README.md`.

## OCPP Connector Control Strategy

Connector enable/disable control uses OCPP transaction stop/start messages rather
than a zero-current smart charging profile.

Observed charger behavior during development:

- The charger may accept `SetChargingProfile` with a `0 A` limit and still keep
  charging.
- A positive `SetChargingProfile` works for current limiting once a transaction is
  active.
- `RemoteStopTransaction` reliably stops charging when the active transaction ID
  is known.
- `RemoteStartTransaction` can start a new transaction when the charger is
  connected and waiting.

For that reason:

- `enabled: false` stops a known active transaction with
  `RemoteStopTransaction`.
- `enabled: true` starts a transaction with `RemoteStartTransaction` when no
  active transaction is known.
- `SetChargingProfile` is used for positive current limits, not as the primary
  way to stop charging.
- If a transaction starts while the connector is disabled, the implementation
  immediately requests `RemoteStopTransaction` for that transaction.

This behavior avoids relying on charger-specific handling of `0 A` smart charging
profiles.

## Connector Current State Model

Connector current state intentionally separates the allocator result, the charger
command value, and the measured vehicle draw:

- `available_current` is the raw current in `A` calculated as available for the
  connector before charger-operational constraints are applied.
- `allocated_current` is the effective current in `A` after charger constraints
  are applied. If `available_current` is below the configured minimum charging
  current, currently `allocation.min_current` with a default of `6`, it is clamped
  to `0`. It must also respect the connector enabled state, connector
  `max_current`, and the requested `current_limit`.
- `drawn_current` is the actual current in `A` drawn by the vehicle/charger. It is
  represented internally as a three-value vector in charger-local phase order:
  `L1`, `L2`, and `L3`.

`available_current` and `allocated_current` are both kept because they answer
different operational questions. `available_current` explains what the allocator
calculated, including sub-minimum values such as `4 A`. `allocated_current`
explains what the component actually applies to the charger. Keeping both makes it
possible to diagnose why a connector is paused without exposing additional names
such as target or applied current.

When `allocated_current` is `0`, the component should stop the transaction with
`RemoteStopTransaction`. It should not send a `SetChargingProfile` below the
charger minimum current, and it should not rely on a `0 A` charging profile as the
primary stop mechanism. When `allocated_current` is positive, it should be at least
the configured minimum current and may be sent using `SetChargingProfile`.

OCPP 1.6 `SetChargingProfile` provides a scalar current or power value for a
charging schedule period, optionally with `numberPhases`. It does not provide
independent current values for `L1`, `L2`, and `L3`. For that reason,
`available_current` and `allocated_current` remain scalar per-connector values.
The per-phase representation is needed for `drawn_current`, because site-level
accounting must know how much current EV charging contributes to each physical
site phase.

## Charger Drawn Current State Model

Charger drawn current follows the same internal shape as connector drawn current:
three current values in charger-local phase order, `L1`, `L2`, and `L3`, expressed
in `A`. This vector is internal state used for charger-level diagnostics and as an
input to site-level accounting.

The charger-level source and exposed sensor have intentionally different roles:

- `drawn_current_source` is an optional measurement input. It references existing
  ESPHome current sensors and is the preferred source when real charger-level or
  meter-level current measurements are available.
- `drawn_current` is an optional user-facing ESPHome sensor published by this
  component. Because ESPHome sensors expose a single scalar value, it publishes the
  maximum of the three internal phase currents.

`drawn_current_source` should support both forms:

1. A per-phase mapping with `l1`, `l2`, and `l3` sensor IDs. These values are read
   as the charger current on each corresponding charger-local phase.
2. A single sensor ID. In this fallback form, the measured value is applied to all
   three charger phases. This is useful when the meter reports one balanced or
   aggregate current value and no phase-specific measurements are available.

When no `drawn_current_source` is configured, charger drawn current is derived from
connectors by summing connector `drawn_current` by charger-local phase:

```text
charger_drawn_current[Lx] = sum(connector_drawn_current[Lx])
```

The source-priority rule is therefore:

1. Use real `drawn_current_source` measurements when configured.
2. Otherwise, use the calculated connector sum by phase.

This keeps real measurements authoritative without requiring charger-level current
sensors for installations where connector OCPP metering is sufficient. It also
keeps `drawn_current` as a read-only `sensor`: it describes observed or calculated
state, while writable controls such as current limits remain `number` entities.

Do not apply charger `phase_mapping` when reading `drawn_current_source` or when
maintaining the charger internal `drawn_current` vector. Both are charger-local.
Apply `phase_mapping` only at the boundary where charger-level current is passed to
site-level calculations, so the site model receives currents in physical site phase
order.

## OCPP Current Metering and Phase Mapping

OCPP 1.6 `MeterValues` is connector-scoped: the message contains a `connectorId`,
and each `sampledValue` can include a `phase` field. A connector can therefore
report phase-specific current values such as `Current.Import` on `L1`, `L2`, and
`L3`. Some connectors instead report one non-phase-specific `Current.Import` value
with an empty `phase` field.

The intended responsibility split is:

1. The connector metering code converts OCPP `MeterValues` into a charger-phase
   current vector. It interprets the OCPP `phase` field and the configured charger
   phase count, but it does not apply charger-to-site phase rotation.
2. The charger stores connector and charger `drawn_current` internally in
   charger-local phase order `[L1, L2, L3]` in `A`.
3. The charger owns `phase_mapping`. It maps charger-local drawn current to site
   phases only when passing current data to site-level calculations.

The model assumes all connectors on a charger use the same phase count as the
charger. Mixed single-phase and three-phase connectors on the same charger are not
modeled.

For phase-specific `Current.Import` samples, the connector records the values
against the corresponding charger-local phases. No phase mapping is applied while
updating connector or charger internal `drawn_current` state.

For a non-phase-specific `Current.Import` sample, the connector builds the
charger-phase vector from the configured charger phase count. For a connector on a
single-phase charger, assign the value to the first charger-local phase and treat
the other phases as `0 A`. For a connector on a three-phase charger, assume the
value is a balanced per-phase current and assign it to all three charger-local
phases. The charger then maps the resulting charger-phase vector to the site-phase
vector only when site-level calculations need physical site phase values.

The balanced three-phase assumption is accurate for a three-phase car drawing
balanced current from a three-phase charger. It is only an approximation for a
single-phase car plugged into a three-phase connector when the connector does not
expose phase-specific current metering. In that case the component cannot know
which physical phase is carrying the current from OCPP alone. Accurate site-level
phase accounting for that scenario requires phase-specific `Current.Import` values
from the connector or another phase-aware measurement source.

During an active charging session, measured `drawn_current` should only become
authoritative after a valid session-local `Current.Import` value has been
received. Before the first current sample arrives, user-facing drawn-current
sensors may show `0 A`, but internal hard-limit calculations should assume the
connector is drawing its `allocated_current`. This is intentionally conservative:
it avoids briefly over-allocating site capacity while the charger is still waiting
to send its first current meter value. When no charging session is active,
`drawn_current` is always treated as `0 A` and session-local current availability
is reset so stale current samples from a previous transaction are not reused.

Future OCPP capability discovery should use `GetConfiguration` to read and log
metering-related keys such as `MeterValuesSampledData`,
`MeterValueSampleInterval`, `MeterValuesAlignedData`, and
`ClockAlignedDataInterval`. The log should make it clear whether the charger
appears to report `Current.Import`, which interval it uses, and whether periodic
meter values are disabled. If the charger allows it, the component may use
`ChangeConfiguration` to request `Current.Import` in sampled meter values and a
reasonable non-zero `MeterValueSampleInterval`. That would both document charger
capabilities for users and reduce the time spent in the conservative
allocated-current fallback after a session starts.

## Transaction State After Restarts

During development, ESPHome restarts can happen while a car is connected or while
a charger still has session state. After a restart, the component may not know the
active transaction ID until it receives a new `StartTransaction`, `StopTransaction`,
or usable `MeterValues` with a transaction ID.

The `restart` connector button is intended as a manual recovery control for this
case. If no active transaction is known, it sends `RemoteStartTransaction`
directly. If a transaction is known, it stops that transaction and starts a new one
after the charger reports `StopTransaction`.

## OCPP Message Scope

The first implementation focuses on the minimal OCPP 1.6J messages needed for
local power management.

Messages handled from charger to component:

- `BootNotification`
- `Heartbeat`
- `StatusNotification`
- `Authorize`
- `StartTransaction`
- `StopTransaction`
- `MeterValues`

Messages sent from component to charger:

- `SetChargingProfile`
- `ClearChargingProfile`
- `RemoteStartTransaction`
- `RemoteStopTransaction`
- `ChangeAvailability`
- `Reset`

`SetChargingProfile` is used for positive current limiting and load sharing.

## Hard Current Limits

The three configured `max_current` values are hard current limits and must be
respected contemporarily: `site.grid.max_current`, `charger.max_current`, and
`connector.max_current`. The component must not allocate or request a current that
exceeds any configured limit that applies to the connector.

All three `max_current` options describe the electrical installation, such as the
size of cables and the capacity of breakers or fuses. They are not car capability
limits and they are not an expression of currently available power; available
charging power is calculated separately from the grid draw limits and live site
power measurements. Likewise, `site.grid.max_power` is the maximum total power
that may be drawn from the grid, not the power reserved for EV charging.

## Minimal v1 Goals

The first useful version should be able to:

1. Accept charger WebSocket connections.
2. Accept `BootNotification` and `Heartbeat`.
3. Track connector state from `StatusNotification`.
4. Authorize private charging for configured chargers according to the authorization policy.
5. Track transactions using `StartTransaction` and `StopTransaction`.
6. Read power in `W` and energy in `kWh` from `MeterValues`.
7. Calculate current limits from the configured site and power-source constraints.
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
- complex solar surplus optimization.

## OCPP idTag

OCPP 1.6 `RemoteStartTransaction` requires an `idTag`. In public charging systems,
this usually identifies the user, RFID card, account, or authorization token for
the transaction.

This component currently targets private local control and does not expose `idTag`
as YAML configuration. Remote starts use the fixed internal value
`esphome-ocpp`.

If authentication or user-level authorization is implemented later, this fixed
value should be revisited. Possible future designs include deriving the idTag from
an authenticated Home Assistant user, an RFID reader, an allowlist, or another
authorization source rather than making it a per-connector static setting.
