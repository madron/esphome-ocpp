#pragma once

#include "message.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace esphome::ocpp {

class CurrentLimit;
class RequestedCurrent;

float calculate_control_current(float requested_current, float current_limit, uint32_t max_current);

class Connector {
    public:
        static constexpr uint32_t DEFAULT_CONNECTOR_ID = 1;

        void set_connector_id(uint32_t connector_id) { this->connector_id_ = connector_id; }
        uint32_t get_connector_id() const { return this->connector_id_; }
        void set_phases(uint8_t phases) { this->phases_ = phases; }
        uint8_t get_phases() const { return this->phases_; }
        void set_phase_mapping(uint8_t connector_phase, uint8_t supply_phase) {
            if (connector_phase < 1 || connector_phase > 3)
                return;
            this->phase_mapping_[connector_phase - 1] = supply_phase;
        }
        uint8_t get_phase_mapping(uint8_t connector_phase) const {
            if (connector_phase < 1 || connector_phase > 3)
                return 0;
            return this->phase_mapping_[connector_phase - 1];
        }
        void set_phase_voltage(float phase_voltage) { this->phase_voltage_ = phase_voltage; }
        float get_active_phases() const { return this->active_phases_; }
        void set_log_meter_values(bool log_meter_values) { this->log_meter_values_ = log_meter_values; }
        bool get_log_meter_values() const { return this->log_meter_values_; }
        void set_max_current(uint32_t max_current);
        uint32_t get_max_current() const { return this->max_current_; }
        void set_current_limit_max(uint32_t current_limit_max);
        uint32_t get_current_limit_max() const { return this->current_limit_max_; }
        void set_current_sensor(sensor::Sensor *current_sensor) { this->current_sensor_ = current_sensor; }
        void set_current_l1_sensor(sensor::Sensor *current_l1_sensor) { this->current_l1_sensor_ = current_l1_sensor; }
        void set_current_l2_sensor(sensor::Sensor *current_l2_sensor) { this->current_l2_sensor_ = current_l2_sensor; }
        void set_current_l3_sensor(sensor::Sensor *current_l3_sensor) { this->current_l3_sensor_ = current_l3_sensor; }
        void set_control_current_sensor(sensor::Sensor *control_current_sensor);
        void set_current_limit_number(CurrentLimit *current_limit_number);
        void set_requested_current_number(RequestedCurrent *requested_current_number);
        void set_power_sensor(sensor::Sensor *power_sensor) { this->power_sensor_ = power_sensor; }
        void set_total_energy_sensor(sensor::Sensor *total_energy_sensor) { this->total_energy_sensor_ = total_energy_sensor; }
        void set_session_energy_sensor(sensor::Sensor *session_energy_sensor) {
            this->session_energy_sensor_ = session_energy_sensor;
        }
        void set_session_time_sensor(sensor::Sensor *session_time_sensor) { this->session_time_sensor_ = session_time_sensor; }
        void set_voltage_sensor(sensor::Sensor *voltage_sensor) { this->voltage_sensor_ = voltage_sensor; }
        void set_voltage_l1_sensor(sensor::Sensor *voltage_l1_sensor) { this->voltage_l1_sensor_ = voltage_l1_sensor; }
        void set_voltage_l2_sensor(sensor::Sensor *voltage_l2_sensor) { this->voltage_l2_sensor_ = voltage_l2_sensor; }
        void set_voltage_l3_sensor(sensor::Sensor *voltage_l3_sensor) { this->voltage_l3_sensor_ = voltage_l3_sensor; }
        void set_active_phases_sensor(sensor::Sensor *active_phases_sensor) { this->active_phases_sensor_ = active_phases_sensor; }
        void set_status_text_sensor(text_sensor::TextSensor *status_text_sensor) { this->status_text_sensor_ = status_text_sensor; }
        void set_error_text_sensor(text_sensor::TextSensor *error_text_sensor) { this->error_text_sensor_ = error_text_sensor; }
        void set_plugged_binary_sensor(binary_sensor::BinarySensor *plugged_binary_sensor) {
            this->plugged_binary_sensor_ = plugged_binary_sensor;
            if (this->plugged_binary_sensor_ != nullptr)
                this->plugged_binary_sensor_->publish_initial_state(this->plugged_);
        }
        bool is_plugged() const { return this->plugged_; }
        bool has_active_transaction() const { return this->active_transaction_id_ != 0; }
        uint32_t get_active_transaction_id() const { return this->active_transaction_id_; }
        void set_active_transaction_id(uint32_t active_transaction_id) { this->active_transaction_id_ = active_transaction_id; }
        void clear_active_transaction();
        void reset_active_phases();
        void set_current_limit(float current_limit);
        float get_current_limit() const { return this->current_limit_; }
        void set_requested_current(float requested_current);
        float get_requested_current() const { return this->requested_current_; }
        float get_control_current() const { return this->control_current_; }
        void loop(uint32_t now_millis);
        void publish_meter_values(const std::string &connection_id, const MeterValues &meter_values);
        void publish_status_notification(const StatusNotification &status_notification, uint32_t now_millis = 0);
        void publish_unavailable();

