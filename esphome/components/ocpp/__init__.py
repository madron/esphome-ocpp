import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, text_sensor
from esphome.const import CONF_ID, CONF_PORT

DEPENDENCIES = ["network"]
AUTO_LOAD = ["binary_sensor", "json", "sensor", "socket", "text_sensor"]

CONF_CHARGE_POINT_ID = "charge_point_id"
CONF_CHARGE_POINTS = "charge_points"
CONF_CONNECTOR_ID = "connector_id"
CONF_CONNECTORS = "connectors"
CONF_CURRENT = "current"
CONF_DEBUG_OCPP_MESSAGES = "debug_ocpp_messages"
CONF_ENERGY = "energy"
CONF_FORCE_PROTOCOL = "force_protocol"
CONF_CHARGER_INFO = "charger_info"
CONF_ONLINE = "online"
CONF_POWER = "power"
CONF_PROTOCOL = "protocol"
CONF_SERVER = "server"
CONF_SERVER_PATH = "path"
CONF_STARTUP_NOTIFICATIONS_DELAY = "startup_notifications_delay"
CONF_VOLTAGE = "voltage"

SUPPORTED_PROTOCOLS = ["ocpp1.6", "ocpp2.0.1"]

ocpp_ns = cg.esphome_ns.namespace("ocpp")
OcppComponent = ocpp_ns.class_("OcppComponent", cg.Component)
ChargePoint = ocpp_ns.class_("ChargePoint")
Connector = ocpp_ns.class_("Connector")

#----------------------------------------------------------
# Server
#----------------------------------------------------------

def validate_server_path(value):
    value = cv.string(value)
    if not value.startswith("/"):
        raise cv.Invalid("path must start with '/'")
    return value.rstrip("/") or "/"


SERVER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PORT, default=9000): cv.port,
        cv.Optional(CONF_SERVER_PATH, default="/"): validate_server_path,
    }
)


#----------------------------------------------------------
# Connector
#----------------------------------------------------------

CONNECTOR_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Connector),
        cv.Optional(CONF_CONNECTOR_ID, default=1): cv.int_range(min=0),
        cv.Optional(CONF_CURRENT): sensor.sensor_schema(
            unit_of_measurement="A",
            accuracy_decimals=1,
            device_class="current",
            state_class="measurement",
        ),
        cv.Optional(CONF_ENERGY): sensor.sensor_schema(
            unit_of_measurement="kWh",
            accuracy_decimals=3,
            device_class="energy",
            state_class="total_increasing",
        ),
        cv.Optional(CONF_POWER): sensor.sensor_schema(
            unit_of_measurement="W",
            accuracy_decimals=0,
            device_class="power",
            state_class="measurement",
        ),
        cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
            unit_of_measurement="V",
            accuracy_decimals=1,
            device_class="voltage",
            state_class="measurement",
        ),
    }
)


#----------------------------------------------------------
# Charge point
#----------------------------------------------------------

def validate_charge_point_id(value):
    value = cv.string(value).strip()
    if not value:
        raise cv.Invalid("charge_point_id must not be empty")
    if "/" in value:
        raise cv.Invalid("charge_point_id must not contain '/'")
    return value


def validate_protocol(value):
    value = cv.string(value).strip()
    if value not in SUPPORTED_PROTOCOLS:
        supported = ", ".join(SUPPORTED_PROTOCOLS)
        raise cv.Invalid(f"force_protocol must be one of: {supported}")
    return value


CHARGE_POINT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ChargePoint),
        cv.Optional(CONF_CHARGE_POINT_ID): validate_charge_point_id,
        cv.Optional(CONF_CONNECTORS, default=[{}]): cv.ensure_list(CONNECTOR_SCHEMA),
        cv.Optional(CONF_DEBUG_OCPP_MESSAGES, default=False): cv.boolean,
        cv.Optional(CONF_FORCE_PROTOCOL): validate_protocol,
        cv.Optional(CONF_CHARGER_INFO): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_ONLINE): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_PROTOCOL): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_STARTUP_NOTIFICATIONS_DELAY, default=300): cv.int_range(min=0, max=4294967),
    }
)


#----------------------------------------------------------
# Site
#----------------------------------------------------------

