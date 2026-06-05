import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, number, sensor, switch
from esphome.const import (
    CONF_CURRENT,
    CONF_ID,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_PORT,
    CONF_POWER,
    CONF_STEP,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_WATT,
)

DEPENDENCIES = ["network"]
AUTO_LOAD = ["json", "socket", "sensor", "number", "button", "switch"]

CONF_CHARGE_POINT_ID = "charge_point_id"
CONF_CHARGERS = "chargers"
CONF_CONNECTORS = "connectors"
CONF_CURRENT_LIMIT = "current_limit"
CONF_GRID = "grid"
CONF_L1 = "l1"
CONF_L2 = "l2"
CONF_L3 = "l3"
CONF_AGGREGATE = "aggregate"
CONF_MAX_CURRENT = "max_current"
CONF_MAX_PHASE_IMBALANCE = "max_phase_imbalance"
CONF_MAX_POWER = "max_power"
CONF_PHASES = "phases"
CONF_ENABLED = "enabled"
CONF_RESTART = "restart"
CONF_SERVER = "server"
CONF_SITE = "site"
CONF_PATH = "path"
CONF_VOLTAGE = "voltage"

ocpp_ns = cg.esphome_ns.namespace("ocpp")
OcppServer = ocpp_ns.class_("OcppServer", cg.Component)
OcppConnectorButton = ocpp_ns.class_("OcppConnectorButton", button.Button)
OcppConnectorEnabledSwitch = ocpp_ns.class_("OcppConnectorEnabledSwitch", switch.Switch)
OcppCurrentLimitNumber = ocpp_ns.class_("OcppCurrentLimitNumber", number.Number)


def _validate_path(value):
    value = cv.string(value)
    if not value.startswith("/"):
        raise cv.Invalid("OCPP WebSocket path must start with '/'")
    return value.rstrip("/") or "/"


def _consume_ocpp_sockets(config):
    from esphome.components import socket

    socket.consume_sockets(2, "ocpp")(config)
    return config


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
                if limit[CONF_MAX_VALUE] > effective_max_current:
                    raise cv.Invalid(
                        f"current_limit max_value for connector '{connector_id}' must not exceed the effective max_current"
                    )
                if limit[CONF_MIN_VALUE] >= limit[CONF_MAX_VALUE]:
                    raise cv.Invalid(
                        f"current_limit min_value for connector '{connector_id}' must be less than max_value"
                    )
    return value


def _validate_site(value):
    phases = value[CONF_PHASES]
    grid = value.get(CONF_GRID, {})
    power = grid.get(CONF_POWER, {})
    has_aggregate = CONF_AGGREGATE in power
    phase_keys = (CONF_L1, CONF_L2, CONF_L3)
    configured_phases = [phase for phase in phase_keys if phase in power]

    if phases == 1:
        if CONF_L2 in power or CONF_L3 in power:
            raise cv.Invalid("single-phase sites may only configure grid.power.l1")
        if has_aggregate:
            raise cv.Invalid("single-phase sites should use grid.power.l1 instead of grid.power.aggregate")
    elif configured_phases and len(configured_phases) != 3:
        raise cv.Invalid("three-phase sites must configure all of grid.power.l1, l2 and l3 together")

    if has_aggregate and configured_phases:
        raise cv.Invalid("grid.power.aggregate must not be combined with per-phase grid.power sensors")
    return value


SERVER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PORT, default=9000): cv.port,
        cv.Optional(CONF_PATH, default="/ocpp"): _validate_path,
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

GRID_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MAX_POWER): cv.positive_float,
        cv.Optional(CONF_MAX_PHASE_IMBALANCE): cv.positive_float,
        cv.Required(CONF_MAX_CURRENT): cv.positive_float,
        cv.Optional(CONF_POWER): GRID_POWER_SCHEMA,
    }
)

SITE_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_PHASES): cv.one_of(1, 3, int=True),
            cv.Required(CONF_VOLTAGE): cv.positive_float,
            cv.Optional(CONF_GRID): GRID_SCHEMA,
        }
    ),
    _validate_site,
)

CONNECTOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.int_range(min=1, max=255),
        cv.Optional(CONF_MAX_CURRENT): cv.positive_float,
        cv.Optional(CONF_CURRENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
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
        cv.Required(CONF_CONNECTORS): cv.All(cv.ensure_list(CONNECTOR_SCHEMA), cv.Length(min=1, max=1)),
    }
)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OcppServer),
            cv.Optional(CONF_SERVER, default={}): SERVER_SCHEMA,
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
    if site := config.get(CONF_SITE):
        cg.add(var.set_site(site[CONF_PHASES], site[CONF_VOLTAGE]))
        if grid := site.get(CONF_GRID):
            if CONF_MAX_POWER in grid:
                cg.add(var.set_grid_max_power(grid[CONF_MAX_POWER]))
            if CONF_MAX_PHASE_IMBALANCE in grid:
                cg.add(var.set_grid_max_phase_imbalance(grid[CONF_MAX_PHASE_IMBALANCE]))
            cg.add(var.set_grid_max_current(grid[CONF_MAX_CURRENT]))
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
    for charger in config[CONF_CHARGERS]:
        cg.add(var.add_charger(charger[CONF_CHARGE_POINT_ID], charger[CONF_MAX_CURRENT]))
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
                cg.add(
                    var.set_connector_current_limit_number(
                        charger[CONF_CHARGE_POINT_ID],
                        connector[CONF_ID],
                        num,
                        limit_config[CONF_MIN_VALUE],
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
