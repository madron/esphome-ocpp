import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, number, sensor, switch, text_sensor
from esphome.const import (
    CONF_CURRENT,
    CONF_ID,
    CONF_INITIAL_VALUE,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_PORT,
    CONF_RESTORE_VALUE,
    CONF_STATE,
    CONF_STEP,
    DEVICE_CLASS_CURRENT,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
)

DEPENDENCIES = ["network"]
AUTO_LOAD = ["json", "socket", "sensor", "number", "button", "switch", "text_sensor"]

CONF_CHARGE_POINT_ID = "charge_point_id"
CONF_CHARGERS = "chargers"
CONF_CONNECTORS = "connectors"
CONF_ALLOCATION = "allocation"
CONF_CURRENT_LIMIT = "current_limit"
CONF_L1 = "l1"
CONF_L2 = "l2"
CONF_L3 = "l3"
CONF_MAX_CURRENT = "max_current"
CONF_MIN_CURRENT = "min_current"
CONF_PHASE_MAPPING = "phase_mapping"
CONF_PHASES = "phases"
CONF_ENABLED = "enabled"
CONF_RESTART = "restart"
CONF_SERVER = "server"
CONF_SITE = "site"
CONF_PATH = "path"
CONF_STRATEGY = "strategy"
CONF_VOLTAGE = "voltage"

ocpp_ns = cg.esphome_ns.namespace("ocpp")
OcppServer = ocpp_ns.class_("OcppServer", cg.Component)
OcppConnectorButton = ocpp_ns.class_("OcppConnectorButton", button.Button)
OcppConnectorEnabledSwitch = ocpp_ns.class_("OcppConnectorEnabledSwitch", switch.Switch)
OcppCurrentLimitNumber = ocpp_ns.class_("OcppCurrentLimitNumber", number.Number, cg.Component)

PHASE_TO_INDEX = {CONF_L1.upper(): 0, CONF_L2.upper(): 1, CONF_L3.upper(): 2}


CURRENT_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_AMPERE,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_CURRENT,
    state_class=STATE_CLASS_MEASUREMENT,
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
    }
)

SITE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_PHASES): cv.one_of(1, 3, int=True),
        cv.Required(CONF_VOLTAGE): cv.positive_float,
    }
)

CONNECTOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.int_range(min=1, max=255),
        cv.Optional(CONF_MAX_CURRENT): cv.positive_float,
        cv.Optional(CONF_CURRENT): CURRENT_SENSOR_SCHEMA,
        cv.Optional(CONF_STATE): text_sensor.text_sensor_schema(),
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
        cv.Required(CONF_CONNECTORS): cv.All(cv.ensure_list(CONNECTOR_SCHEMA), cv.Length(min=1)),
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
    if site := config.get(CONF_SITE):
        cg.add(var.set_site(site[CONF_PHASES], site[CONF_VOLTAGE]))
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
