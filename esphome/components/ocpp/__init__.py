from collections.abc import Mapping

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, number, sensor, switch, text_sensor
from esphome.const import (
    CONF_CAPACITY,
    CONF_CURRENT,
    CONF_ENERGY,
    CONF_ID,
    CONF_INITIAL_VALUE,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_NAME,
    CONF_PORT,
    CONF_POWER,
    CONF_RESTORE_VALUE,
    CONF_STATE,
    CONF_STEP,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_WATT,
)

DEPENDENCIES = ["network"]
AUTO_LOAD = ["json", "socket", "sensor", "number", "button", "switch", "text_sensor"]

CONF_CHARGE_POINT_ID = "charge_point_id"
CONF_CHARGERS = "chargers"
CONF_CONNECTORS = "connectors"
CONF_AVAILABLE_CURRENT = "available_current"
CONF_ALLOCATION = "allocation"
CONF_ALLOCATED_CURRENT = "allocated_current"
CONF_CURRENT_LIMIT = "current_limit"
CONF_DRAWN_CURRENT = "drawn_current"
CONF_DRAWN_CURRENT_SOURCE = "drawn_current_source"
CONF_GRID = "grid"
CONF_HEADROOM_CURRENT = "headroom_current"
CONF_L1 = "l1"
CONF_L2 = "l2"
CONF_L3 = "l3"
CONF_AGGREGATE = "aggregate"
CONF_MAX_CURRENT = "max_current"
CONF_MAX_PHASE_IMBALANCE = "max_phase_imbalance"
CONF_MAX_POWER = "max_power"
CONF_MIN_CURRENT = "min_current"
CONF_PHASE_MAPPING = "phase_mapping"
CONF_PHASES = "phases"
CONF_POLICY = "policy"
CONF_PREFERENCE = "preference"
CONF_ENABLED = "enabled"
CONF_RESTART = "restart"
CONF_SERVER = "server"
CONF_SITE = "site"
CONF_PATH = "path"
CONF_SOLAR = "solar"
CONF_SETTLE_TIME_PER_AMP = "settle_time_per_amp"
CONF_EXPORT_MARGIN_POWER = "export_margin_power"
CONF_STORAGE = "storage"
CONF_SOC = "soc"
CONF_STRATEGY = "strategy"
CONF_VOLTAGE = "voltage"

ocpp_ns = cg.esphome_ns.namespace("ocpp")
OcppServer = ocpp_ns.class_("OcppServer", cg.Component)
OcppConnectorButton = ocpp_ns.class_("OcppConnectorButton", button.Button)
OcppConnectorEnabledSwitch = ocpp_ns.class_("OcppConnectorEnabledSwitch", switch.Switch)
OcppCurrentLimitNumber = ocpp_ns.class_("OcppCurrentLimitNumber", number.Number, cg.Component)
OcppSolarExportMarginNumber = ocpp_ns.class_("OcppSolarExportMarginNumber", number.Number)

PHASE_KEYS = (CONF_L1, CONF_L2, CONF_L3)
PHASE_TO_INDEX = {CONF_L1.upper(): 0, CONF_L2.upper(): 1, CONF_L3.upper(): 2}


CURRENT_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_AMPERE,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_CURRENT,
    state_class=STATE_CLASS_MEASUREMENT,
)

DRAWN_CURRENT_SOURCE_PHASE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_L1): cv.use_id(sensor.Sensor),
        cv.Required(CONF_L2): cv.use_id(sensor.Sensor),
        cv.Required(CONF_L3): cv.use_id(sensor.Sensor),
    }
)

DRAWN_CURRENT_SOURCE_SCHEMA = cv.Any(
    cv.use_id(sensor.Sensor), DRAWN_CURRENT_SOURCE_PHASE_SCHEMA
)


def _validate_path(value):
    value = cv.string(value)
    if not value.startswith("/"):
        raise cv.Invalid("OCPP WebSocket path must start with '/'")
    return value.rstrip("/") or "/"


