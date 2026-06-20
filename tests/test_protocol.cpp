#include "assertions.cpp"
#include "esphome/components/ocpp/message.h"
#include "esphome/components/ocpp/protocol.h"

#include <cmath>
#include <memory>
#include <string>

using esphome::ocpp::BootNotification;
using esphome::ocpp::ChangeConfigurationResponse;
using esphome::ocpp::GetConfigurationResponse;
using esphome::ocpp::MeterValues;
using esphome::ocpp::OcppMessage;
using esphome::ocpp::OcppMessageType;
using esphome::ocpp::OcppProtocol;
using esphome::ocpp::OcppProtocolVersion;

int main() {
    OcppProtocol protocol;
    assert_equal("default_version", protocol.get_version() == OcppProtocolVersion::OCPP_1_6, true);

    std::unique_ptr<OcppMessage> ocpp_16_message = protocol.parse_message(
        R"([2,"boot-1","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox","firmwareVersion":"1.2.3"}])"
    );
    assert_equal("ocpp16_message_exists", ocpp_16_message != nullptr, true);
    auto *ocpp_16_boot = dynamic_cast<BootNotification *>(ocpp_16_message.get());
    assert_equal("ocpp16_boot_exists", ocpp_16_boot != nullptr, true);
    assert_equal("ocpp16_boot_unique_id", ocpp_16_boot->unique_id, std::string("boot-1"));
    assert_equal("ocpp16_boot_model", ocpp_16_boot->charge_point_model, std::string("Wallbox"));
    assert_equal("ocpp16_boot_vendor", ocpp_16_boot->charge_point_vendor, std::string("Acme"));
    assert_equal("ocpp16_boot_firmware", ocpp_16_boot->firmware_version, std::string("1.2.3"));

    assert_equal("set_ocpp201", protocol.set_websocket_protocol("ocpp2.0.1"), true);
    assert_equal("ocpp201_version", protocol.get_version() == OcppProtocolVersion::OCPP_2_0_1, true);
    std::unique_ptr<OcppMessage> ocpp_201_message = protocol.parse_message(
        R"([2,"boot-2","BootNotification",{"chargingStation":{"model":"Prism Solar","vendorName":"Silla Industries","firmwareVersion":"3.2.77"},"reason":"PowerUp"}])"
    );
    assert_equal("ocpp201_message_exists", ocpp_201_message != nullptr, true);
    auto *ocpp_201_boot = dynamic_cast<BootNotification *>(ocpp_201_message.get());
    assert_equal("ocpp201_boot_exists", ocpp_201_boot != nullptr, true);
    assert_equal("ocpp201_boot_unique_id", ocpp_201_boot->unique_id, std::string("boot-2"));
    assert_equal("ocpp201_boot_model", ocpp_201_boot->charge_point_model, std::string("Prism Solar"));
    assert_equal("ocpp201_boot_vendor", ocpp_201_boot->charge_point_vendor, std::string("Silla Industries"));
    assert_equal("ocpp201_boot_firmware", ocpp_201_boot->firmware_version, std::string("3.2.77"));

    std::unique_ptr<OcppMessage> call_result = protocol.parse_message(R"([3,"result-1",{}])");
    assert_equal("call_result_exists", call_result != nullptr, true);
    assert_equal("call_result_type", static_cast<int>(call_result->message_type_id), 3);
    assert_equal("call_result_unique_id", call_result->unique_id, std::string("result-1"));
    assert_equal("call_result_action", call_result->action, std::string(""));

    assert_equal("invalid_json_rejected", protocol.parse_message("{") == nullptr, true);
    assert_equal("invalid_top_level_rejected", protocol.parse_message(R"({"messageTypeId":2})") == nullptr, true);
    assert_equal("invalid_message_type_rejected", protocol.parse_message(R"([9,"bad",{}])") == nullptr, true);
    assert_equal("invalid_call_payload_rejected", protocol.parse_message(R"([2,"bad","BootNotification",[]])") == nullptr, true);

    assert_equal("unsupported_protocol", protocol.set_websocket_protocol("ocpp9.9"), false);
    assert_equal("reset_ocpp16", protocol.set_websocket_protocol("ocpp1.6"), true);
    assert_equal("change_configuration_request",
                 protocol.make_change_configuration_request("change-config-meter-values-sampled-data",
                                                            "MeterValuesSampledData",
                                                            "Current.Import,Power.Active.Import"),
                 R"([2,"change-config-meter-values-sampled-data","ChangeConfiguration",{"key":"MeterValuesSampledData","value":"Current.Import,Power.Active.Import"}])");
    assert_equal("get_configuration_request", protocol.make_get_configuration_request("get-configuration"),
                 R"([2,"get-configuration","GetConfiguration",{"key":["MeterValueSampleInterval","MeterValuesSampledData","ConnectorSwitch3to1PhaseSupported"]}])");
    std::unique_ptr<OcppMessage> get_configuration_message = protocol.parse_message(
        R"([3,"get-configuration",{"configurationKey":[{"key":"MeterValueSampleInterval","readonly":false,"value":"5"},{"key":"MeterValuesSampledData","readonly":false,"value":"Power.Active.Import,Current.Import,Voltage"},{"key":"ConnectorSwitch3to1PhaseSupported","readonly":true,"value":"false"}]}])"
    );
    auto *get_configuration_response = dynamic_cast<GetConfigurationResponse *>(get_configuration_message.get());
    assert_equal("get_configuration_response_exists", get_configuration_response != nullptr, true);
    assert_equal("get_configuration_response_interval", get_configuration_response->meter_value_sample_interval,
                 std::string("5"));
    assert_equal("get_configuration_response_sampled_data", get_configuration_response->meter_values_sampled_data,
                 std::string("Power.Active.Import,Current.Import,Voltage"));
    assert_equal("get_configuration_response_phase_switch", get_configuration_response->connector_switch_3_to_1_phase_supported,
                 std::string("false"));
    std::unique_ptr<OcppMessage> change_configuration_message = protocol.parse_message(
        R"([3,"change-config-meter-values-sampled-data",{"status":"Rejected"}])"
    );
    auto *change_configuration_response = dynamic_cast<ChangeConfigurationResponse *>(change_configuration_message.get());
    assert_equal("change_configuration_response_exists", change_configuration_response != nullptr, true);
    assert_equal("change_configuration_response_status", change_configuration_response->status, std::string("Rejected"));
    std::unique_ptr<OcppMessage> meter_values_message = protocol.parse_message(
        R"([2,"meter-1","MeterValues",{"connectorId":1,"meterValue":[{"timestamp":"2026-06-20T00:00:00Z","sampledValue":[{"value":"16.2","measurand":"Current.Import","unit":"A"},{"value":"3.68","measurand":"Power.Active.Import","unit":"kW"},{"value":"12345","measurand":"Energy.Active.Import.Register","unit":"Wh"},{"value":"230.5","measurand":"Voltage","unit":"V"}]}]}])"
    );
    auto *meter_values = dynamic_cast<MeterValues *>(meter_values_message.get());
    assert_equal("meter_values_exists", meter_values != nullptr, true);
    assert_equal("meter_values_current", meter_values->current, 16.2f);
    assert_equal("meter_values_power", meter_values->power, 3680.0f);
    assert_equal("meter_values_energy", meter_values->energy, 12.345f);
    assert_equal("meter_values_voltage", meter_values->voltage, 230.5f);
    std::unique_ptr<OcppMessage> partial_meter_values_message = protocol.parse_message(
        R"([2,"meter-2","MeterValues",{"connectorId":1,"meterValue":[{"sampledValue":[{"value":"54321"},{"value":"240","measurand":"Voltage"}]}]}])"
    );
    auto *partial_meter_values = dynamic_cast<MeterValues *>(partial_meter_values_message.get());
    assert_equal("partial_meter_values_exists", partial_meter_values != nullptr, true);
    assert_equal("partial_meter_values_current_nan", std::isnan(partial_meter_values->current), true);
    assert_equal("partial_meter_values_power_nan", std::isnan(partial_meter_values->power), true);
    assert_equal("partial_meter_values_energy_default_measurand", partial_meter_values->energy, 54.321f);
    assert_equal("partial_meter_values_voltage", partial_meter_values->voltage, 240.0f);
    assert_equal("get_configuration_request_unsupported_on_ocpp201", protocol.set_websocket_protocol("ocpp2.0.1"), true);
    assert_equal("change_configuration_request_ocpp201_empty",
                 protocol.make_change_configuration_request("change-config-meter-values-sampled-data",
                                                            "MeterValuesSampledData",
                                                            "Current.Import"),
                 std::string(""));
    assert_equal("get_configuration_request_ocpp201_empty", protocol.make_get_configuration_request("get-configuration"),
                 std::string(""));
    assert_equal("boot_response", protocol.make_boot_notification_response("boot-1"),
                 R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
    assert_equal("meter_values_response", protocol.make_meter_values_response("meter-1"), R"([3,"meter-1",{}])");
}
