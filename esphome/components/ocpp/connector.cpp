#include "connector.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp.connector";
static constexpr float MIN_CHARGING_PROFILE_CURRENT = 6.0f;

bool get_plugged_from_status(const std::string &status) {
    if (status == "Preparing" || status == "Charging" || status == "SuspendedEVSE" || status == "SuspendedEV" ||
        status == "Finishing" || status == "Occupied") {
        return true;
    }
    return false;
}

}  // namespace

float calculate_control_current(
    float requested_current,
    float current_limit,
    uint32_t max_current
) {
    float control_current = std::min(requested_current, current_limit);
    if (max_current > 0)
        control_current = std::min(control_current, static_cast<float>(max_current));
    if (control_current > 0.0f && control_current < MIN_CHARGING_PROFILE_CURRENT)
        return 0.0f;
    return control_current;
}

// max_current is an installation limit; this method should only be called once during setup.
void Connector::set_max_current(uint32_t max_current) {
    this->max_current_ = max_current;
    this->current_limit_max_ = max_current;
    this->current_limit_ = static_cast<float>(max_current);
    this->requested_current_ = static_cast<float>(max_current);
    this->update_needed_current_();
    this->update_control_current_();
}

// current_limit_max configures the current_limit number range; this method should only be called once during setup.
void Connector::set_current_limit_max(uint32_t current_limit_max) {
    if (this->max_current_ > 0 && current_limit_max > this->max_current_)
        current_limit_max = this->max_current_;
    this->current_limit_max_ = current_limit_max;
    this->current_limit_ = static_cast<float>(current_limit_max);
    if (this->current_limit_number_ != nullptr)
        this->current_limit_number_->publish_state(this->current_limit_);
    this->update_needed_current_();
    this->update_control_current_();
}

void Connector::set_needed_current_l1_sensor(sensor::Sensor *needed_current_l1_sensor) {
    this->needed_current_l1_sensor_ = needed_current_l1_sensor;
    if (this->needed_current_l1_sensor_ != nullptr)
        this->needed_current_l1_sensor_->publish_state(this->needed_current_l1_);
}

void Connector::set_needed_current_l2_sensor(sensor::Sensor *needed_current_l2_sensor) {
    this->needed_current_l2_sensor_ = needed_current_l2_sensor;
    if (this->needed_current_l2_sensor_ != nullptr)
        this->needed_current_l2_sensor_->publish_state(this->needed_current_l2_);
}

void Connector::set_needed_current_l3_sensor(sensor::Sensor *needed_current_l3_sensor) {
    this->needed_current_l3_sensor_ = needed_current_l3_sensor;
    if (this->needed_current_l3_sensor_ != nullptr)
        this->needed_current_l3_sensor_->publish_state(this->needed_current_l3_);
}

void Connector::set_control_current_sensor(sensor::Sensor *control_current_sensor) {
    this->control_current_sensor_ = control_current_sensor;
    if (this->control_current_sensor_ != nullptr)
        this->control_current_sensor_->publish_state(this->control_current_);
}

void Connector::set_current_limit_number(CurrentLimit *current_limit_number) {
    this->current_limit_number_ = current_limit_number;
    if (this->current_limit_number_ != nullptr)
        this->current_limit_number_->publish_state(this->current_limit_);
}

void Connector::set_requested_current_number(RequestedCurrent *requested_current_number) {
    this->requested_current_number_ = requested_current_number;
    if (this->requested_current_number_ != nullptr)
        this->requested_current_number_->publish_state(this->requested_current_);
}

void Connector::set_current_limit(float current_limit) {
    this->current_limit_ = this->clamp_current_limit_(std::round(current_limit));
    if (this->current_limit_number_ != nullptr)
        this->current_limit_number_->publish_state(this->current_limit_);
    this->update_needed_current_();
    this->update_control_current_();
}