    protected:
        float clamp_current_(float value) const;
        float clamp_current_limit_(float value) const;
        void update_control_current_();
        void set_plugged_(bool plugged);
        void update_session_energy_(float total_energy);
        void update_session_time_(uint32_t now_millis);
        // Called when the connector starts a charging session, which is defined as the car becoming plugged in.
        // This hook is the place for future per-session initialization such as latching initial meter values.
        // If overridden, call Connector::on_session_start() to keep built-in session sensors working.
        virtual void on_session_start();
        // Called when the connector stops a charging session, which is defined as the car becoming unplugged.
        // This hook is the place for future per-session cleanup or summary publishing.
        // If overridden, call Connector::on_session_stop() to keep built-in session sensors working.
        virtual void on_session_stop();

        uint32_t connector_id_{DEFAULT_CONNECTOR_ID};
        uint8_t phases_{1};
        uint8_t phase_mapping_[3]{1, 2, 3};
        uint32_t max_current_{0};
        float phase_voltage_{DEFAULT_PHASE_VOLTAGE};
        uint32_t current_limit_max_{0};
        float current_limit_{0.0f};
        float requested_current_{0.0f};
        float control_current_{0.0f};
        float active_phases_{NAN};
        sensor::Sensor *current_sensor_{nullptr};
        sensor::Sensor *current_l1_sensor_{nullptr};
        sensor::Sensor *current_l2_sensor_{nullptr};
        sensor::Sensor *current_l3_sensor_{nullptr};
        sensor::Sensor *control_current_sensor_{nullptr};
        CurrentLimit *current_limit_number_{nullptr};
        RequestedCurrent *requested_current_number_{nullptr};
        sensor::Sensor *power_sensor_{nullptr};
        sensor::Sensor *total_energy_sensor_{nullptr};
        sensor::Sensor *session_energy_sensor_{nullptr};
        sensor::Sensor *session_time_sensor_{nullptr};
        sensor::Sensor *voltage_sensor_{nullptr};
        sensor::Sensor *voltage_l1_sensor_{nullptr};
        sensor::Sensor *voltage_l2_sensor_{nullptr};
        sensor::Sensor *voltage_l3_sensor_{nullptr};
        sensor::Sensor *active_phases_sensor_{nullptr};
        text_sensor::TextSensor *status_text_sensor_{nullptr};
        text_sensor::TextSensor *error_text_sensor_{nullptr};
        binary_sensor::BinarySensor *plugged_binary_sensor_{nullptr};
        float last_total_energy_{NAN};
        float session_start_energy_{NAN};
        uint32_t session_time_{0};
        uint32_t session_start_millis_{0};
        uint32_t last_update_millis_{0};
        bool plugged_{false};
        uint32_t active_transaction_id_{0};
        bool log_meter_values_{false};
};

class CurrentLimit : public number::Number {
    public:
        void set_connector(Connector *connector) { this->connector_ = connector; }

    protected:
        void control(float value) override;

        Connector *connector_{nullptr};
};

class RequestedCurrent : public number::Number {
    public:
        void set_connector(Connector *connector) { this->connector_ = connector; }

    protected:
        void control(float value) override;

        Connector *connector_{nullptr};
};

}  // namespace esphome::ocpp