def _consume_ocpp_sockets(config):
    from esphome.components import socket

    socket.consume_sockets(2, "ocpp")(config)
    return config


def _current_sensor_config(value):
    if isinstance(value, str):
        value = {CONF_NAME: value}
    return CURRENT_SENSOR_SCHEMA(value)


def _drawn_current_schema(value):
    if not isinstance(value, Mapping):
        return _current_sensor_config(value)

    value = dict(value)
    if not any(phase in value for phase in PHASE_KEYS):
        return _current_sensor_config(value)

    validated = {}
    scalar_config = {key: config_value for key, config_value in value.items() if key not in PHASE_KEYS}
    if scalar_config:
        validated.update(_current_sensor_config(scalar_config))

    for phase in PHASE_KEYS:
        if phase in value:
            validated[phase] = _current_sensor_config(value[phase])
    return validated


DRAWN_CURRENT_SCHEMA = _drawn_current_schema


def _split_drawn_current_config(config):
    if not isinstance(config, Mapping):
        return config, {}

    phase_configs = {phase: config[phase] for phase in PHASE_KEYS if phase in config}
    if not phase_configs:
        return config, {}

    scalar_config = {key: value for key, value in config.items() if key not in PHASE_KEYS}
    return scalar_config or None, phase_configs


def _phase_name(value):
    value = cv.string_strict(value).upper()
    if value not in PHASE_TO_INDEX:
        raise cv.Invalid("phase must be one of L1, L2 or L3")
    return value


def _validate_chargers(value):
    charge_point_ids = set()
    charger_ids = set()
    for charger in value:
        charger_id = charger[CONF_ID]
        if charger_id in charger_ids:
            raise cv.Invalid(f"Duplicate charger id '{charger_id}'")
        charger_ids.add(charger_id)

        charge_point_id = charger[CONF_CHARGE_POINT_ID]
        if charge_point_id in charge_point_ids:
            raise cv.Invalid(f"Duplicate charge_point_id '{charge_point_id}'")
        charge_point_ids.add(charge_point_id)

        phases = charger[CONF_PHASES]
        phase_mapping = charger.setdefault(
            CONF_PHASE_MAPPING,
            ["L1"] if phases == 1 else ["L1", "L2", "L3"],
        )
        if len(phase_mapping) != phases:
            raise cv.Invalid(
                f"phase_mapping for charger '{charger_id}' must contain exactly {phases} phase(s)"
            )
        if phases == 3 and set(phase_mapping) != set(PHASE_TO_INDEX):
            raise cv.Invalid(
                f"three-phase charger '{charger_id}' must map all of L1, L2 and L3 exactly once"
            )

        connector_ids = set()
        for connector in charger[CONF_CONNECTORS]:
            connector_id = connector[CONF_ID]
            if connector_id in connector_ids:
                raise cv.Invalid(f"Duplicate connector id '{connector_id}' for charger '{charger_id}'")
            connector_ids.add(connector_id)

            connector.setdefault(CONF_MAX_CURRENT, charger[CONF_MAX_CURRENT])

            if limit := connector.get(CONF_CURRENT_LIMIT):
                effective_max_current = min(charger[CONF_MAX_CURRENT], connector[CONF_MAX_CURRENT])
                limit.setdefault(CONF_MAX_VALUE, effective_max_current)
                limit.setdefault(CONF_INITIAL_VALUE, limit[CONF_MIN_VALUE])
                if limit[CONF_MAX_VALUE] > effective_max_current:
                    raise cv.Invalid(
                        f"current_limit max_value for connector '{connector_id}' must not exceed the effective max_current"
                    )
                if limit[CONF_MIN_VALUE] >= limit[CONF_MAX_VALUE]:
                    raise cv.Invalid(
                        f"current_limit min_value for connector '{connector_id}' must be less than max_value"
                    )
                if (
                    limit[CONF_INITIAL_VALUE] < limit[CONF_MIN_VALUE]
                    or limit[CONF_INITIAL_VALUE] > limit[CONF_MAX_VALUE]
                ):
                    raise cv.Invalid(
                        f"current_limit initial_value for connector '{connector_id}' must be between min_value and max_value"
                    )
    return value