void Connector::set_requested_current(float requested_current) {
    this->requested_current_ = this->clamp_current_(std::round(requested_current * 10.0f) / 10.0f);
    if (this->requested_current_number_ != nullptr)
        this->requested_current_number_->publish_state(this->requested_current_);
    this->update_control_current_();
}

float Connector::clamp_current_(float value) const {
    if (value < 0.0f)
        return 0.0f;
    if (this->max_current_ > 0 && value > static_cast<float>(this->max_current_))
        return static_cast<float>(this->max_current_);
    return value;
}

float Connector::clamp_current_limit_(float value) const {
    if (value < 0.0f)
        return 0.0f;
    if (this->current_limit_max_ > 0 && value > static_cast<float>(this->current_limit_max_))
        return static_cast<float>(this->current_limit_max_);
    return this->clamp_current_(value);
}

std::array<float, 3> Connector::map_phases(std::array<float, 3> currents) const {
    std::array<float, 3> mapped_currents{0.0f, 0.0f, 0.0f};
    uint8_t phases = std::min<uint8_t>(this->phases_, 3);
    for (uint8_t connector_phase = 0; connector_phase < phases; connector_phase++) {
        uint8_t mapped_phase = this->phase_mapping_[connector_phase];
        if (mapped_phase == 0)
            continue;
        mapped_currents[mapped_phase - 1] = currents[connector_phase];
    }
    return mapped_currents;
}

void Connector::update_needed_current_() {
    float needed_current_l1 = 0.0f;
    float needed_current_l2 = 0.0f;
    float needed_current_l3 = 0.0f;
    uint8_t phases;

    if (this->is_plugged() && this->status_ != "SuspendedEV") {
        float needed_current = std::min(this->current_limit_, static_cast<float>(this->max_current_));
        if (needed_current < MIN_CHARGING_PROFILE_CURRENT)
            needed_current = 0.0f;

        // If active_phases is not yet available we assume all the phases are used
        if (this->active_phases_ == 0) {
            phases = this->get_phases();
        } else {
            phases = this->active_phases_;
        }

        if (phases >= 1)
            needed_current_l1 = needed_current;
        if (phases >= 2)
            needed_current_l2 = needed_current;
        if (phases >= 3)
            needed_current_l3 = needed_current;

        auto needed_currents = this->map_phases({needed_current_l1, needed_current_l2, needed_current_l3});
        needed_current_l1 = needed_currents[0];
        needed_current_l2 = needed_currents[1];
        needed_current_l3 = needed_currents[2];
    }

    bool updated = false;
    if (needed_current_l1 != this->needed_current_l1_) {
        updated = true;
        this->set_needed_current_l1_(needed_current_l1);
    }
    if (needed_current_l2 != this->needed_current_l2_) {
        updated = true;
        this->set_needed_current_l2_(needed_current_l2);
    }
    if (needed_current_l3 != this->needed_current_l3_) {
        updated = true;
        this->set_needed_current_l3_(needed_current_l3);
    }
}

void Connector::update_control_current_() {
    float old_control_current = this->control_current_;
    this->control_current_ = calculate_control_current(
        this->requested_current_,
        this->current_limit_,
        this->max_current_
    );
    if (this->control_current_sensor_ != nullptr)
        this->control_current_sensor_->publish_state(this->control_current_);
    if (this->listener_ != nullptr && this->control_current_ != old_control_current)
        this->listener_->on_connector_control_current_changed(this, old_control_current, this->control_current_);
}

void Connector::set_needed_current_l1_(float needed_current_l1) {
    this->needed_current_l1_ = needed_current_l1;
    if (this->needed_current_l1_sensor_ != nullptr)
        this->needed_current_l1_sensor_->publish_state(this->needed_current_l1_);
}

void Connector::set_needed_current_l2_(float needed_current_l2) {
    this->needed_current_l2_ = needed_current_l2;
    if (this->needed_current_l2_sensor_ != nullptr)
        this->needed_current_l2_sensor_->publish_state(this->needed_current_l2_);
}

