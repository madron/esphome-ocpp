#include "assertions.cpp"
#include "esphome/components/ocpp/message.h"

#include <cmath>
#include <string>
#include <vector>

using esphome::ocpp::OcppMessage;
using esphome::ocpp::OcppMessageType;
using esphome::ocpp::OcppCall;
using esphome::ocpp::Authorize;
using esphome::ocpp::BootNotification;
using esphome::ocpp::MeterValues;
using esphome::ocpp::SampledValue;
using esphome::ocpp::StartTransaction;
using esphome::ocpp::StatusNotification;
using esphome::ocpp::StopTransaction;

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

    Authorize authorize("authorize-1", "free");
    assert_equal("authorize_message_type", static_cast<int>(authorize.message_type_id), 2);
    assert_equal("authorize_unique_id", authorize.unique_id, std::string("authorize-1"));
    assert_equal("authorize_action", authorize.action, std::string("Authorize"));
    assert_equal("authorize_id_tag", authorize.id_tag, std::string("free"));

    StartTransaction start_transaction("start-1", 2, "free");
    assert_equal("start_transaction_message_type", static_cast<int>(start_transaction.message_type_id), 2);
    assert_equal("start_transaction_unique_id", start_transaction.unique_id, std::string("start-1"));
    assert_equal("start_transaction_action", start_transaction.action, std::string("StartTransaction"));
    assert_equal("start_transaction_connector_id", start_transaction.connector_id, 2U);
    assert_equal("start_transaction_id_tag", start_transaction.id_tag, std::string("free"));

    StopTransaction stop_transaction("stop-1", 42);
    assert_equal("stop_transaction_message_type", static_cast<int>(stop_transaction.message_type_id), 2);
    assert_equal("stop_transaction_unique_id", stop_transaction.unique_id, std::string("stop-1"));
    assert_equal("stop_transaction_action", stop_transaction.action, std::string("StopTransaction"));
    assert_equal("stop_transaction_transaction_id", stop_transaction.transaction_id, 42U);

    StatusNotification status_notification("status-1", 2, "NoError", "Available");
    assert_equal("status_notification_message_type", static_cast<int>(status_notification.message_type_id), 2);
    assert_equal("status_notification_unique_id", status_notification.unique_id, std::string("status-1"));
    assert_equal("status_notification_action", status_notification.action, std::string("StatusNotification"));
    assert_equal("status_notification_connector_id", status_notification.connector_id, 2U);
    assert_equal("status_notification_error_code", status_notification.error_code, std::string("NoError"));
    assert_equal("status_notification_status", status_notification.status, std::string("Available"));

    StatusNotification default_status_notification;
    assert_equal("default_status_notification_unique_id", default_status_notification.unique_id, std::string(""));
    assert_equal("default_status_notification_action", default_status_notification.action, std::string("StatusNotification"));
    assert_equal("default_status_notification_connector_id", default_status_notification.connector_id, 1U);
    assert_equal("default_status_notification_error_code", default_status_notification.error_code, std::string(""));
    assert_equal("default_status_notification_status", default_status_notification.status, std::string(""));

    std::vector<SampledValue> sampled_values = {
        SampledValue(16.0f, "Current.Import", "A", "L1"),
        SampledValue(3.68f, "Power.Active.Import", "kW"),
        SampledValue(12500.0f, "Energy.Active.Import.Register", "Wh"),
        SampledValue(230.0f, "Voltage", "V", "L1"),
    };
    MeterValues meter_values("meter-1", 2, sampled_values);
    assert_equal("meter_values_message_type", static_cast<int>(meter_values.message_type_id), 2);
    assert_equal("meter_values_unique_id", meter_values.unique_id, std::string("meter-1"));
    assert_equal("meter_values_action", meter_values.action, std::string("MeterValues"));
    assert_equal("meter_values_connector_id", meter_values.connector_id, 2U);
    assert_equal("meter_values_current", meter_values.current, 16.0f);
    assert_equal("meter_values_power", meter_values.power, 3680.0f);
    assert_equal("meter_values_energy", meter_values.energy, 12.5f);
    assert_equal("meter_values_voltage", meter_values.voltage, 230.0f);
    assert_equal("meter_values_sample_count", meter_values.sampled_values.size(), 4U);
    assert_equal("meter_values_first_phase", meter_values.sampled_values[0].phase, std::string("L1"));
    assert_equal("meter_values_summary", meter_values.sampled_values_summary(),
                 std::string("Current: L1=16 A - Power: 3.68 kW - Energy: 12500 Wh - Voltage: L1=230 V"));

    MeterValues default_meter_values;
    assert_equal("default_meter_values_unique_id", default_meter_values.unique_id, std::string(""));
    assert_equal("default_meter_values_action", default_meter_values.action, std::string("MeterValues"));
    assert_equal("default_meter_values_connector_id", default_meter_values.connector_id, 1U);
    assert_equal("default_meter_values_current_nan", std::isnan(default_meter_values.current), true);
    assert_equal("default_meter_values_power_nan", std::isnan(default_meter_values.power), true);
    assert_equal("default_meter_values_energy_nan", std::isnan(default_meter_values.energy), true);
    assert_equal("default_meter_values_voltage_nan", std::isnan(default_meter_values.voltage), true);
    assert_equal("default_meter_values_sample_count", default_meter_values.sampled_values.size(), 0U);
    assert_equal("default_meter_values_summary", default_meter_values.sampled_values_summary(), std::string(""));
}