def consume_sockets(config):
    from esphome.components import socket
    # 1 socket for server and 1 for each client/charge_point
    socket_count = 1 + len(config[CONF_CHARGE_POINTS])
    socket.consume_sockets(socket_count, "ocpp")(config)
    return config


def validate_charge_points(config):
    charge_point_ids = set()
    for charge_point in config[CONF_CHARGE_POINTS]:
        connector_ids = set()
        for connector in charge_point[CONF_CONNECTORS]:
            connector_id = connector[CONF_CONNECTOR_ID]
            if connector_id in connector_ids:
                raise cv.Invalid(f"Duplicate connector_id '{connector_id}' in charge point")
            connector_ids.add(connector_id)
        if CONF_CHARGE_POINT_ID not in charge_point:
            continue
        charge_point_id = charge_point[CONF_CHARGE_POINT_ID]
        if charge_point_id in charge_point_ids:
            raise cv.Invalid(f"Duplicate charge_point_id '{charge_point_id}'")
        charge_point_ids.add(charge_point_id)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OcppComponent),
            cv.Optional(CONF_SERVER, default={}): SERVER_SCHEMA,
            cv.Optional(CONF_CHARGE_POINTS, default=[]): cv.ensure_list(CHARGE_POINT_SCHEMA),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    consume_sockets,
    validate_charge_points,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # server
    server = config[CONF_SERVER]
    cg.add(var.set_server_port(server[CONF_PORT]))
    cg.add(var.set_server_path(server[CONF_SERVER_PATH]))

    # charge_points
    for charge_point_conf in config[CONF_CHARGE_POINTS]:
        charge_point = cg.new_Pvariable(charge_point_conf[CONF_ID])
        if CONF_CHARGE_POINT_ID in charge_point_conf:
            cg.add(charge_point.set_charge_point_id(charge_point_conf[CONF_CHARGE_POINT_ID]))
        if CONF_FORCE_PROTOCOL in charge_point_conf:
            cg.add(charge_point.set_force_protocol(charge_point_conf[CONF_FORCE_PROTOCOL]))
        if CONF_PROTOCOL in charge_point_conf:
            sens = await text_sensor.new_text_sensor(charge_point_conf[CONF_PROTOCOL])
            cg.add(charge_point.set_protocol_text_sensor(sens))
        if CONF_CHARGER_INFO in charge_point_conf:
            sens = await text_sensor.new_text_sensor(charge_point_conf[CONF_CHARGER_INFO])
            cg.add(charge_point.set_charger_info_text_sensor(sens))
        if CONF_ONLINE in charge_point_conf:
            sens = await binary_sensor.new_binary_sensor(charge_point_conf[CONF_ONLINE])
            cg.add(charge_point.set_online_binary_sensor(sens))
        for connector_conf in charge_point_conf[CONF_CONNECTORS]:
            connector = cg.new_Pvariable(connector_conf[CONF_ID])
            cg.add(connector.set_connector_id(connector_conf[CONF_CONNECTOR_ID]))
            if CONF_CURRENT in connector_conf:
                sens = await sensor.new_sensor(connector_conf[CONF_CURRENT])
                cg.add(connector.set_current_sensor(sens))
            if CONF_POWER in connector_conf:
                sens = await sensor.new_sensor(connector_conf[CONF_POWER])
                cg.add(connector.set_power_sensor(sens))
            if CONF_ENERGY in connector_conf:
                sens = await sensor.new_sensor(connector_conf[CONF_ENERGY])
                cg.add(connector.set_energy_sensor(sens))
            if CONF_VOLTAGE in connector_conf:
                sens = await sensor.new_sensor(connector_conf[CONF_VOLTAGE])
                cg.add(connector.set_voltage_sensor(sens))
            cg.add(charge_point.add_connector(connector))
        cg.add(charge_point.set_debug_ocpp_messages(charge_point_conf[CONF_DEBUG_OCPP_MESSAGES]))
        cg.add(charge_point.set_startup_notifications_delay(charge_point_conf[CONF_STARTUP_NOTIFICATIONS_DELAY] * 1000))
        cg.add(var.add_charge_point(charge_point))
