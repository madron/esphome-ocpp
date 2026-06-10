import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT

DEPENDENCIES = ["network"]
AUTO_LOAD = ["json", "socket"]

CONF_CHARGE_POINTS = "charge_points"
CONF_SERVER = "server"
CONF_SERVER_PATH = "path"

ocpp_ns = cg.esphome_ns.namespace("ocpp")
OcppComponent = ocpp_ns.class_("OcppComponent", cg.Component)

#----------------------------------------------------------
# Server
#----------------------------------------------------------

def _validate_server_path(value):
    value = cv.string(value)
    if not value.startswith("/"):
        raise cv.Invalid("OCPP WebSocket path must start with '/'")
    return value.rstrip("/") or "/"


SERVER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PORT, default=9000): cv.port,
        cv.Optional(CONF_SERVER_PATH, default="/"): _validate_server_path,
    }
)


#----------------------------------------------------------
# Charge point
#----------------------------------------------------------

CHARGE_POINT_SCHEMA = cv.Schema(
    {
    }
)


#----------------------------------------------------------
# Site
#----------------------------------------------------------

def _consume_ocpp_sockets(config):
    from esphome.components import socket
    charge_point_count = len(config['charge_points'])
    # 1 socket for server and 1 for each client/charger
    # 1 more socket to debug a not yet configured charger
    socket_count = 1 + charge_point_count + 1
    socket.consume_sockets(socket_count, "ocpp")(config)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OcppComponent),
            cv.Optional(CONF_SERVER, default={}): SERVER_SCHEMA,
            cv.Optional(CONF_CHARGE_POINTS, default=[]): cv.ensure_list(CHARGE_POINT_SCHEMA),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _consume_ocpp_sockets,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # server
    server = config[CONF_SERVER]
    cg.add(var.set_server_port(server[CONF_PORT]))
    cg.add(var.set_server_path(server[CONF_SERVER_PATH]))
