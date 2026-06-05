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
  to `0`.
- `drawn_current` is the actual current in `A` drawn by the vehicle/charger. It is
  represented internally as a three-value vector in site phase order: `L1`, `L2`,
  and `L3`.

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

## OCPP Current Metering and Phase Mapping

OCPP 1.6 `MeterValues` supports a `phase` field on each `sampledValue`, so chargers
can report phase-specific current values such as `Current.Import` on `L1`, `L2`,
and `L3`. Some chargers instead report one non-phase-specific `Current.Import`
value with an empty `phase` field.

The intended mapping rules for `drawn_current` are:

1. Store `drawn_current` internally as a site-phase vector `[L1, L2, L3]` in `A`.
2. If `MeterValues` includes phase-specific `Current.Import` values, map each OCPP
   phase to the corresponding site phase using the connector `phase_mapping`.
3. If a single-phase connector reports a non-phase-specific `Current.Import` value,
   assign that value to the connector's mapped site phase and set the other phases
   to `0 A`.
4. If a three-phase connector reports a non-phase-specific `Current.Import` value,
   assume it is a balanced per-phase current and assign the same value to all three
   mapped site phases.

Rule 4 is accurate for a three-phase car drawing balanced current from a
three-phase charger. It is only an approximation for a single-phase car plugged
into a three-phase charger when the charger does not expose phase-specific current
metering. In that case the component cannot know which physical phase is carrying
the current from OCPP alone. Accurate site-level phase accounting for that scenario
requires phase-specific `Current.Import` values from the charger or another
phase-aware measurement source.

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
