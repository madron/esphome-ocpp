# OcppMessage

**Source:** `esphome/components/ocpp/message.h:35`

Version-agnostic message model. All message types live in a single header and are used by both `OcppProtocol` (parsing/making) and `ChargePoint` (handling).

## Class Hierarchy

```
OcppMessage (base)
 ├── message_type_id: OcppMessageType
 ├── unique_id: string
 ├── action: string
 │
 ├── OcppCall (base for CALL messages)
 │    ├── BootNotification
 │    │    ├── charge_point_model, charge_point_vendor, firmware_version
 │    ├── Authorize
 │    │    ├── id_tag
 │    ├── StartTransaction
 │    │    ├── connector_id, id_tag
 │    ├── StopTransaction
 │    │    ├── transaction_id
 │    ├── MeterValues
 │    │    ├── connector_id, transaction_id, sampled_values
 │    │    ├── current, power, energy, voltage (computed aggregates)
 │    │    ├── current_l1/l2/l3, voltage_l1/l2/l3, active_phases (phase inference)
 │    └── StatusNotification
 │         ├── connector_id, error_code, status
 │
 ├── GetConfigurationResponse (CALL_RESULT)
 │    ├── meter_value_sample_interval, meter_values_sampled_data, connector_switch_3_to_1_phase_supported
 │
 └── ChangeConfigurationResponse (CALL_RESULT)
      └── status

SampledValue (value struct)
 ├── value: float, measurand: string, unit: string, phase: string
```

## OcppMessageType Enum (`message.h:18`)

| Value | ID | Meaning |
| --- | --- | --- |
| `CALL` | 2 | Request from either side |
| `CALL_RESULT` | 3 | Success response |
| `CALL_ERROR` | 4 | Error response |

## Constants (`message.h:12-16`)

| Constant | Value | Purpose |
| --- | --- | --- |
| `DEFAULT_PHASE_VOLTAGE` | 230.0 V | Fallback voltage for phase inference |
| `MIN_PHASE_INFERENCE_VOLTAGE` | 190.0 V | Minimum voltage to attempt inference |
| `MIN_PHASE_INFERENCE_CURRENT` | 6.0 A | Minimum current to consider a phase active |
| `MIN_PHASE_INFERENCE_POWER` | 1140 W | Derived: 190 V × 6 A |
| `MAX_PHASE_INFERENCE_ERROR` | 0.4 | Max deviation for phase count rounding |

## MeterValues Phase Inference

`MeterValues::calculate_phase_values()` (`message.h:217`):

### Strategy

1. **Explicit per-phase currents**: If the charger reports current with phase tags (`L1`, `L2`, `L3`), use them directly. Count phases with current ≥ 6A as active.
2. **Inferred from power/current ratio**: If only aggregate current is available, estimate active phases as `round(power / (voltage × current))`. Requires valid power, current, and voltage (or fallback `phase_voltage`). Rejects if deviation > `MAX_PHASE_INFERENCE_ERROR`.

### Phase Value Distribution

When active phase count is inferred (or latched from a previous reading):
- If `active_phases = N`, distribute the aggregate current to the first N configured phases
- Respect `connector_phases` upper bound

### Voltage Phase Values

`calculate_voltage_phase_values()` (`message.h:477`):
- If charger reports per-phase voltages, use them directly
- If only aggregate voltage: replicate to all configured phases (L1 = voltage, L2/L3 = 0 for single phase; same voltage to all for 3-phase)
- Missing phases for the configured count default to 0

### Unit Normalization

| Measurand | `normalize_*()` | Target unit |
| --- | --- | --- |
| `Current.Import` | `normalize_current_()` — mA → A | A |
| `Power.Active.Import` | `normalize_power_()` — kW → W | W |
| `Energy.Active.Import.Register` | `normalize_energy_()` — Wh → kWh, kWh passthrough, MWh → kWh | kWh |
| `Voltage` | `normalize_voltage_()` — mV → V, kV → V | V |

### `sampled_values_summary()`

`message.h:204`: Builds a compact log string. Phase-aware grouping for current and voltage (if per-phase values exist, shows `L1=10 A, L2=10 A, L3=10 A`), otherwise falls back to aggregate-only grouping.
