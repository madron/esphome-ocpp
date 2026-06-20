#include "assertions.cpp"
#include "esphome/components/ocpp/message.h"

#include <cmath>
#include <string>

using esphome::ocpp::OcppMessage;
using esphome::ocpp::OcppMessageType;
using esphome::ocpp::OcppCall;
using esphome::ocpp::BootNotification;
using esphome::ocpp::MeterValues;

int main() {
    OcppMessage call(OcppMessageType::CALL, "call-1");
    assert_equal("call_message_type", static_cast<int>(call.message_type_id), 2);
    assert_equal("call_unique_id", call.unique_id, std::string("call-1"));
    assert_equal("call_action_defaults_empty", call.action, std::string(""));

    OcppMessage base_call_with_action(OcppMessageType::CALL, "call-2", "Heartbeat");
    assert_equal("base_call_with_action", base_call_with_action.action, std::string("Heartbeat"));

    OcppMessage result(OcppMessageType::CALL_RESULT, "result-1");
    assert_equal("result_message_type", static_cast<int>(result.message_type_id), 3);
    assert_equal("result_unique_id", result.unique_id, std::string("result-1"));
    assert_equal("result_action_defaults_empty", result.action, std::string(""));

    OcppMessage error(OcppMessageType::CALL_ERROR, "error-1");
    assert_equal("error_message_type", static_cast<int>(error.message_type_id), 4);
    assert_equal("error_unique_id", error.unique_id, std::string("error-1"));
    assert_equal("error_action_defaults_empty", error.action, std::string(""));

    OcppMessage default_message(OcppMessageType::CALL_RESULT);
    assert_equal("default_message_unique_id", default_message.unique_id, std::string(""));
    assert_equal("default_message_action", default_message.action, std::string(""));

    OcppCall generic_call("SomeAction", "generic-call-1");
    assert_equal("generic_call_message_type", static_cast<int>(generic_call.message_type_id), 2);
    assert_equal("generic_call_unique_id", generic_call.unique_id, std::string("generic-call-1"));
    assert_equal("generic_call_action", generic_call.action, std::string("SomeAction"));

    OcppCall default_call("DefaultAction");
    assert_equal("default_call_unique_id", default_call.unique_id, std::string(""));
    assert_equal("default_call_action", default_call.action, std::string("DefaultAction"));

    BootNotification boot_notification("boot-1", "Wallbox", "Acme", "1.2.3");
    assert_equal("boot_notification_message_type", static_cast<int>(boot_notification.message_type_id), 2);
    assert_equal("boot_notification_unique_id", boot_notification.unique_id, std::string("boot-1"));
    assert_equal("boot_notification_action", boot_notification.action, std::string("BootNotification"));
    assert_equal("boot_notification_model", boot_notification.charge_point_model, std::string("Wallbox"));
    assert_equal("boot_notification_vendor", boot_notification.charge_point_vendor, std::string("Acme"));
    assert_equal("boot_notification_firmware", boot_notification.firmware_version, std::string("1.2.3"));

    BootNotification default_boot_notification;
    assert_equal("default_boot_notification_message_type", static_cast<int>(default_boot_notification.message_type_id), 2);
    assert_equal("default_boot_notification_unique_id", default_boot_notification.unique_id, std::string(""));
    assert_equal("default_boot_notification_action", default_boot_notification.action, std::string("BootNotification"));
    assert_equal("default_boot_notification_model", default_boot_notification.charge_point_model, std::string(""));
    assert_equal("default_boot_notification_vendor", default_boot_notification.charge_point_vendor, std::string(""));
    assert_equal("default_boot_notification_firmware", default_boot_notification.firmware_version, std::string(""));

    MeterValues meter_values("meter-1", 2, 16.0f, 3680.0f, 12.5f, 230.0f);
    assert_equal("meter_values_message_type", static_cast<int>(meter_values.message_type_id), 2);
    assert_equal("meter_values_unique_id", meter_values.unique_id, std::string("meter-1"));
    assert_equal("meter_values_action", meter_values.action, std::string("MeterValues"));
    assert_equal("meter_values_connector_id", meter_values.connector_id, 2U);
    assert_equal("meter_values_current", meter_values.current, 16.0f);
    assert_equal("meter_values_power", meter_values.power, 3680.0f);
    assert_equal("meter_values_energy", meter_values.energy, 12.5f);
    assert_equal("meter_values_voltage", meter_values.voltage, 230.0f);

    MeterValues default_meter_values;
    assert_equal("default_meter_values_unique_id", default_meter_values.unique_id, std::string(""));
    assert_equal("default_meter_values_action", default_meter_values.action, std::string("MeterValues"));
    assert_equal("default_meter_values_connector_id", default_meter_values.connector_id, 1U);
    assert_equal("default_meter_values_current_nan", std::isnan(default_meter_values.current), true);
    assert_equal("default_meter_values_power_nan", std::isnan(default_meter_values.power), true);
    assert_equal("default_meter_values_energy_nan", std::isnan(default_meter_values.energy), true);
    assert_equal("default_meter_values_voltage_nan", std::isnan(default_meter_values.voltage), true);
}
