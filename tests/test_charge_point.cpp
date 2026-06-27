#include "assertions.cpp"
#include "esphome/components/ocpp/charge_point.h"

#include <cmath>
#include <string>
#include <vector>

using esphome::ocpp::ChargePoint;
using esphome::ocpp::Connector;
using esphome::ocpp::MeterValues;
using esphome::ocpp::OcppMessage;
using esphome::ocpp::OcppMessageType;
using esphome::ocpp::QueuedMessage;
using esphome::ocpp::SampledValue;
using esphome::ocpp::StatusNotification;
using esphome::binary_sensor::BinarySensor;
using esphome::sensor::Sensor;
using esphome::text_sensor::TextSensor;

class TestChargePoint : public ChargePoint {
 public:
    using ChargePoint::debug_action_for_message_;
    using ChargePoint::should_log_debug_ocpp_message_;

    TestChargePoint() {
        this->set_max_current(32);
        this->set_online_binary_sensor(&this->online_sensor);
        this->set_protocol_text_sensor(&this->protocol_sensor);
        this->set_charger_info_text_sensor(&this->charger_info_sensor);
        this->connector.set_max_current(this->get_max_current());
        this->connector.set_current_sensor(&this->current_sensor);
        this->connector.set_control_current_sensor(&this->control_current_sensor);
        this->connector.set_power_sensor(&this->power_sensor);
        this->connector.set_total_energy_sensor(&this->total_energy_sensor);
        this->connector.set_session_energy_sensor(&this->session_energy_sensor);
        this->connector.set_session_time_sensor(&this->session_time_sensor);
        this->connector.set_voltage_sensor(&this->voltage_sensor);
        this->connector.set_status_text_sensor(&this->status_sensor);
        this->connector.set_error_text_sensor(&this->error_sensor);
        this->connector.set_plugged_binary_sensor(&this->plugged_sensor);
        this->add_connector(&this->connector);
        this->second_connector.set_connector_id(2);
        this->second_connector.set_max_current(this->get_max_current());
        this->second_connector.set_current_sensor(&this->second_current_sensor);
        this->second_connector.set_control_current_sensor(&this->second_control_current_sensor);
        this->second_connector.set_power_sensor(&this->second_power_sensor);
        this->second_connector.set_total_energy_sensor(&this->second_total_energy_sensor);
        this->second_connector.set_voltage_sensor(&this->second_voltage_sensor);
        this->second_connector.set_status_text_sensor(&this->second_status_sensor);
        this->second_connector.set_error_text_sensor(&this->second_error_sensor);
        this->second_connector.set_plugged_binary_sensor(&this->second_plugged_sensor);
        this->add_connector(&this->second_connector);
    }

    TestChargePoint(const TestChargePoint &) = delete;
    TestChargePoint &operator=(const TestChargePoint &) = delete;

    std::vector<QueuedMessage> &messages{this->messages_};
    BinarySensor online_sensor;
    Connector connector;
    Connector second_connector;
    Sensor current_sensor;
    Sensor control_current_sensor;
    Sensor power_sensor;
    Sensor total_energy_sensor;
    Sensor session_energy_sensor;
    Sensor session_time_sensor;
    Sensor voltage_sensor;
    Sensor second_current_sensor;
    Sensor second_control_current_sensor;
    Sensor second_power_sensor;
    Sensor second_total_energy_sensor;
    Sensor second_voltage_sensor;
    TextSensor protocol_sensor;
    TextSensor charger_info_sensor;
    BinarySensor plugged_sensor;
    BinarySensor second_plugged_sensor;
    TextSensor status_sensor;
    TextSensor error_sensor;
    TextSensor second_status_sensor;
    TextSensor second_error_sensor;
};