void Connector::set_needed_current_l3_(float needed_current_l3) {
    this->needed_current_l3_ = needed_current_l3;
    if (this->needed_current_l3_sensor_ != nullptr)
        this->needed_current_l3_sensor_->publish_state(this->needed_current_l3_);
}

void Connector::clear_active_transaction() {
    this->active_transaction_id_ = 0;
}

void Connector::set_phases(uint8_t phases) {
    this->phases_ = phases;
    this->reset_active_phases();
}

void Connector::set_active_phases(uint8_t active_phases) {
    if (this->active_phases_ == active_phases)
        return;
    this->active_phases_ = active_phases;
    this->update_needed_current_();
    this->publish_active_phases_sensor();
}

void Connector::publish_active_phases_sensor() {
    if (this->active_phases_sensor_ != nullptr)
        this->active_phases_sensor_->publish_state(this->active_phases_ == 0 ? NAN : static_cast<float>(this->active_phases_));
}

void Connector::set_active_phases_sensor(sensor::Sensor *active_phases_sensor) {
    this->active_phases_sensor_ = active_phases_sensor;
    this->publish_active_phases_sensor();
}

void Connector::reset_active_phases() {
    uint8_t active_phases = this->phases_ == 1 ? 1 : 0;
    bool updated = this->active_phases_ != active_phases;
    this->set_active_phases(active_phases);
    if (updated)
        this->update_needed_current_();
}

void Connector::loop(uint32_t now_millis) {
    this->last_update_millis_ = now_millis;
    if (this->plugged_)
        this->update_session_time_(now_millis);
}

void Connector::set_plugged_(bool plugged) {
    if (this->plugged_ != plugged) {
        this->plugged_ = plugged;
        this->reset_active_phases();
        if (this->plugged_)
            this->on_session_start();
        else
            this->on_session_stop();
    }
    if (this->plugged_binary_sensor_ != nullptr)
        this->plugged_binary_sensor_->publish_state(this->plugged_);
}

void Connector::on_session_start() {
    this->session_start_energy_ = this->last_total_energy_;
    this->session_time_ = 0;
    this->session_start_millis_ = this->last_update_millis_;
    if (this->session_energy_sensor_ != nullptr)
        this->session_energy_sensor_->publish_state(0.0f);
    if (this->session_time_sensor_ != nullptr)
        this->session_time_sensor_->publish_state(0.0f);
    ESP_LOGD(TAG, "Connector %u session started", static_cast<unsigned>(this->connector_id_));
}

void Connector::on_session_stop() {
    this->update_session_time_(this->last_update_millis_);
    ESP_LOGD(TAG, "Connector %u session stopped", static_cast<unsigned>(this->connector_id_));
}

void Connector::update_session_energy_(float total_energy) {
    if (std::isnan(total_energy))
        return;
    this->last_total_energy_ = total_energy;
    if (!this->plugged_)
        return;
    if (std::isnan(this->session_start_energy_))
        this->session_start_energy_ = total_energy;
    float session_energy = total_energy - this->session_start_energy_;
    if (session_energy < 0.0f)
        session_energy = 0.0f;
    if (this->session_energy_sensor_ != nullptr)
        this->session_energy_sensor_->publish_state(session_energy);
}

void Connector::update_session_time_(uint32_t now_millis) {
    uint32_t session_time = (now_millis - this->session_start_millis_) / 1000;
    if (session_time == this->session_time_)
        return;
    this->session_time_ = session_time;
    if (this->session_time_sensor_ != nullptr)
        this->session_time_sensor_->publish_state(static_cast<float>(session_time));
}

