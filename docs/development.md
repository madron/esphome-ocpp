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
