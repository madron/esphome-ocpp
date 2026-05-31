import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number, sensor
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
AUTO_LOAD = ["json", "socket", "sensor", "number"]

CONF_CHARGE_POINT_ID = "charge_point_id"
CONF_CHARGERS = "chargers"
CONF_CONNECTORS = "connectors"
CONF_CURRENT_LIMIT = "current_limit"
CONF_MAX_CURRENT = "max_current"
CONF_SERVER = "server"
CONF_PATH = "path"

ocpp_ns = cg.esphome_ns.namespace("ocpp")
OcppServer = ocpp_ns.class_("OcppServer", cg.Component)
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

            if limit := connector.get(CONF_CURRENT_LIMIT):
                limit.setdefault(CONF_MAX_VALUE, connector[CONF_MAX_CURRENT])
                if limit[CONF_MAX_VALUE] > connector[CONF_MAX_CURRENT]:
                    raise cv.Invalid(
                        f"current_limit max_value for connector '{connector_id}' must not exceed max_current"
                    )
                if limit[CONF_MIN_VALUE] >= limit[CONF_MAX_VALUE]:
                    raise cv.Invalid(
                        f"current_limit min_value for connector '{connector_id}' must be less than max_value"
                    )
    return value


SERVER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PORT, default=9000): cv.port,
        cv.Optional(CONF_PATH, default="/ocpp"): _validate_path,
    }
)

CONNECTOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.int_range(min=1, max=255),
        cv.Required(CONF_MAX_CURRENT): cv.positive_float,
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
    }
)

CHARGER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.string_strict,
        cv.Required(CONF_CHARGE_POINT_ID): cv.string_strict,
        cv.Required(CONF_CONNECTORS): cv.All(cv.ensure_list(CONNECTOR_SCHEMA), cv.Length(min=1, max=1)),
    }
)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OcppServer),
            cv.Optional(CONF_SERVER, default={}): SERVER_SCHEMA,
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
    for charger in config[CONF_CHARGERS]:
        cg.add(var.add_charger(charger[CONF_CHARGE_POINT_ID]))
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
    cg.add_define("USE_OCPP")