def _validate_site(value):
    phases = value[CONF_PHASES]
    grid = value.get(CONF_GRID, {})
    power = grid.get(CONF_POWER, {})
    storage = value.get(CONF_STORAGE, {})
    storage_power = storage.get(CONF_POWER, {})
    headroom_current = value.get(CONF_HEADROOM_CURRENT, {})
    if not isinstance(headroom_current, Mapping):
        headroom_current = {}
    grid_headroom_current = grid.get(CONF_HEADROOM_CURRENT, {})
    if not isinstance(grid_headroom_current, Mapping):
        grid_headroom_current = {}
    drawn_current = value.get(CONF_DRAWN_CURRENT, {})
    if not isinstance(drawn_current, Mapping):
        drawn_current = {}
    has_aggregate = CONF_AGGREGATE in power
    configured_phases = [phase for phase in PHASE_KEYS if phase in power]
    has_storage_aggregate = CONF_AGGREGATE in storage_power
    configured_storage_phases = [phase for phase in PHASE_KEYS if phase in storage_power]

    if phases == 1:
        if CONF_L2 in power or CONF_L3 in power:
            raise cv.Invalid("single-phase sites may only configure grid.power.l1")
        if CONF_L2 in headroom_current or CONF_L3 in headroom_current:
            raise cv.Invalid("single-phase sites may only configure headroom_current.l1")
        if CONF_L2 in grid_headroom_current or CONF_L3 in grid_headroom_current:
            raise cv.Invalid("single-phase sites may only configure grid.headroom_current.l1")
        if CONF_L2 in drawn_current or CONF_L3 in drawn_current:
            raise cv.Invalid("single-phase sites may only configure drawn_current.l1")
        if has_aggregate:
            raise cv.Invalid("single-phase sites should use grid.power.l1 instead of grid.power.aggregate")
        if CONF_L2 in storage_power or CONF_L3 in storage_power:
            raise cv.Invalid("single-phase sites may only configure storage.power.l1")
        if has_storage_aggregate:
            raise cv.Invalid("single-phase sites should use storage.power.l1 instead of storage.power.aggregate")
    elif configured_phases and len(configured_phases) != 3:
        raise cv.Invalid("three-phase sites must configure all of grid.power.l1, l2 and l3 together")
    elif configured_storage_phases and len(configured_storage_phases) != 3:
        raise cv.Invalid("three-phase sites must configure all of storage.power.l1, l2 and l3 together")

    if has_aggregate and configured_phases:
        raise cv.Invalid("grid.power.aggregate must not be combined with per-phase grid.power sensors")
    if has_storage_aggregate and configured_storage_phases:
        raise cv.Invalid("storage.power.aggregate must not be combined with per-phase storage.power sensors")
    if CONF_SOC in storage and CONF_ENERGY in storage:
        raise cv.Invalid("storage.soc and storage.energy are mutually exclusive")
    if (CONF_SOC in storage or CONF_ENERGY in storage) and CONF_CAPACITY not in storage:
        raise cv.Invalid("storage.capacity is required when storage.soc or storage.energy is configured")
    return value


def _validate_export_margin_number(value):
    if value[CONF_MIN_VALUE] >= value[CONF_MAX_VALUE]:
        raise cv.Invalid("export_margin_power min_value must be less than max_value")
    if value[CONF_INITIAL_VALUE] < value[CONF_MIN_VALUE] or value[CONF_INITIAL_VALUE] > value[CONF_MAX_VALUE]:
        raise cv.Invalid("export_margin_power initial_value must be between min_value and max_value")
    return value


SERVER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PORT, default=9000): cv.port,
        cv.Optional(CONF_PATH, default="/ocpp"): _validate_path,
    }
)

