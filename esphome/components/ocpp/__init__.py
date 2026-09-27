import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID

DEPENDENCIES = ["network"]
AUTO_LOAD = []

ocpp_ns = cg.esphome_ns.namespace("ocpp")
OcppComponent = ocpp_ns.class_("OcppComponent", cg.Component)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OcppComponent),
        }
    ).extend(cv.COMPONENT_SCHEMA),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # OCPP protocol backend, see llm-wiki/decisions.md for the library choice.
    # Not called from any code yet (see ocpp.cpp); this only wires up the
    # PlatformIO dependency so the library is fetched and compiled.
    cg.add_library("matth-x/MicroOcpp", "1.2.0")
    # This project builds with the ESP-IDF framework (see dev.yaml), so select
    # MicroOcpp's native ESP-IDF platform binding instead of its default
    # Arduino binding (MicroOcpp/Platform.h).
    cg.add_build_flag("-DMO_PLATFORM=MO_PLATFORM_ESPIDF")
    # MicroOcpp's ESP-IDF platform binding persists state on SPIFFS
    # (MicroOcpp/Core/FilesystemAdapter.cpp). ESPHome excludes the ESP-IDF
    # "spiffs" component by default, so re-include it. NOTE: this only makes the
    # driver available. Mounting still fails at runtime until a SPIFFS partition
    # labelled "mo" exists (esp32.add_partition) and directory support is enabled
    # (esp32.require_vfs_dir, for ftw_root's opendir/readdir). See
    # llm-wiki/decisions.md.
    include_builtin_idf_component("spiffs")