void Connector::publish_meter_values(const std::string &connection_id, const MeterValues &meter_values) {
    MeterValues derived_meter_values = meter_values;
    float latched_active_phases = this->plugged_ && this->active_phases_ != 0 ? static_cast<float>(this->active_phases_) : NAN;
    derived_meter_values.calculate_phase_values(this->phases_, this->phase_voltage_, latched_active_phases);
    if (this->plugged_) {
        if (this->active_phases_ == 0 && !std::isnan(derived_meter_values.active_phases)) {
            this->set_active_phases(static_cast<uint8_t>(std::round(derived_meter_values.active_phases)));
        }
        if (this->active_phases_ != 0 && derived_meter_values.active_phases != static_cast<float>(this->active_phases_))
            derived_meter_values.calculate_phase_values(this->phases_, this->phase_voltage_, static_cast<float>(this->active_phases_));
    }

    if (this->log_meter_values_ && !connection_id.empty()) {
        std::string summary = derived_meter_values.sampled_values_summary();
        if (!summary.empty())
            ESP_LOGI(TAG, "%s %u MeterValues %s", connection_id.c_str(),
                    static_cast<unsigned>(derived_meter_values.connector_id), summary.c_str());
    }
    if (this->current_sensor_ != nullptr)
        this->current_sensor_->publish_state(derived_meter_values.current);
    if (this->current_l1_sensor_ != nullptr)
        this->current_l1_sensor_->publish_state(derived_meter_values.current_l1);
    if (this->current_l2_sensor_ != nullptr)
        this->current_l2_sensor_->publish_state(derived_meter_values.current_l2);
    if (this->current_l3_sensor_ != nullptr)
        this->current_l3_sensor_->publish_state(derived_meter_values.current_l3);
    if (this->power_sensor_ != nullptr)
        this->power_sensor_->publish_state(derived_meter_values.power);
    if (this->total_energy_sensor_ != nullptr)
        this->total_energy_sensor_->publish_state(derived_meter_values.energy);
    this->update_session_energy_(derived_meter_values.energy);
    if (this->voltage_sensor_ != nullptr)
        this->voltage_sensor_->publish_state(derived_meter_values.voltage);
    if (this->voltage_l1_sensor_ != nullptr)
        this->voltage_l1_sensor_->publish_state(derived_meter_values.voltage_l1);
    if (this->voltage_l2_sensor_ != nullptr)
        this->voltage_l2_sensor_->publish_state(derived_meter_values.voltage_l2);
    if (this->voltage_l3_sensor_ != nullptr)
        this->voltage_l3_sensor_->publish_state(derived_meter_values.voltage_l3);
    if (this->active_phases_sensor_ != nullptr)
        this->active_phases_sensor_->publish_state(
            this->active_phases_ == 0 ? NAN : static_cast<float>(this->active_phases_));
}

void Connector::publish_status_notification(const StatusNotification &status_notification, uint32_t now_millis) {
    this->last_update_millis_ = now_millis;
    // Error
    std::string error_code = status_notification.error_code;
    if (error_code == "NoError")
        error_code.clear();
    if (this->error_text_sensor_ != nullptr)
        this->error_text_sensor_->publish_state(error_code);
    // Plugged
    bool plugged = get_plugged_from_status(status_notification.status);
    this->set_plugged_(plugged);
    // Status
    if (this->status_ != status_notification.status) {
        this->status_ = status_notification.status;
        this->update_needed_current_();
    }
    if (this->status_text_sensor_ != nullptr)
        this->status_text_sensor_->publish_state(status_notification.status);
}

void Connector::publish_unavailable() {
    this->clear_active_transaction();
    this->reset_active_phases();
    this->publish_meter_values("", MeterValues("", this->connector_id_));
    this->publish_status_notification(StatusNotification("", this->connector_id_));
}

void CurrentLimit::control(float value) {
    if (this->connector_ == nullptr)
        return;
    this->connector_->set_current_limit(value);
}

void RequestedCurrent::control(float value) {
    if (this->connector_ == nullptr)
        return;
    this->connector_->set_requested_current(value);
}

}  // namespace esphome::ocpp
