import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, text_sensor
from esphome.const import CONF_ID, CONF_PORT

DEPENDENCIES = ["network"]
AUTO_LOAD = ["binary_sensor", "json", "socket", "text_sensor"]

CONF_CHARGE_POINT_ID = "charge_point_id"
CONF_CHARGE_POINTS = "charge_points"
CONF_DEBUG_OCPP_MESSAGES = "debug_ocpp_messages"
CONF_FORCE_BOOT_NOTIFICATION = "force_boot_notification"
CONF_FORCE_PROTOCOL = "force_protocol"
CONF_ONLINE = "online"
CONF_PROTOCOL = "protocol"
CONF_SERVER = "server"
CONF_SERVER_PATH = "path"

SUPPORTED_PROTOCOLS = ["ocpp1.6"]

ocpp_ns = cg.esphome_ns.namespace("ocpp")
OcppComponent = ocpp_ns.class_("OcppComponent", cg.Component)
ChargePoint = ocpp_ns.class_("ChargePoint")

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
        cv.Optional(CONF_DEBUG_OCPP_MESSAGES, default=False): cv.boolean,
        cv.Optional(CONF_FORCE_BOOT_NOTIFICATION, default=False): cv.boolean,
        cv.Optional(CONF_FORCE_PROTOCOL): validate_protocol,
        cv.Optional(CONF_ONLINE): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_PROTOCOL): text_sensor.text_sensor_schema(),
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
        if CONF_ONLINE in charge_point_conf:
            sens = await binary_sensor.new_binary_sensor(charge_point_conf[CONF_ONLINE])
            cg.add(charge_point.set_online_binary_sensor(sens))
        cg.add(charge_point.set_debug_ocpp_messages(charge_point_conf[CONF_DEBUG_OCPP_MESSAGES]))
        cg.add(charge_point.set_force_boot_notification(charge_point_conf[CONF_FORCE_BOOT_NOTIFICATION]))
        cg.add(var.add_charge_point(charge_point))