ALLOCATION_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_STRATEGY, default="equal"): cv.one_of("equal", lower=True),
        cv.Optional(CONF_MIN_CURRENT, default=6): cv.positive_float,
        cv.Optional(CONF_UPDATE_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SETTLE_TIME_PER_AMP, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_PREFERENCE, default="first_connected"): cv.one_of(
            "first_connected",
            "last_connected",
            "least_charged",
            "round_robin",
            lower=True,
        ),
    }
)

GRID_POWER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_L1): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_L2): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_L3): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_AGGREGATE): cv.use_id(sensor.Sensor),
    }
)

SOLAR_EXPORT_MARGIN_NUMBER_SCHEMA = cv.All(
    number.number_schema(
        OcppSolarExportMarginNumber,
        unit_of_measurement=UNIT_WATT,
        device_class=DEVICE_CLASS_POWER,
    ).extend(
        {
            cv.Optional(CONF_MIN_VALUE, default=0): cv.float_range(min=0),
            cv.Optional(CONF_MAX_VALUE, default=1000): cv.positive_float,
            cv.Optional(CONF_STEP, default=50): cv.positive_float,
            cv.Optional(CONF_INITIAL_VALUE, default=300): cv.float_range(min=0),
        }
    ),
    _validate_export_margin_number,
)

SOLAR_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_EXPORT_MARGIN_POWER): SOLAR_EXPORT_MARGIN_NUMBER_SCHEMA,
    }
)

STORAGE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_POWER): GRID_POWER_SCHEMA,
        cv.Optional(CONF_SOC): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_ENERGY): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_CAPACITY): cv.positive_float,
    }
)

GRID_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_MAX_POWER): cv.positive_float,
        cv.Optional(CONF_MAX_PHASE_IMBALANCE): cv.positive_float,
        cv.Required(CONF_MAX_CURRENT): cv.positive_float,
        cv.Optional(CONF_HEADROOM_CURRENT): DRAWN_CURRENT_SCHEMA,
        cv.Optional(CONF_POWER): GRID_POWER_SCHEMA,
    }
)

SITE_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_PHASES): cv.one_of(1, 3, int=True),
            cv.Required(CONF_VOLTAGE): cv.positive_float,
            cv.Optional(CONF_POLICY, default="normal"): cv.one_of("normal", "solar", lower=True),
            cv.Optional(CONF_SOLAR, default={}): SOLAR_SCHEMA,
            cv.Optional(CONF_HEADROOM_CURRENT): DRAWN_CURRENT_SCHEMA,
            cv.Optional(CONF_DRAWN_CURRENT): DRAWN_CURRENT_SCHEMA,
            cv.Optional(CONF_GRID): GRID_SCHEMA,
            cv.Optional(CONF_STORAGE): STORAGE_SCHEMA,
        }
    ),
    _validate_site,
)

CONNECTOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.int_range(min=1, max=255),
        cv.Optional(CONF_MAX_CURRENT): cv.positive_float,
        cv.Optional(CONF_AVAILABLE_CURRENT): CURRENT_SENSOR_SCHEMA,
        cv.Optional(CONF_ALLOCATED_CURRENT): CURRENT_SENSOR_SCHEMA,
        cv.Optional(CONF_DRAWN_CURRENT): DRAWN_CURRENT_SCHEMA,
        cv.Optional(CONF_CURRENT): CURRENT_SENSOR_SCHEMA,
        cv.Optional(CONF_STATE): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_POWER): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_CURRENT_LIMIT): number.number_schema(
            OcppCurrentLimitNumber,
            unit_of_measurement=UNIT_AMPERE,
            device_class=DEVICE_CLASS_CURRENT,
        ).extend(
            {
                cv.Optional(CONF_MIN_VALUE, default=6): cv.positive_float,
                cv.Optional(CONF_MAX_VALUE): cv.positive_float,
                cv.Optional(CONF_STEP, default=1): cv.positive_float,
                cv.Optional(CONF_INITIAL_VALUE): cv.positive_float,
                cv.Optional(CONF_RESTORE_VALUE, default=False): cv.boolean,
            }
        ),
        cv.Optional(CONF_ENABLED): switch.switch_schema(OcppConnectorEnabledSwitch),
        cv.Optional(CONF_RESTART): button.button_schema(OcppConnectorButton),
    }
)

