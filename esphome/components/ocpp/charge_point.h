#pragma once

#include "connector.h"
#include "protocol.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace esphome::ocpp {

struct QueuedMessage {
    std::string payload;
    OcppMessageType message_type_id{OcppMessageType::CALL_RESULT};
    std::string unique_id;
    std::string action;
    uint32_t sent_at_millis{0};
};

class ChargePoint : public ConnectorListener {
    public:
        static constexpr size_t DEFAULT_MAX_QUEUED_MESSAGES = 8;
        static constexpr uint32_t DEFAULT_STARTUP_NOTIFICATIONS_DELAY_MS = 300000;
        static constexpr uint32_t DEFAULT_CALL_TIMEOUT_MS = 90000;

        enum class ChangeConfigurationStage : uint8_t {
            IDLE = 0,
            METER_VALUES_SAMPLED_DATA,
            METER_VALUE_SAMPLE_INTERVAL,
        };

        void set_charge_point_id(std::string charge_point_id);
        const std::string &get_charge_point_id() const;
        void set_connection_id(std::string connection_id);
        const std::string &get_connection_id() const;
        void set_force_protocol(std::string force_protocol);
        const std::string &get_force_protocol() const;
        void set_phases(uint8_t phases) { this->phases_ = phases; }
        uint8_t get_phases() const { return this->phases_; }
        void set_phase_mapping(std::initializer_list<uint8_t> phase_mapping) {
            this->phase_mapping_.fill(0);
            std::copy(phase_mapping.begin(), phase_mapping.end(), this->phase_mapping_.begin());
        }
        const std::array<uint8_t, 3> &get_phase_mapping() const { return this->phase_mapping_; }
        void set_phase_voltage(float phase_voltage) { this->phase_voltage_ = phase_voltage; }
        float get_phase_voltage() const { return this->phase_voltage_; }
        void set_online_binary_sensor(binary_sensor::BinarySensor *online_binary_sensor) {
            this->online_binary_sensor_ = online_binary_sensor;
        }
        void set_protocol_text_sensor(text_sensor::TextSensor *protocol_text_sensor) {
            this->protocol_text_sensor_ = protocol_text_sensor;
        }
        void set_charger_info_text_sensor(text_sensor::TextSensor *charger_info_text_sensor) {
            this->charger_info_text_sensor_ = charger_info_text_sensor;
        }
        void add_debug_ocpp_exclude_action(std::string action);
        bool is_debug_ocpp_action_excluded(const std::string &action) const;
        void set_debug_ocpp_messages(bool debug_ocpp_messages);
        bool get_debug_ocpp_messages() const;
        void set_max_current(uint32_t max_current);
        uint32_t get_max_current() const { return this->max_current_; }
        void set_startup_notifications_delay(uint32_t startup_notifications_delay_ms);
        uint32_t get_startup_notifications_delay() const;
        bool is_online() const;
        const std::string &get_meter_value_sample_interval() const { return this->meter_value_sample_interval_; }
        const std::string &get_meter_values_sampled_data() const { return this->meter_values_sampled_data_; }
        const std::string &get_connector_switch_3_to_1_phase_supported() const {
            return this->connector_switch_3_to_1_phase_supported_;
        }
        void set_max_queued_messages(size_t max_queued_messages) { this->max_queued_messages_ = max_queued_messages; }
        size_t get_max_queued_messages() const { return this->max_queued_messages_; }
        void add_connector(Connector *connector) {
            if (connector != nullptr) {
                const auto &connector_phase_mapping = connector->get_phase_mapping();
                std::array<uint8_t, 3> site_phase_mapping{{0, 0, 0}};
                for (size_t connector_phase = 0;
                     connector_phase < connector->get_phases() && connector_phase < site_phase_mapping.size();
                     connector_phase++) {
                    uint8_t charge_point_phase = connector_phase_mapping[connector_phase];
                    if (charge_point_phase < 1 || charge_point_phase > this->phase_mapping_.size())
                        continue;
                    site_phase_mapping[connector_phase] = this->phase_mapping_[charge_point_phase - 1];
                }
                connector->set_phase_mapping({site_phase_mapping[0], site_phase_mapping[1], site_phase_mapping[2]});
                connector->set_phase_voltage(this->phase_voltage_);
                connector->set_listener(this);
            }
            this->connectors_.push_back(connector);
        }