int main() {
    {
        // Authorize accepts any idTag to let the charger continue with the transaction flow
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(R"([2,"authorize-1","Authorize",{"idTag":"free"}])");
        assert_equal("authorize_online", charge_point.is_online(), true);
        assert_equal("authorize_response_count", charge_point.messages.size(), 1);
        assert_equal("authorize_response", charge_point.messages[0].payload,
                     R"([3,"authorize-1",{"idTagInfo":{"status":"Accepted"}}])");
    }

    {
        // StartTransaction is accepted and tracked per connector
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(
            R"([2,"start-1","StartTransaction",{"connectorId":1,"idTag":"free","meterStart":123,"timestamp":"2026-06-20T00:00:00Z"}])"
        );
        assert_equal("start_transaction_online", charge_point.is_online(), true);
        assert_equal("start_transaction_response_count", charge_point.messages.size(), 2);
        assert_equal("start_transaction_response", charge_point.messages[0].payload,
                     R"([3,"start-1",{"idTagInfo":{"status":"Accepted"},"transactionId":1}])");
        assert_equal("start_transaction_profile", charge_point.messages[1].payload,
                     R"([2,"set-charging-profile-1-1","SetChargingProfile",{"connectorId":1,"csChargingProfiles":{"chargingProfileId":1,"transactionId":1,"stackLevel":0,"chargingProfilePurpose":"TxProfile","chargingProfileKind":"Absolute","chargingSchedule":{"chargingRateUnit":"A","chargingSchedulePeriod":[{"startPeriod":0,"limit":32}]}}}])");
        assert_equal("start_transaction_connector_1_active_id", charge_point.connector.get_active_transaction_id(), 1U);

        charge_point.handle_ocpp_text(
            R"([2,"start-2","StartTransaction",{"connectorId":2,"idTag":"guest","meterStart":456,"timestamp":"2026-06-20T00:05:00Z"}])"
        );
        assert_equal("start_transaction_second_response_count", charge_point.messages.size(), 4);
        assert_equal("start_transaction_second_response", charge_point.messages[2].payload,
                     R"([3,"start-2",{"idTagInfo":{"status":"Accepted"},"transactionId":2}])");
        assert_equal("start_transaction_second_profile", charge_point.messages[3].payload,
                     R"([2,"set-charging-profile-2-2","SetChargingProfile",{"connectorId":2,"csChargingProfiles":{"chargingProfileId":2,"transactionId":2,"stackLevel":0,"chargingProfilePurpose":"TxProfile","chargingProfileKind":"Absolute","chargingSchedule":{"chargingRateUnit":"A","chargingSchedulePeriod":[{"startPeriod":0,"limit":32}]}}}])");
        assert_equal("start_transaction_connector_2_active_id", charge_point.second_connector.get_active_transaction_id(), 2U);
    }

    {
        // control_current changes send SetChargingProfile only while a transaction is active
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.connector.set_requested_current(12.3f);
        assert_equal("profile_not_sent_without_transaction", charge_point.messages.size(), 0);

        charge_point.handle_ocpp_text(
            R"([2,"start-1","StartTransaction",{"connectorId":1,"idTag":"free","meterStart":123,"timestamp":"2026-06-20T00:00:00Z"}])"
        );
        assert_equal("profile_sent_on_start_count", charge_point.messages.size(), 2);
        assert_equal("profile_sent_on_start_current", charge_point.messages[1].payload,
                     R"([2,"set-charging-profile-1-1","SetChargingProfile",{"connectorId":1,"csChargingProfiles":{"chargingProfileId":1,"transactionId":1,"stackLevel":0,"chargingProfilePurpose":"TxProfile","chargingProfileKind":"Absolute","chargingSchedule":{"chargingRateUnit":"A","chargingSchedulePeriod":[{"startPeriod":0,"limit":12.3}]}}}])");

        charge_point.messages.clear();
        charge_point.connector.set_current_limit(10.0f);
        assert_equal("profile_sent_on_control_current_change_count", charge_point.messages.size(), 1);
        assert_equal("profile_sent_on_control_current_change", charge_point.messages[0].payload,
                     R"([2,"set-charging-profile-1-2","SetChargingProfile",{"connectorId":1,"csChargingProfiles":{"chargingProfileId":1,"transactionId":1,"stackLevel":0,"chargingProfilePurpose":"TxProfile","chargingProfileKind":"Absolute","chargingSchedule":{"chargingRateUnit":"A","chargingSchedulePeriod":[{"startPeriod":0,"limit":10}]}}}])");

        charge_point.connector.set_current_limit(10.0f);
        assert_equal("profile_not_repeated_when_control_current_unchanged", charge_point.messages.size(), 1);
    }

    {
        // Resuming from SuspendedEV reapplies the transaction profile even when control_current is unchanged
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.connector.set_current_limit(7.0f);
        charge_point.connector.set_active_transaction_id(1);
        charge_point.connector.publish_status_notification(StatusNotification("", 1, "NoError", "Charging"));

        charge_point.handle_ocpp_text(
            R"([2,"status-suspended","StatusNotification",{"connectorId":1,"errorCode":"NoError","status":"SuspendedEV"}])"
        );
        assert_equal("suspended_ev_response_only_count", charge_point.messages.size(), 1);
        assert_equal("suspended_ev_response", charge_point.messages[0].payload, R"([3,"status-suspended",{}])");

        charge_point.messages.clear();
        charge_point.handle_ocpp_text(
            R"([2,"status-charging","StatusNotification",{"connectorId":1,"errorCode":"NoError","status":"Charging"}])"
        );
        assert_equal("charging_resume_profile_count", charge_point.messages.size(), 2);
        assert_equal("charging_resume_response", charge_point.messages[0].payload, R"([3,"status-charging",{}])");
        assert_equal("charging_resume_profile", charge_point.messages[1].payload,
                     R"([2,"set-charging-profile-1-1","SetChargingProfile",{"connectorId":1,"csChargingProfiles":{"chargingProfileId":1,"transactionId":1,"stackLevel":0,"chargingProfilePurpose":"TxProfile","chargingProfileKind":"Absolute","chargingSchedule":{"chargingRateUnit":"A","chargingSchedulePeriod":[{"startPeriod":0,"limit":7}]}}}])");

        charge_point.messages.clear();
        charge_point.handle_ocpp_text(
            R"([2,"status-charging-duplicate","StatusNotification",{"connectorId":1,"errorCode":"NoError","status":"Charging"}])"
        );
        assert_equal("duplicate_charging_response_only_count", charge_point.messages.size(), 1);
        assert_equal("duplicate_charging_response", charge_point.messages[0].payload,
                     R"([3,"status-charging-duplicate",{}])");
    }

    {
        // control_current zero/non-zero transitions stop and restart the OCPP transaction
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(
            R"([2,"start-1","StartTransaction",{"connectorId":1,"idTag":"free","meterStart":123,"timestamp":"2026-06-20T00:00:00Z"}])"
        );
        charge_point.messages.clear();

        charge_point.connector.set_current_limit(0.0f);
        assert_equal("remote_stop_on_zero_count", charge_point.messages.size(), 1);
        assert_equal("remote_stop_on_zero", charge_point.messages[0].payload,
                     R"([2,"remote-stop-transaction-1-1","RemoteStopTransaction",{"transactionId":1}])");

        charge_point.handle_ocpp_text(
            R"([2,"stop-1","StopTransaction",{"transactionId":1,"meterStop":456,"timestamp":"2026-06-20T00:10:00Z"}])"
        );
        assert_equal("remote_stop_clears_transaction", charge_point.connector.get_active_transaction_id(), 0U);
        charge_point.messages.clear();

        charge_point.connector.set_current_limit(6.0f);
        assert_equal("remote_start_on_nonzero_count", charge_point.messages.size(), 1);
        assert_equal("remote_start_on_nonzero", charge_point.messages[0].payload,
                     R"([2,"remote-start-transaction-1-1","RemoteStartTransaction",{"connectorId":1,"idTag":"free"}])");

        charge_point.handle_ocpp_text(
            R"([2,"start-2","StartTransaction",{"connectorId":1,"idTag":"free","meterStart":456,"timestamp":"2026-06-20T00:11:00Z"}])"
        );
        assert_equal("remote_start_transaction_profile_count", charge_point.messages.size(), 3);
        assert_equal("remote_start_transaction_response", charge_point.messages[1].payload,
                     R"([3,"start-2",{"idTagInfo":{"status":"Accepted"},"transactionId":2}])");
        assert_equal("remote_start_transaction_profile", charge_point.messages[2].payload,
                     R"([2,"set-charging-profile-1-2","SetChargingProfile",{"connectorId":1,"csChargingProfiles":{"chargingProfileId":1,"transactionId":2,"stackLevel":0,"chargingProfilePurpose":"TxProfile","chargingProfileKind":"Absolute","chargingSchedule":{"chargingRateUnit":"A","chargingSchedulePeriod":[{"startPeriod":0,"limit":6}]}}}])");
    }

    {
        // StopTransaction clears the active transaction and disconnect also clears state
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(
            R"([2,"start-1","StartTransaction",{"connectorId":1,"idTag":"free","meterStart":123,"timestamp":"2026-06-20T00:00:00Z"}])"
        );
        assert_equal("stop_transaction_active_before_stop", charge_point.connector.get_active_transaction_id(), 1U);
        charge_point.messages.clear();
        charge_point.handle_ocpp_text(
            R"([2,"stop-1","StopTransaction",{"transactionId":1,"meterStop":789,"timestamp":"2026-06-20T00:10:00Z"}])"
        );
        assert_equal("stop_transaction_response_count", charge_point.messages.size(), 1);
        assert_equal("stop_transaction_response", charge_point.messages[0].payload, R"([3,"stop-1",{}])");
        assert_equal("stop_transaction_active_after_stop", charge_point.connector.get_active_transaction_id(), 0U);

        charge_point.handle_ocpp_text(
            R"([2,"start-2","StartTransaction",{"connectorId":1,"idTag":"free","meterStart":800,"timestamp":"2026-06-20T00:20:00Z"}])"
        );
        assert_equal("disconnect_clears_transaction_before_disconnect", charge_point.connector.get_active_transaction_id(), 2U);
        charge_point.on_disconnected();
        assert_equal("disconnect_clears_transaction_after_disconnect", charge_point.connector.get_active_transaction_id(), 0U);
    }

    {
        TestChargePoint charge_point;

        assert_equal("set_charge_point_id", charge_point.get_charge_point_id(), "");
        assert_equal("get_connection_id", charge_point.get_connection_id(), "");

        // set_charge_point_id also initializes the connection_id
        charge_point.set_charge_point_id("A99999");
        assert_equal("set_charge_point_id", charge_point.get_charge_point_id(), "A99999");
        assert_equal("set_charge_point_id_connection_id", charge_point.get_connection_id(), "A99999");

        // set_connection_id
        charge_point.set_connection_id("B11111");
        assert_equal("set_connection_id", charge_point.get_connection_id(), "B11111");
        assert_equal("set_connection_id_keeps_charge_point_id", charge_point.get_charge_point_id(), "A99999");

        // get_debug_ocpp_messages
        assert_equal("get_debug_ocpp_messages", charge_point.get_debug_ocpp_messages(), false);
        assert_equal("debug_meter_values_not_excluded", charge_point.is_debug_ocpp_action_excluded("MeterValues"), false);
        assert_equal("get_startup_notifications_delay", charge_point.get_startup_notifications_delay(), 300000U);
        assert_equal("get_max_current", charge_point.get_max_current(), 32U);
        assert_equal("get_max_queued_messages", charge_point.get_max_queued_messages(), 8);
        assert_equal("get_force_protocol", charge_point.get_force_protocol(), std::string(""));
        assert_equal("protocol_default", charge_point.protocol_sensor.state, std::string(""));
        assert_equal("charger_info_default", charge_point.charger_info_sensor.state, std::string(""));
        assert_equal("status_default", charge_point.status_sensor.state, std::string(""));
        assert_equal("error_default", charge_point.error_sensor.state, std::string(""));

        // set_debug_ocpp_messages
        charge_point.set_debug_ocpp_messages(true);
        assert_equal("set_debug_ocpp_messages", charge_point.get_debug_ocpp_messages(), true);

        // add_debug_ocpp_exclude_action
        charge_point.add_debug_ocpp_exclude_action("MeterValues");
        charge_point.add_debug_ocpp_exclude_action("MeterValues");
        assert_equal("debug_meter_values_excluded", charge_point.is_debug_ocpp_action_excluded("MeterValues"), true);
        assert_equal("debug_meter_values_case_sensitive", charge_point.is_debug_ocpp_action_excluded("metervalues"), false);

        // set_startup_notifications_delay
        charge_point.set_startup_notifications_delay(0);
        assert_equal("set_startup_notifications_delay", charge_point.get_startup_notifications_delay(), 0U);

        // set_max_current stores the charge point installation limit; connectors are configured once
        charge_point.set_max_current(24);
        assert_equal("set_max_current", charge_point.get_max_current(), 24U);
        assert_equal("set_max_current_connector_unchanged", charge_point.connector.get_max_current(), 32U);
        assert_equal("set_max_current_second_connector_unchanged", charge_point.second_connector.get_max_current(), 32U);

        // set_phase_mapping stores charge-point phase to site phase order
        const auto &phase_mapping = charge_point.get_phase_mapping();
        assert_equal("phase_mapping_default_l1", phase_mapping[0], static_cast<uint8_t>(1));
        assert_equal("phase_mapping_default_l2", phase_mapping[1], static_cast<uint8_t>(2));
        assert_equal("phase_mapping_default_l3", phase_mapping[2], static_cast<uint8_t>(3));
        charge_point.set_phase_mapping({0, 0, 0});
        assert_equal("phase_mapping_unconfigured_l1", phase_mapping[0], static_cast<uint8_t>(0));
        assert_equal("phase_mapping_unconfigured_l2", phase_mapping[1], static_cast<uint8_t>(0));
        assert_equal("phase_mapping_unconfigured_l3", phase_mapping[2], static_cast<uint8_t>(0));
        charge_point.set_phase_mapping({2, 3, 1});
        assert_equal("phase_mapping_l1", phase_mapping[0], static_cast<uint8_t>(2));
        assert_equal("phase_mapping_l2", phase_mapping[1], static_cast<uint8_t>(3));
        assert_equal("phase_mapping_l3", phase_mapping[2], static_cast<uint8_t>(1));
        assert_equal("phase_mapping_size", phase_mapping.size(), static_cast<size_t>(3));
        charge_point.set_phase_mapping({2});
        assert_equal("phase_mapping_short_l1", phase_mapping[0], static_cast<uint8_t>(2));
        assert_equal("phase_mapping_short_l2", phase_mapping[1], static_cast<uint8_t>(0));
        assert_equal("phase_mapping_short_l3", phase_mapping[2], static_cast<uint8_t>(0));

        // add_connector composes connector-to-charge-point and charge-point-to-site mappings
        ChargePoint rotated_charge_point;
        Connector rotated_connector;
        rotated_charge_point.set_phase_mapping({3, 1, 2});
        rotated_connector.set_phases(2);
        rotated_connector.set_phase_mapping({2, 1});
        rotated_charge_point.add_connector(&rotated_connector);
        const auto &composed_phase_mapping = rotated_connector.get_phase_mapping();
        assert_equal("composed_phase_mapping_l1", composed_phase_mapping[0], static_cast<uint8_t>(1));
        assert_equal("composed_phase_mapping_l2", composed_phase_mapping[1], static_cast<uint8_t>(3));
        assert_equal("composed_phase_mapping_l3", composed_phase_mapping[2], static_cast<uint8_t>(0));

        // set_max_queued_messages
        charge_point.set_max_queued_messages(16);
        assert_equal("set_max_queued_messages", charge_point.get_max_queued_messages(), 16);

        // set_force_protocol
        charge_point.set_force_protocol("ocpp1.6");
        assert_equal("set_force_protocol", charge_point.get_force_protocol(), std::string("ocpp1.6"));
    }

    {
        // Protocol sensor is published on connect and cleared on disconnect
        TestChargePoint charge_point;
        charge_point.on_connected("A99999", "ocpp1.6");
        assert_equal("protocol_after_connect", charge_point.protocol_sensor.state,
                     std::string("ocpp1.6"));
        charge_point.on_disconnected();
        assert_equal("protocol_after_disconnect", charge_point.protocol_sensor.state, std::string(""));
    }

    {
        // BootNotification updates online state, emits a response, and requests configuration once
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        assert_equal("online_before_boot", charge_point.is_online(), false);
        assert_equal("online_sensor_before_boot", charge_point.online_sensor.state, false);
        charge_point.handle_ocpp_text(R"([2,"boot-1","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        assert_equal("online_after_boot", charge_point.is_online(), true);
        assert_equal("online_sensor_after boot", charge_point.online_sensor.state, true);
        assert_equal("charger_info_after_boot", charge_point.charger_info_sensor.state,
                     std::string("vendor: Acme, model: Wallbox"));
        assert_equal("boot_response_count", charge_point.messages.size(), 2);
        assert_equal("boot_response", charge_point.messages[0].payload, R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
        assert_equal("boot_get_configuration", charge_point.messages[1].payload,
                     R"([2,"get-configuration","GetConfiguration",{"key":["MeterValueSampleInterval","MeterValuesSampledData","ConnectorSwitch3to1PhaseSupported"]}])");

        charge_point.on_disconnected();
        assert_equal("online_after_disconnect", charge_point.is_online(), false);
        assert_equal("online_sensor_after_disconnect", charge_point.online_sensor.state, false);
        assert_equal("charger_info_after_disconnect", charge_point.charger_info_sensor.state, std::string(""));
        assert_equal("queued_messages_cleared_after_disconnect", charge_point.messages.size(), 0);
    }

    {
        // GetConfiguration is sent after every BootNotification, including reconnects
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(R"([2,"boot-1","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        assert_equal("get_configuration_first_boot_count", charge_point.messages.size(), 2);

        charge_point.on_disconnected();
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(R"([2,"boot-2","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        assert_equal("get_configuration_second_boot_count", charge_point.messages.size(), 2);
        assert_equal("get_configuration_second_boot_response", charge_point.messages[0].payload,
                     R"([3,"boot-2",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
        assert_equal("get_configuration_second_boot_request", charge_point.messages[1].payload,
                     R"([2,"get-configuration","GetConfiguration",{"key":["MeterValueSampleInterval","MeterValuesSampledData","ConnectorSwitch3to1PhaseSupported"]}])");

        charge_point.messages.clear();
        charge_point.handle_ocpp_text(R"([2,"boot-3","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        assert_equal("get_configuration_third_boot_count", charge_point.messages.size(), 2);
        assert_equal("get_configuration_third_boot_request", charge_point.messages[1].payload,
                     R"([2,"get-configuration","GetConfiguration",{"key":["MeterValueSampleInterval","MeterValuesSampledData","ConnectorSwitch3to1PhaseSupported"]}])");
    }

    {
        // Targeted GetConfiguration response values are captured for later use/diagnostics
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(
            R"([3,"get-configuration",{"configurationKey":[{"key":"MeterValueSampleInterval","readonly":false,"value":"5"},{"key":"MeterValuesSampledData","readonly":false,"value":"Power.Active.Import,Current.Import,Voltage"},{"key":"ConnectorSwitch3to1PhaseSupported","readonly":true,"value":"false"}]}])");
        assert_equal("get_configuration_meter_value_sample_interval", charge_point.get_meter_value_sample_interval(),
                     std::string("5"));
        assert_equal("get_configuration_meter_values_sampled_data", charge_point.get_meter_values_sampled_data(),
                     std::string("Power.Active.Import,Current.Import,Voltage"));
        assert_equal("get_configuration_connector_switch_3_to_1_phase_supported",
                     charge_point.get_connector_switch_3_to_1_phase_supported(), std::string("false"));
        assert_equal("get_configuration_queues_change_configuration_count", charge_point.messages.size(), 1);
        assert_equal("get_configuration_queues_change_configuration", charge_point.messages[0].payload,
                     R"([2,"change-config-meter-values-sampled-data","ChangeConfiguration",{"key":"MeterValuesSampledData","value":"Current.Import,Power.Active.Import,Energy.Active.Import.Register,Voltage"}])");
    }

    {
        // MeterValuesSampledData falls back in order and then MeterValueSampleInterval is changed to 5
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(
            R"([3,"get-configuration",{"configurationKey":[{"key":"MeterValueSampleInterval","readonly":false,"value":"60"},{"key":"MeterValuesSampledData","readonly":false,"value":"Power.Active.Import"}]}])");
        assert_equal("change_configuration_initial_request_count", charge_point.messages.size(), 1);
        assert_equal("change_configuration_initial_request", charge_point.messages[0].payload,
                     R"([2,"change-config-meter-values-sampled-data","ChangeConfiguration",{"key":"MeterValuesSampledData","value":"Current.Import,Power.Active.Import,Energy.Active.Import.Register,Voltage"}])");

        charge_point.handle_ocpp_text(R"([3,"change-config-meter-values-sampled-data",{"status":"Rejected"}])");
        assert_equal("change_configuration_first_fallback_count", charge_point.messages.size(), 2);
        assert_equal("change_configuration_first_fallback", charge_point.messages[1].payload,
                     R"([2,"change-config-meter-values-sampled-data","ChangeConfiguration",{"key":"MeterValuesSampledData","value":"Current.Import,Power.Active.Import,Energy.Active.Import.Register"}])");

        charge_point.handle_ocpp_text(R"([3,"change-config-meter-values-sampled-data",{"status":"Rejected"}])");
        assert_equal("change_configuration_second_fallback_count", charge_point.messages.size(), 3);
        assert_equal("change_configuration_second_fallback", charge_point.messages[2].payload,
                     R"([2,"change-config-meter-values-sampled-data","ChangeConfiguration",{"key":"MeterValuesSampledData","value":"Current.Import,Power.Active.Import"}])");

        charge_point.handle_ocpp_text(R"([3,"change-config-meter-values-sampled-data",{"status":"Accepted"}])");
        assert_equal("change_configuration_interval_request_count", charge_point.messages.size(), 4);
        assert_equal("change_configuration_interval_request", charge_point.messages[3].payload,
                     R"([2,"change-config-meter-value-sample-interval","ChangeConfiguration",{"key":"MeterValueSampleInterval","value":"5"}])");
    }

    {
        // Exhausting all MeterValuesSampledData fallbacks still proceeds to MeterValueSampleInterval
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(
            R"([3,"get-configuration",{"configurationKey":[{"key":"MeterValueSampleInterval","readonly":false,"value":"60"}]}])");
        charge_point.handle_ocpp_text(R"([3,"change-config-meter-values-sampled-data",{"status":"Rejected"}])");
        charge_point.handle_ocpp_text(R"([3,"change-config-meter-values-sampled-data",{"status":"Rejected"}])");
        charge_point.handle_ocpp_text(R"([3,"change-config-meter-values-sampled-data",{"status":"Rejected"}])");
        charge_point.handle_ocpp_text(R"([3,"change-config-meter-values-sampled-data",{"status":"Rejected"}])");
        assert_equal("change_configuration_interval_after_all_fallbacks_count", charge_point.messages.size(), 5);
        assert_equal("change_configuration_interval_after_all_fallbacks", charge_point.messages[4].payload,
                     R"([2,"change-config-meter-value-sample-interval","ChangeConfiguration",{"key":"MeterValueSampleInterval","value":"5"}])");
    }

    {
        // OCPP 2.0.1 BootNotification uses a different payload shape but the same charge point logic
        TestChargePoint charge_point;
        charge_point.on_connected("A99999", "ocpp2.0.1");
        charge_point.handle_ocpp_text(
            R"([2,"boot-2","BootNotification",{"chargingStation":{"model":"Prism Solar","vendorName":"Silla Industries","firmwareVersion":"3.2.77"},"reason":"PowerUp"}])");
        assert_equal("ocpp201_online_after_boot", charge_point.is_online(), true);
        assert_equal("ocpp201_charger_info_after_boot", charge_point.charger_info_sensor.state,
                     std::string("vendor: Silla Industries, model: Prism Solar, firmware: 3.2.77"));
        assert_equal("ocpp201_boot_response_count", charge_point.messages.size(), 1);
        assert_equal("ocpp201_boot_response", charge_point.messages[0].payload,
                     R"([3,"boot-2",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
    }

    {
        // Heartbeat
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        assert_equal("offline_before_heartbeat", charge_point.is_online(), false);
        charge_point.handle_ocpp_text(R"([2,"d8cc833a-0f43-441a-adbc-5e1f1869f067","Heartbeat",{}])");
        assert_equal("online_after_heartbeat", charge_point.is_online(), true);
        assert_equal("heartbeat_response_count", charge_point.messages.size(), 1);
        assert_equal("heartbeat_response", charge_point.messages[0].payload, R"([3,"d8cc833a-0f43-441a-adbc-5e1f1869f067",{"currentTime":"1970-01-01T00:00:00Z"}])");
    }

    {
        // MeterValues updates the matching connector sensors and replies with an empty result
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(
            R"([2,"meter-1","MeterValues",{"connectorId":1,"meterValue":[{"sampledValue":[{"value":"16.2","measurand":"Current.Import","unit":"A"},{"value":"3680","measurand":"Power.Active.Import","unit":"W"},{"value":"12345","measurand":"Energy.Active.Import.Register","unit":"Wh"}]}]}])");
        assert_equal("meter_values_online", charge_point.is_online(), true);
        assert_equal("meter_values_current_sensor", charge_point.current_sensor.state, 16.2f);
        assert_equal("meter_values_power_sensor", charge_point.power_sensor.state, 3680.0f);
        assert_equal("meter_values_total_energy_sensor", charge_point.total_energy_sensor.state, 12.345f);
        assert_equal("meter_values_voltage_sensor_nan", std::isnan(charge_point.voltage_sensor.state), true);
        assert_equal("meter_values_second_current_sensor_nan", std::isnan(charge_point.second_current_sensor.state), true);
        assert_equal("meter_values_response_count", charge_point.messages.size(), 1);
        assert_equal("meter_values_response", charge_point.messages[0].payload, R"([3,"meter-1",{}])");
        assert_equal("meter_values_response_action", charge_point.messages[0].action, std::string("MeterValues"));

        charge_point.handle_ocpp_text(
            R"([2,"meter-2","MeterValues",{"connectorId":2,"meterValue":[{"sampledValue":[{"value":"230.5","measurand":"Voltage","unit":"V"}]}]}])");
        assert_equal("second_meter_values_current_sensor_nan", std::isnan(charge_point.second_current_sensor.state), true);
        assert_equal("second_meter_values_power_sensor_nan", std::isnan(charge_point.second_power_sensor.state), true);
        assert_equal("second_meter_values_total_energy_sensor_nan", std::isnan(charge_point.second_total_energy_sensor.state), true);
        assert_equal("second_meter_values_voltage_sensor", charge_point.second_voltage_sensor.state, 230.5f);
        assert_equal("second_meter_values_first_connector_current_unchanged", charge_point.current_sensor.state, 16.2f);

        charge_point.on_disconnected();
        assert_equal("meter_values_current_sensor_after_disconnect_nan", std::isnan(charge_point.current_sensor.state), true);
        assert_equal("meter_values_second_voltage_sensor_after_disconnect_nan",
                     std::isnan(charge_point.second_voltage_sensor.state), true);
    }

    {
        // MeterValues with transactionId recovers the active transaction after a restart
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(
            R"([2,"meter-transaction-1","MeterValues",{"connectorId":1,"transactionId":7,"meterValue":[{"sampledValue":[{"value":"10","measurand":"Current.Import","unit":"A"}]}]}])");
        assert_equal("meter_values_recovers_transaction_id", charge_point.connector.get_active_transaction_id(), 7U);
        assert_equal("meter_values_recovery_response_count", charge_point.messages.size(), 2);
        assert_equal("meter_values_recovery_response", charge_point.messages[0].payload, R"([3,"meter-transaction-1",{}])");
        assert_equal("meter_values_recovery_profile", charge_point.messages[1].payload,
                     R"([2,"set-charging-profile-1-1","SetChargingProfile",{"connectorId":1,"csChargingProfiles":{"chargingProfileId":1,"transactionId":7,"stackLevel":0,"chargingProfilePurpose":"TxProfile","chargingProfileKind":"Absolute","chargingSchedule":{"chargingRateUnit":"A","chargingSchedulePeriod":[{"startPeriod":0,"limit":32}]}}}])");

        charge_point.messages.clear();
        charge_point.handle_ocpp_text(
            R"([2,"meter-transaction-2","MeterValues",{"connectorId":1,"transactionId":7,"meterValue":[{"sampledValue":[{"value":"11","measurand":"Current.Import","unit":"A"}]}]}])");
        assert_equal("meter_values_same_transaction_response_only", charge_point.messages.size(), 1);
        assert_equal("meter_values_same_transaction_response", charge_point.messages[0].payload,
                     R"([3,"meter-transaction-2",{}])");

        charge_point.messages.clear();
        charge_point.connector.set_current_limit(10.0f);
        assert_equal("meter_values_recovered_transaction_current_change_count", charge_point.messages.size(), 1);
        assert_equal("meter_values_recovered_transaction_current_change", charge_point.messages[0].payload,
                     R"([2,"set-charging-profile-1-2","SetChargingProfile",{"connectorId":1,"csChargingProfiles":{"chargingProfileId":1,"transactionId":7,"stackLevel":0,"chargingProfilePurpose":"TxProfile","chargingProfileKind":"Absolute","chargingSchedule":{"chargingRateUnit":"A","chargingSchedulePeriod":[{"startPeriod":0,"limit":10}]}}}])");
    }

    {
        // StatusNotification
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        assert_equal("offline_before_status_notification", charge_point.is_online(), false);
        assert_equal("plugged_has_state_before_first_status", charge_point.plugged_sensor.has_state, true);
        assert_equal("plugged_off_before_first_status", charge_point.plugged_sensor.state, false);
        assert_equal("second_plugged_has_state_before_first_status", charge_point.second_plugged_sensor.has_state, true);
        assert_equal("second_plugged_off_before_first_status", charge_point.second_plugged_sensor.state, false);
        charge_point.handle_ocpp_text(
            R"([2,"status-1","StatusNotification",{"connectorId":1,"errorCode":"NoError","status":"Available"}])");
        assert_equal("online_after_status_notification", charge_point.is_online(), true);
        assert_equal("status_notification_status_sensor", charge_point.status_sensor.state, std::string("Available"));
        assert_equal("status_notification_error_sensor", charge_point.error_sensor.state, std::string(""));
        assert_equal("status_notification_plugged_has_state", charge_point.plugged_sensor.has_state, true);
        assert_equal("status_notification_plugged_false", charge_point.plugged_sensor.state, false);
        assert_equal("status_notification_internal_plugged_false", charge_point.connector.is_plugged(), false);
        assert_equal("status_notification_second_plugged_has_state", charge_point.second_plugged_sensor.has_state, true);
        assert_equal("status_notification_second_plugged_false", charge_point.second_plugged_sensor.state, false);
        assert_equal("status_notification_second_status_sensor_unchanged", charge_point.second_status_sensor.state,
                     std::string(""));
        assert_equal("status_notification_second_error_sensor_unchanged", charge_point.second_error_sensor.state,
                     std::string(""));

        charge_point.handle_ocpp_text(
            R"([2,"status-prepare","StatusNotification",{"connectorId":1,"errorCode":"NoError","status":"Preparing"}])");
        assert_equal("status_notification_preparing_plugged_true", charge_point.plugged_sensor.state, true);
        assert_equal("status_notification_preparing_internal_plugged_true", charge_point.connector.is_plugged(), true);
        assert_equal("status_notification_preparing_remote_start", charge_point.messages[2].payload,
                     R"([2,"remote-start-transaction-1-1","RemoteStartTransaction",{"connectorId":1,"idTag":"free"}])");

        charge_point.handle_ocpp_text(
            R"([2,"status-2","StatusNotification",{"connectorId":1,"errorCode":"GroundFailure","status":"Faulted"}])");
        assert_equal("status_notification_fault_status_sensor", charge_point.status_sensor.state, std::string("Faulted"));
        assert_equal("status_notification_fault_error_sensor", charge_point.error_sensor.state,
                     std::string("GroundFailure"));
        assert_equal("status_notification_fault_plugged_false", charge_point.plugged_sensor.state, false);
        assert_equal("status_notification_fault_internal_unplugged", charge_point.connector.is_plugged(), false);
        assert_equal("status_notification_response_count", charge_point.messages.size(), 4);
        assert_equal("status_notification_response", charge_point.messages[0].payload, R"([3,"status-1",{}])");
        assert_equal("status_notification_preparing_response", charge_point.messages[1].payload, R"([3,"status-prepare",{}])");
        assert_equal("status_notification_fault_response", charge_point.messages[3].payload, R"([3,"status-2",{}])");

        charge_point.on_disconnected();
        assert_equal("status_notification_status_sensor_after_disconnect", charge_point.status_sensor.state,
                     std::string(""));
        assert_equal("status_notification_error_sensor_after_disconnect", charge_point.error_sensor.state,
                     std::string(""));
        assert_equal("status_notification_plugged_after_disconnect_false", charge_point.plugged_sensor.state, false);
    }


    {
        // startup_notifications_delay can be disabled with 0
        TestChargePoint charge_point;
        charge_point.set_startup_notifications_delay(0);
        charge_point.on_connected("A99999");
        charge_point.loop(300000);
        assert_equal("disabled_startup_notifications_trigger_count", charge_point.messages.size(), 0);
    }

    {
        // Missing startup notifications are requested after the delay, BootNotification first
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.loop(299999);
        assert_equal("startup_trigger_before_delay", charge_point.messages.size(), 0);
        charge_point.loop(300000);
        assert_equal("startup_boot_trigger_after_delay", charge_point.messages.size(), 1);
        assert_equal("startup_boot_trigger", charge_point.messages[0].payload,
                     R"([2,"trigger-boot-notification","TriggerMessage",{"requestedMessage":"BootNotification"}])");
        charge_point.loop(301000);
        assert_equal("startup_status_waits_for_boot_trigger_reply", charge_point.messages.size(), 1);
        std::string startup_boot_trigger;
        assert_equal("startup_boot_trigger_dequeued", charge_point.pop_queued_message(&startup_boot_trigger), true);
        charge_point.handle_ocpp_text(R"([2,"boot-1","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        assert_equal("startup_boot_response_and_get_configuration_count", charge_point.messages.size(), 2);
        assert_equal("startup_boot_response", charge_point.messages[0].payload,
                     R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
        assert_equal("startup_get_configuration_before_status_trigger", charge_point.messages[1].payload,
                     R"([2,"get-configuration","GetConfiguration",{"key":["MeterValueSampleInterval","MeterValuesSampledData","ConnectorSwitch3to1PhaseSupported"]}])");
        charge_point.handle_ocpp_text(R"([3,"trigger-boot-notification",{"status":"Accepted"}])");
        assert_equal("startup_status_trigger_after_boot_reply", charge_point.messages.size(), 3);
        assert_equal("startup_status_trigger", charge_point.messages[2].payload,
                     R"([2,"trigger-status-notification","TriggerMessage",{"requestedMessage":"StatusNotification"}])");
    }

    {
        // Natural BootNotification before the delay suppresses only the boot trigger
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(R"([2,"boot-1","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        assert_equal("natural_boot_response", charge_point.messages[0].payload, R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
        assert_equal("natural_boot_get_configuration", charge_point.messages[1].payload,
                     R"([2,"get-configuration","GetConfiguration",{"key":["MeterValueSampleInterval","MeterValuesSampledData","ConnectorSwitch3to1PhaseSupported"]}])");
        charge_point.messages.clear();
        charge_point.loop(300000);
        assert_equal("natural_boot_status_trigger_count", charge_point.messages.size(), 1);
        assert_equal("natural_boot_status_trigger", charge_point.messages[0].payload,
                     R"([2,"trigger-status-notification","TriggerMessage",{"requestedMessage":"StatusNotification"}])");
    }

    {
        // Debug payload filters match actions and related queued/in-flight responses
        TestChargePoint charge_point;
        charge_point.add_debug_ocpp_exclude_action("MeterValues");
        OcppMessage meter_call(OcppMessageType::CALL, "meter-1", "MeterValues");
        OcppMessage heartbeat_call(OcppMessageType::CALL, "heartbeat-1", "Heartbeat");
        QueuedMessage meter_response{"[3,\"meter-1\",{}]", OcppMessageType::CALL_RESULT, "meter-1", "MeterValues"};
        assert_equal("debug_meter_call_suppressed", charge_point.should_log_debug_ocpp_message_(meter_call), false);
        assert_equal("debug_heartbeat_call_logged", charge_point.should_log_debug_ocpp_message_(heartbeat_call), true);
        assert_equal("debug_meter_response_suppressed", charge_point.should_log_debug_ocpp_message_(meter_response), false);

        charge_point.add_debug_ocpp_exclude_action("TriggerMessage");
        charge_point.on_connected("A99999");
        charge_point.loop(300000);
        std::string startup_boot_trigger;
        assert_equal("debug_trigger_dequeued", charge_point.pop_queued_message(&startup_boot_trigger, 300000), true);
        OcppMessage trigger_result(OcppMessageType::CALL_RESULT, "trigger-boot-notification");
        assert_equal("debug_trigger_result_action", charge_point.debug_action_for_message_(trigger_result),
                     std::string("TriggerMessage"));
        assert_equal("debug_trigger_result_suppressed", charge_point.should_log_debug_ocpp_message_(trigger_result), false);
    }

    {
        // Natural StatusNotification before the delay suppresses only the status trigger
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(
            R"([2,"status-1","StatusNotification",{"connectorId":1,"errorCode":"NoError","status":"Available"}])");
        charge_point.messages.clear();
        charge_point.loop(300000);
        assert_equal("natural_status_boot_trigger_count", charge_point.messages.size(), 1);
        assert_equal("natural_status_boot_trigger", charge_point.messages[0].payload,
                     R"([2,"trigger-boot-notification","TriggerMessage",{"requestedMessage":"BootNotification"}])");
        std::string startup_boot_trigger;
        assert_equal("natural_status_boot_trigger_dequeued", charge_point.pop_queued_message(&startup_boot_trigger), true);
        charge_point.handle_ocpp_text(R"([3,"trigger-boot-notification",{"status":"Accepted"}])");
        assert_equal("natural_status_no_status_trigger_after_boot_reply", charge_point.messages.size(), 0);
    }

    {
        // Queued messages are FIFO and bounded
        TestChargePoint charge_point;
        charge_point.set_max_queued_messages(2);
        charge_point.on_connected("A99999");
        charge_point.handle_ocpp_text(R"([2,"heartbeat-1","Heartbeat",{}])");
        charge_point.handle_ocpp_text(R"([2,"heartbeat-2","Heartbeat",{}])");
        charge_point.handle_ocpp_text(R"([2,"heartbeat-3","Heartbeat",{}])");
        assert_equal("bounded_queue_count", charge_point.messages.size(), 2);
        assert_equal("bounded_queue_first", charge_point.messages[0].payload, R"([3,"heartbeat-1",{"currentTime":"1970-01-01T00:00:00Z"}])");
        assert_equal("bounded_queue_second", charge_point.messages[1].payload, R"([3,"heartbeat-2",{"currentTime":"1970-01-01T00:00:00Z"}])");

        std::string message;
        assert_equal("pop_first_message", charge_point.pop_queued_message(&message), true);
        assert_equal("first_popped_message", message, R"([3,"heartbeat-1",{"currentTime":"1970-01-01T00:00:00Z"}])");
        assert_equal("remaining_queue_count", charge_point.messages.size(), 1);
        assert_equal("remaining_first_message", charge_point.messages[0].payload, R"([3,"heartbeat-2",{"currentTime":"1970-01-01T00:00:00Z"}])");
    }

    {
        // Responses from the charge point are still sent while our previous CALL is in flight
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.loop(300000);

        std::string startup_boot_trigger;
        assert_equal("in_flight_boot_trigger_dequeued", charge_point.pop_queued_message(&startup_boot_trigger, 300000), true);
        assert_equal("in_flight_boot_trigger", startup_boot_trigger,
                     R"([2,"trigger-boot-notification","TriggerMessage",{"requestedMessage":"BootNotification"}])");

        charge_point.handle_ocpp_text(R"([2,"boot-1","BootNotification",{"chargePointVendor":"Acme","chargePointModel":"Wallbox"}])");
        assert_equal("in_flight_queue_count", charge_point.messages.size(), 2);
        assert_equal("in_flight_response_before_call", charge_point.messages[0].payload,
                     R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");
        assert_equal("in_flight_get_configuration_before_status_trigger", charge_point.messages[1].payload,
                     R"([2,"get-configuration","GetConfiguration",{"key":["MeterValueSampleInterval","MeterValuesSampledData","ConnectorSwitch3to1PhaseSupported"]}])");

        std::string boot_response;
        assert_equal("in_flight_response_dequeued", charge_point.pop_queued_message(&boot_response, 300001), true);
        assert_equal("in_flight_response_payload", boot_response,
                     R"([3,"boot-1",{"currentTime":"1970-01-01T00:00:00Z","interval":300,"status":"Accepted"}])");

        std::string blocked_status_trigger;
        assert_equal("in_flight_blocks_next_call", charge_point.pop_queued_message(&blocked_status_trigger, 300002), false);
        charge_point.handle_ocpp_text(R"([3,"trigger-boot-notification",{"status":"Accepted"}])");
        assert_equal("in_flight_queues_next_call_after_reply", charge_point.messages.size(), 2);
        assert_equal("in_flight_next_call_after_reply", charge_point.pop_queued_message(&blocked_status_trigger, 300003), true);
        assert_equal("in_flight_next_call_payload", blocked_status_trigger,
                     R"([2,"get-configuration","GetConfiguration",{"key":["MeterValueSampleInterval","MeterValuesSampledData","ConnectorSwitch3to1PhaseSupported"]}])");

        assert_equal("in_flight_status_trigger_waits_for_get_configuration_count", charge_point.messages.size(), 1);
        assert_equal("in_flight_status_trigger_waits_for_get_configuration", charge_point.messages[0].payload,
                     R"([2,"trigger-status-notification","TriggerMessage",{"requestedMessage":"StatusNotification"}])");
    }

    {
        // A timed-out CALL is cleared so the next charger request can be sent
        TestChargePoint charge_point;
        charge_point.on_connected("A99999");
        charge_point.loop(300000);

        std::string startup_boot_trigger;
        assert_equal("timeout_boot_trigger_dequeued", charge_point.pop_queued_message(&startup_boot_trigger, 300000), true);
        charge_point.loop(300000 + ChargePoint::DEFAULT_CALL_TIMEOUT_MS);
        assert_equal("timeout_status_trigger_count", charge_point.messages.size(), 1);
        assert_equal("timeout_status_trigger", charge_point.messages[0].payload,
                     R"([2,"trigger-status-notification","TriggerMessage",{"requestedMessage":"StatusNotification"}])");

        std::string startup_status_trigger;
        assert_equal("timeout_status_trigger_dequeued",
                     charge_point.pop_queued_message(&startup_status_trigger,
                                                     300000 + ChargePoint::DEFAULT_CALL_TIMEOUT_MS),
                     true);
        assert_equal("timeout_status_trigger_payload", startup_status_trigger,
                     R"([2,"trigger-status-notification","TriggerMessage",{"requestedMessage":"StatusNotification"}])");
    }

    return 0;
}