CHARGER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.string_strict,
        cv.Required(CONF_CHARGE_POINT_ID): cv.string_strict,
        cv.Required(CONF_MAX_CURRENT): cv.positive_float,
            cv.Required(CONF_PHASES): cv.one_of(1, 3, int=True),
            cv.Optional(CONF_PHASE_MAPPING): cv.All(cv.ensure_list(_phase_name), cv.Length(min=1, max=3)),
        cv.Optional(CONF_DRAWN_CURRENT_SOURCE): DRAWN_CURRENT_SOURCE_SCHEMA,
        cv.Optional(CONF_DRAWN_CURRENT): CURRENT_SENSOR_SCHEMA,
        cv.Required(CONF_CONNECTORS): cv.All(cv.ensure_list(CONNECTOR_SCHEMA), cv.Length(min=1, max=1)),
    }
)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OcppServer),
            cv.Optional(CONF_SERVER, default={}): SERVER_SCHEMA,
            cv.Optional(CONF_ALLOCATION, default={}): ALLOCATION_SCHEMA,
            cv.Optional(CONF_SITE): SITE_SCHEMA,
            cv.Optional(CONF_CHARGERS, default=[]): cv.All(
                cv.ensure_list(CHARGER_SCHEMA), cv.Length(max=1), _validate_chargers
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _consume_ocpp_sockets,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    server = config[CONF_SERVER]
    cg.add(var.set_port(server[CONF_PORT]))
    cg.add(var.set_path(server[CONF_PATH]))
    allocation = config[CONF_ALLOCATION]
    cg.add(var.set_allocation_min_current(allocation[CONF_MIN_CURRENT]))
    cg.add(
        var.set_charging_profile_settle_time_per_amp(
            allocation[CONF_SETTLE_TIME_PER_AMP].total_milliseconds
        )
    )
    if site := config.get(CONF_SITE):
        cg.add(var.set_site(site[CONF_PHASES], site[CONF_VOLTAGE]))
        cg.add(var.set_site_energy_policy(site[CONF_POLICY]))
        if solar := site.get(CONF_SOLAR):
            if export_margin_power := solar.get(CONF_EXPORT_MARGIN_POWER):
                num = await number.new_number(
                    export_margin_power,
                    min_value=export_margin_power[CONF_MIN_VALUE],
                    max_value=export_margin_power[CONF_MAX_VALUE],
                    step=export_margin_power[CONF_STEP],
                )
                cg.add(
                    var.set_solar_export_margin_power_number(
                        num, export_margin_power[CONF_INITIAL_VALUE]
                    )
                )
        if headroom_current_config := site.get(CONF_HEADROOM_CURRENT):
            scalar_config, phase_configs = _split_drawn_current_config(headroom_current_config)
            if scalar_config:
                sens = await sensor.new_sensor(scalar_config)
                cg.add(var.set_site_headroom_current_max_sensor(sens))
            for index, phase in enumerate(PHASE_KEYS):
                if phase in phase_configs:
                    sens = await sensor.new_sensor(phase_configs[phase])
                    cg.add(var.set_site_headroom_current_sensor(index, sens))
        if drawn_current_config := site.get(CONF_DRAWN_CURRENT):
            scalar_config, phase_configs = _split_drawn_current_config(drawn_current_config)
            if scalar_config:
                sens = await sensor.new_sensor(scalar_config)
                cg.add(var.set_site_drawn_current_max_sensor(sens))
            for index, phase in enumerate(PHASE_KEYS):
                if phase in phase_configs:
                    sens = await sensor.new_sensor(phase_configs[phase])
                    cg.add(var.set_site_drawn_current_sensor(index, sens))
        if grid := site.get(CONF_GRID):
            cg.add(var.set_grid_max_power(grid[CONF_MAX_POWER]))
            if CONF_MAX_PHASE_IMBALANCE in grid:
                cg.add(var.set_grid_max_phase_imbalance(grid[CONF_MAX_PHASE_IMBALANCE]))
            cg.add(var.set_grid_max_current(grid[CONF_MAX_CURRENT]))
            if headroom_current_config := grid.get(CONF_HEADROOM_CURRENT):
                scalar_config, phase_configs = _split_drawn_current_config(headroom_current_config)
                if scalar_config:
                    sens = await sensor.new_sensor(scalar_config)
                    cg.add(var.set_grid_headroom_current_max_sensor(sens))
                for index, phase in enumerate(PHASE_KEYS):
                    if phase in phase_configs:
                        sens = await sensor.new_sensor(phase_configs[phase])
                        cg.add(var.set_grid_headroom_current_sensor(index, sens))
            if power := grid.get(CONF_POWER):
                if CONF_L1 in power:
                    sens = await cg.get_variable(power[CONF_L1])
                    cg.add(var.set_grid_power_l1_sensor(sens))
                if CONF_L2 in power:
                    sens = await cg.get_variable(power[CONF_L2])
                    cg.add(var.set_grid_power_l2_sensor(sens))
                if CONF_L3 in power:
                    sens = await cg.get_variable(power[CONF_L3])
                    cg.add(var.set_grid_power_l3_sensor(sens))
                if CONF_AGGREGATE in power:
                    sens = await cg.get_variable(power[CONF_AGGREGATE])
                    cg.add(var.set_grid_power_aggregate_sensor(sens))
        if storage := site.get(CONF_STORAGE):
            if CONF_CAPACITY in storage:
                cg.add(var.set_storage_capacity(storage[CONF_CAPACITY]))
            if power := storage.get(CONF_POWER):
                if CONF_L1 in power:
                    sens = await cg.get_variable(power[CONF_L1])
                    cg.add(var.set_storage_power_l1_sensor(sens))
                if CONF_L2 in power:
                    sens = await cg.get_variable(power[CONF_L2])
                    cg.add(var.set_storage_power_l2_sensor(sens))
                if CONF_L3 in power:
                    sens = await cg.get_variable(power[CONF_L3])
                    cg.add(var.set_storage_power_l3_sensor(sens))
                if CONF_AGGREGATE in power:
                    sens = await cg.get_variable(power[CONF_AGGREGATE])
                    cg.add(var.set_storage_power_aggregate_sensor(sens))
            if CONF_SOC in storage:
                sens = await cg.get_variable(storage[CONF_SOC])
                cg.add(var.set_storage_soc_sensor(sens))
            if CONF_ENERGY in storage:
                sens = await cg.get_variable(storage[CONF_ENERGY])
                cg.add(var.set_storage_energy_sensor(sens))
    for charger in config[CONF_CHARGERS]:
        cg.add(
            var.add_charger(
                charger[CONF_CHARGE_POINT_ID], charger[CONF_MAX_CURRENT], charger[CONF_PHASES]
            )
        )
        for index, site_phase in enumerate(charger[CONF_PHASE_MAPPING]):
            cg.add(
                var.set_charger_phase_mapping(
                    charger[CONF_CHARGE_POINT_ID], index, PHASE_TO_INDEX[site_phase]
                )
            )
        if drawn_current_source_config := charger.get(CONF_DRAWN_CURRENT_SOURCE):
            if isinstance(drawn_current_source_config, dict):
                for index, phase in enumerate(PHASE_KEYS):
                    sens = await cg.get_variable(drawn_current_source_config[phase])
                    cg.add(
                        var.set_charger_drawn_current_source_phase_sensor(
                            charger[CONF_CHARGE_POINT_ID], index, sens
                        )
                    )
            else:
                sens = await cg.get_variable(drawn_current_source_config)
                cg.add(
                    var.set_charger_drawn_current_source_sensor(
                        charger[CONF_CHARGE_POINT_ID], sens
                    )
                )
        if drawn_current_config := charger.get(CONF_DRAWN_CURRENT):
            sens = await sensor.new_sensor(drawn_current_config)
            cg.add(
                var.set_charger_drawn_current_sensor(
                    charger[CONF_CHARGE_POINT_ID], sens
                )
            )
        for connector in charger[CONF_CONNECTORS]:
            cg.add(
                var.add_connector(
                    charger[CONF_CHARGE_POINT_ID],
                    connector[CONF_ID],
                    connector[CONF_MAX_CURRENT],
                )
            )
            if current_config := connector.get(CONF_CURRENT):
                sens = await sensor.new_sensor(current_config)
                cg.add(
                    var.set_connector_current_sensor(
                        charger[CONF_CHARGE_POINT_ID], connector[CONF_ID], sens
                    )
                )
            if state_config := connector.get(CONF_STATE):
                sens = await text_sensor.new_text_sensor(state_config)
                cg.add(
                    var.set_connector_state_sensor(
                        charger[CONF_CHARGE_POINT_ID], connector[CONF_ID], sens
                    )
                )
            if available_current_config := connector.get(CONF_AVAILABLE_CURRENT):
                sens = await sensor.new_sensor(available_current_config)
                cg.add(
                    var.set_connector_available_current_sensor(
                        charger[CONF_CHARGE_POINT_ID], connector[CONF_ID], sens
                    )
                )
            if allocated_current_config := connector.get(CONF_ALLOCATED_CURRENT):
                sens = await sensor.new_sensor(allocated_current_config)
                cg.add(
                    var.set_connector_allocated_current_sensor(
                        charger[CONF_CHARGE_POINT_ID], connector[CONF_ID], sens
                    )
                )
            if drawn_current_config := connector.get(CONF_DRAWN_CURRENT):
                scalar_config, phase_configs = _split_drawn_current_config(drawn_current_config)
                if scalar_config:
                    sens = await sensor.new_sensor(scalar_config)
                    cg.add(
                        var.set_connector_drawn_current_max_sensor(
                            charger[CONF_CHARGE_POINT_ID], connector[CONF_ID], sens
                        )
                    )
                for index, phase in enumerate(PHASE_KEYS):
                    if phase in phase_configs:
                        sens = await sensor.new_sensor(phase_configs[phase])
                        cg.add(
                            var.set_connector_drawn_current_sensor(
                                charger[CONF_CHARGE_POINT_ID], connector[CONF_ID], index, sens
                            )
                        )
            if power_config := connector.get(CONF_POWER):
                sens = await sensor.new_sensor(power_config)
                cg.add(
                    var.set_connector_power_sensor(
                        charger[CONF_CHARGE_POINT_ID], connector[CONF_ID], sens
                    )
                )
            if limit_config := connector.get(CONF_CURRENT_LIMIT):
                num = await number.new_number(
                    limit_config,
                    min_value=limit_config[CONF_MIN_VALUE],
                    max_value=limit_config[CONF_MAX_VALUE],
                    step=limit_config[CONF_STEP],
                )
                await cg.register_component(num, limit_config)
                cg.add(num.set_initial_value(limit_config[CONF_INITIAL_VALUE]))
                cg.add(num.set_restore_value(limit_config[CONF_RESTORE_VALUE]))
                cg.add(
                    var.set_connector_current_limit_number(
                        charger[CONF_CHARGE_POINT_ID],
                        connector[CONF_ID],
                        num,
                        limit_config[CONF_INITIAL_VALUE],
                    )
                )
            if enabled_config := connector.get(CONF_ENABLED):
                sw = await switch.new_switch(enabled_config)
                cg.add(
                    var.set_connector_enabled_switch(
                        charger[CONF_CHARGE_POINT_ID], connector[CONF_ID], sw
                    )
                )
            if restart_config := connector.get(CONF_RESTART):
                btn = await button.new_button(restart_config)
                cg.add(
                    var.set_connector_restart_button(
                        charger[CONF_CHARGE_POINT_ID], connector[CONF_ID], btn
                    )
                )
    cg.add_define("USE_OCPP")