        void on_connected(std::string connection_id, uint32_t now_millis = 0);
        void on_connected(std::string connection_id, std::string protocol, uint32_t now_millis = 0);
        void on_disconnected();
        void handle_ocpp_text(const std::string &message, uint32_t now_millis = 0);
        void loop(uint32_t now_millis);
        bool pop_queued_message(std::string *message, uint32_t now_millis = 0);
        void on_connector_control_current_changed(
            Connector *connector,
            float old_control_current,
            float new_control_current
        ) override;

    protected:
        bool send_message_(QueuedMessage message);
        void handle_ocpp_message_(const OcppMessage &message);
        void handle_ocpp_call_(const OcppMessage &call);
        void handle_ocpp_call_reply_(const OcppMessage &message);
        void handle_authorize_(const Authorize &authorize);
        void handle_change_configuration_reply_(const OcppMessage &message);
        void handle_get_configuration_response_(const GetConfigurationResponse &message);
        void handle_start_transaction_(const StartTransaction &start_transaction);
        void handle_startup_notification_trigger_reply_(const OcppMessage &message);
        void handle_stop_transaction_(const StopTransaction &stop_transaction);
        bool should_log_debug_ocpp_message_(const OcppMessage &message) const;
        bool should_log_debug_ocpp_message_(const QueuedMessage &message) const;
        const std::string &debug_action_for_message_(const OcppMessage &message) const;
        bool send_meter_value_sample_interval_change_request_();
        bool send_meter_values_sampled_data_change_request_();
        bool send_connector_control_current_(Connector *connector);
        bool send_connector_remote_start_transaction_(Connector *connector);
        bool send_connector_remote_stop_transaction_(Connector *connector);
        void send_get_configuration_request_();
        void send_startup_notification_triggers_();
        void send_boot_notification_trigger_();
        void send_status_notification_trigger_();
        void expire_in_flight_call_(uint32_t now_millis);
        void set_online_(bool online);
        void publish_charger_info_(const BootNotification &boot_notification);
        void publish_meter_values_(const MeterValues &meter_values);
        void publish_status_notification_(const StatusNotification &status_notification);
        Connector *recover_active_transaction_id_from_meter_values_(const MeterValues &meter_values);
        Connector *find_connector_(uint32_t connector_id);
        Connector *find_connector_by_transaction_id_(uint32_t transaction_id);

        std::string charge_point_id_;
        std::string connection_id_;
        std::string forced_protocol_;
        OcppProtocol protocol_;
        std::vector<QueuedMessage> messages_;
        std::unique_ptr<QueuedMessage> in_flight_call_;
        std::string meter_value_sample_interval_;
        std::string meter_values_sampled_data_;
        std::string connector_switch_3_to_1_phase_supported_;
        binary_sensor::BinarySensor *online_binary_sensor_{nullptr};
        text_sensor::TextSensor *protocol_text_sensor_{nullptr};
        text_sensor::TextSensor *charger_info_text_sensor_{nullptr};
        std::vector<Connector *> connectors_;
        std::vector<std::string> debug_ocpp_exclude_actions_;
        uint8_t phases_{1};
        std::array<uint8_t, 3> phase_mapping_{{1, 2, 3}};
        uint32_t max_current_{0};
        float phase_voltage_{DEFAULT_PHASE_VOLTAGE};
        size_t max_queued_messages_{DEFAULT_MAX_QUEUED_MESSAGES};
        bool debug_ocpp_messages_{false};
        uint32_t startup_notifications_delay_ms_{DEFAULT_STARTUP_NOTIFICATIONS_DELAY_MS};
        bool boot_notification_pending_{true};
        bool status_notification_pending_{true};
        bool boot_notification_trigger_in_flight_{false};
        bool status_notification_trigger_in_flight_{false};
        bool connected_{false};
        bool online_{false};
        uint32_t connected_at_millis_{0};
        uint32_t current_millis_{0};
        uint32_t next_transaction_id_{1};
        uint32_t next_remote_start_transaction_sequence_{1};
        uint32_t next_remote_stop_transaction_sequence_{1};
        uint32_t next_set_charging_profile_sequence_{1};
        ChangeConfigurationStage change_configuration_stage_{ChangeConfigurationStage::IDLE};
        size_t meter_values_sampled_data_fallback_index_{0};
};

}  // namespace esphome::ocpp
