#pragma once

#include "protocol.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <cstdint>
#include <cstddef>
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

class Connector {
    public:
        static constexpr uint32_t DEFAULT_CONNECTOR_ID = 1;

        void set_connector_id(uint32_t connector_id) { this->connector_id_ = connector_id; }
        uint32_t get_connector_id() const { return this->connector_id_; }
        void set_current_sensor(sensor::Sensor *current_sensor) { this->current_sensor_ = current_sensor; }
        void set_power_sensor(sensor::Sensor *power_sensor) { this->power_sensor_ = power_sensor; }
        void set_energy_sensor(sensor::Sensor *energy_sensor) { this->energy_sensor_ = energy_sensor; }
        void set_voltage_sensor(sensor::Sensor *voltage_sensor) { this->voltage_sensor_ = voltage_sensor; }
        void set_status_text_sensor(text_sensor::TextSensor *status_text_sensor) { this->status_text_sensor_ = status_text_sensor; }
        void set_error_text_sensor(text_sensor::TextSensor *error_text_sensor) { this->error_text_sensor_ = error_text_sensor; }
        bool has_active_transaction() const { return this->active_transaction_id_ != 0; }
        uint32_t get_active_transaction_id() const { return this->active_transaction_id_; }
        void set_active_transaction_id(uint32_t active_transaction_id) { this->active_transaction_id_ = active_transaction_id; }
        void clear_active_transaction() { this->active_transaction_id_ = 0; }
        void publish_meter_values(const MeterValues &meter_values);
        void publish_status_notification(const StatusNotification &status_notification);
        void publish_unavailable();

    protected:
        uint32_t connector_id_{DEFAULT_CONNECTOR_ID};
        sensor::Sensor *current_sensor_{nullptr};
        sensor::Sensor *power_sensor_{nullptr};
        sensor::Sensor *energy_sensor_{nullptr};
        sensor::Sensor *voltage_sensor_{nullptr};
        text_sensor::TextSensor *status_text_sensor_{nullptr};
        text_sensor::TextSensor *error_text_sensor_{nullptr};
        uint32_t active_transaction_id_{0};
};

class ChargePoint {
    public:
        static constexpr size_t DEFAULT_MAX_QUEUED_MESSAGES = 8;
        static constexpr uint32_t DEFAULT_STARTUP_NOTIFICATIONS_DELAY_MS = 300000;
        static constexpr uint32_t DEFAULT_CALL_TIMEOUT_MS = 60000;

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
        void set_online_binary_sensor(binary_sensor::BinarySensor *online_binary_sensor) {
            this->online_binary_sensor_ = online_binary_sensor;
        }
        void set_protocol_text_sensor(text_sensor::TextSensor *protocol_text_sensor) {
            this->protocol_text_sensor_ = protocol_text_sensor;
        }
        void set_charger_info_text_sensor(text_sensor::TextSensor *charger_info_text_sensor) {
            this->charger_info_text_sensor_ = charger_info_text_sensor;
        }
        void set_debug_ocpp_messages(bool debug_ocpp_messages);
        bool get_debug_ocpp_messages() const;
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
        void add_connector(Connector *connector) { this->connectors_.push_back(connector); }

        void on_connected(std::string connection_id, uint32_t now_millis = 0);
        void on_connected(std::string connection_id, std::string protocol, uint32_t now_millis = 0);
        void on_disconnected();
        void handle_ocpp_text(const std::string &message);
        void loop(uint32_t now_millis);
        bool pop_queued_message(std::string *message, uint32_t now_millis = 0);

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
        bool send_meter_value_sample_interval_change_request_();
        bool send_meter_values_sampled_data_change_request_();
        void send_get_configuration_request_();
        void send_startup_notification_triggers_();
        void send_boot_notification_trigger_();
        void send_status_notification_trigger_();
        void expire_in_flight_call_(uint32_t now_millis);
        void set_online_(bool online);
        void publish_charger_info_(const BootNotification &boot_notification);
        void publish_meter_values_(const MeterValues &meter_values);
        void publish_status_notification_(const StatusNotification &status_notification);
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
        uint32_t next_transaction_id_{1};
        ChangeConfigurationStage change_configuration_stage_{ChangeConfigurationStage::IDLE};
        size_t meter_values_sampled_data_fallback_index_{0};
};

}  // namespace esphome::ocpp
