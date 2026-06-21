#include "charge_point.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp.charge_point";
static const char *const CHANGE_CONFIGURATION_METER_VALUE_SAMPLE_INTERVAL_UNIQUE_ID =
    "change-config-meter-value-sample-interval";
static const char *const CHANGE_CONFIGURATION_METER_VALUES_SAMPLED_DATA_UNIQUE_ID =
    "change-config-meter-values-sampled-data";
static const char *const GET_CONFIGURATION_UNIQUE_ID = "get-configuration";
static const char *const METER_VALUE_SAMPLE_INTERVAL_KEY = "MeterValueSampleInterval";
static constexpr const char *METER_VALUE_SAMPLE_INTERVAL_VALUE = "5";
static const char *const METER_VALUES_SAMPLED_DATA_KEY = "MeterValuesSampledData";
static constexpr const char *const METER_VALUES_SAMPLED_DATA_FALLBACKS[] = {
    "Current.Import,Power.Active.Import,Energy.Active.Import.Register,Voltage",
    "Current.Import,Power.Active.Import,Energy.Active.Import.Register",
    "Current.Import,Power.Active.Import",
    "Current.Import",
};
static const char *const TRIGGER_BOOT_NOTIFICATION_UNIQUE_ID = "trigger-boot-notification";
static const char *const TRIGGER_STATUS_NOTIFICATION_UNIQUE_ID = "trigger-status-notification";
static constexpr size_t DEBUG_OCPP_MESSAGE_CHUNK_SIZE = 360;
static constexpr float MIN_CHARGING_PROFILE_CURRENT = 6.0f;

bool status_notification_to_plugged(const std::string &status, bool *plugged) {
    if (status == "Preparing" || status == "Charging" || status == "SuspendedEVSE" || status == "SuspendedEV" ||
        status == "Finishing" || status == "Occupied") {
        *plugged = true;
        return true;
    }
    if (status == "Available" || status == "Reserved" || status == "Unavailable") {
        *plugged = false;
        return true;
    }
    return false;
}

void log_ocpp_debug_message(const std::string &connection_id, const char *direction, const std::string &message) {
    if (message.size() <= DEBUG_OCPP_MESSAGE_CHUNK_SIZE) {
        ESP_LOGD(TAG, "%s %s %s", connection_id.c_str(), direction, message.c_str());
        return;
    }

    size_t chunks = (message.size() + DEBUG_OCPP_MESSAGE_CHUNK_SIZE - 1) / DEBUG_OCPP_MESSAGE_CHUNK_SIZE;
    ESP_LOGD(TAG, "%s %s payload_len=%u chunks=%u", connection_id.c_str(), direction,
            static_cast<unsigned>(message.size()), static_cast<unsigned>(chunks));
    for (size_t chunk = 0; chunk < chunks; chunk++) {
        size_t offset = chunk * DEBUG_OCPP_MESSAGE_CHUNK_SIZE;
        std::string part = message.substr(offset, DEBUG_OCPP_MESSAGE_CHUNK_SIZE);
        ESP_LOGD(TAG, "%s %s[%u/%u] %s", connection_id.c_str(), direction, static_cast<unsigned>(chunk + 1),
                static_cast<unsigned>(chunks), part.c_str());
    }
}

}  // namespace

float calculate_control_current(float requested_current, float current_limit) {
    float control_current = std::min(requested_current, current_limit);
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
    this->update_control_current_();
}

void Connector::set_current_limit_max(uint32_t current_limit_max) {
    if (this->max_current_ > 0 && current_limit_max > this->max_current_)
        current_limit_max = this->max_current_;
    this->current_limit_max_ = current_limit_max;
    this->current_limit_ = static_cast<float>(current_limit_max);
    if (this->current_limit_number_ != nullptr)
        this->current_limit_number_->publish_state(this->current_limit_);
    this->update_control_current_();
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

void Connector::update_control_current_() {
    this->control_current_ = calculate_control_current(this->requested_current_, this->current_limit_);
    if (this->control_current_sensor_ != nullptr)
        this->control_current_sensor_->publish_state(this->control_current_);
}

void Connector::clear_active_transaction() {
    this->active_transaction_id_ = 0;
}

void Connector::reset_active_phases() {
    this->active_phases_ = NAN;
    if (this->active_phases_sensor_ != nullptr)
        this->active_phases_sensor_->publish_state(NAN);
}

void Connector::loop(uint32_t now_millis) {
    this->last_update_millis_ = now_millis;
    if (this->plugged_)
        this->update_session_time_(now_millis);
}

void Connector::set_plugged_(bool plugged) {
    bool changed = this->plugged_ != plugged;
    this->plugged_ = plugged;
    if (changed)
        this->reset_active_phases();
    if (this->plugged_binary_sensor_ != nullptr)
        this->plugged_binary_sensor_->publish_state(plugged);
    if (changed) {
        if (plugged)
            this->on_session_start();
        else
            this->on_session_stop();
    }
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
    float latched_active_phases = this->plugged_ ? this->active_phases_ : NAN;
    derived_meter_values.calculate_phase_values(this->phases_, this->phase_voltage_, latched_active_phases);
    if (this->plugged_) {
        if (std::isnan(this->active_phases_) && !std::isnan(derived_meter_values.active_phases))
            this->active_phases_ = derived_meter_values.active_phases;
        if (!std::isnan(this->active_phases_) && derived_meter_values.active_phases != this->active_phases_)
            derived_meter_values.calculate_phase_values(this->phases_, this->phase_voltage_, this->active_phases_);
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
        this->active_phases_sensor_->publish_state(this->active_phases_);
}

void Connector::publish_status_notification(const StatusNotification &status_notification, uint32_t now_millis) {
    this->last_update_millis_ = now_millis;
    if (this->status_text_sensor_ != nullptr)
        this->status_text_sensor_->publish_state(status_notification.status);
    std::string error_code = status_notification.error_code;
    if (error_code == "NoError")
        error_code.clear();
    if (this->error_text_sensor_ != nullptr)
        this->error_text_sensor_->publish_state(error_code);
    bool plugged = false;
    if (status_notification_to_plugged(status_notification.status, &plugged))
        this->set_plugged_(plugged);
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

void ChargePoint::set_charge_point_id(std::string charge_point_id) {
    this->charge_point_id_ = std::move(charge_point_id);
    this->connection_id_ = this->charge_point_id_;
}
const std::string &ChargePoint::get_charge_point_id() const { return this->charge_point_id_; }

void ChargePoint::set_connection_id(std::string connection_id) { this->connection_id_ = std::move(connection_id); }
const std::string &ChargePoint::get_connection_id() const { return this->connection_id_; }

void ChargePoint::set_force_protocol(std::string force_protocol) { this->forced_protocol_ = std::move(force_protocol); }
const std::string &ChargePoint::get_force_protocol() const { return this->forced_protocol_; }

void ChargePoint::add_debug_ocpp_exclude_action(std::string action) {
    if (!this->is_debug_ocpp_action_excluded(action))
        this->debug_ocpp_exclude_actions_.push_back(std::move(action));
}

bool ChargePoint::is_debug_ocpp_action_excluded(const std::string &action) const {
    return std::find(this->debug_ocpp_exclude_actions_.begin(), this->debug_ocpp_exclude_actions_.end(), action) !=
           this->debug_ocpp_exclude_actions_.end();
}

void ChargePoint::set_debug_ocpp_messages(bool debug_ocpp_messages) { this->debug_ocpp_messages_ = debug_ocpp_messages; }
bool ChargePoint::get_debug_ocpp_messages() const { return this->debug_ocpp_messages_; }
void ChargePoint::set_max_current(uint32_t max_current) { this->max_current_ = max_current; }
void ChargePoint::set_startup_notifications_delay(uint32_t startup_notifications_delay_ms) {
    this->startup_notifications_delay_ms_ = startup_notifications_delay_ms;
}
uint32_t ChargePoint::get_startup_notifications_delay() const { return this->startup_notifications_delay_ms_; }
bool ChargePoint::is_online() const { return this->online_; }

void ChargePoint::on_connected(std::string connection_id, uint32_t now_millis) {
    this->on_connected(std::move(connection_id), "", now_millis);
}

void ChargePoint::on_connected(std::string connection_id, std::string protocol, uint32_t now_millis) {
    this->set_connection_id(std::move(connection_id));
    this->protocol_.set_websocket_protocol(protocol);
    this->connected_ = true;
    this->connected_at_millis_ = now_millis;
    if (this->protocol_text_sensor_ != nullptr)
        this->protocol_text_sensor_->publish_state(protocol);
    if (this->charger_info_text_sensor_ != nullptr)
        this->charger_info_text_sensor_->publish_state("");
}

void ChargePoint::on_disconnected() {
    this->connected_ = false;
    this->boot_notification_trigger_in_flight_ = false;
    this->change_configuration_stage_ = ChangeConfigurationStage::IDLE;
    this->meter_values_sampled_data_fallback_index_ = 0;
    this->status_notification_trigger_in_flight_ = false;
    this->in_flight_call_.reset();
    this->messages_.clear();
    if (this->protocol_text_sensor_ != nullptr)
        this->protocol_text_sensor_->publish_state("");
    if (this->charger_info_text_sensor_ != nullptr)
        this->charger_info_text_sensor_->publish_state("");
    for (auto *connector : this->connectors_) {
        if (connector != nullptr)
            connector->publish_unavailable();
    }
    this->set_online_(false);
}

void ChargePoint::handle_ocpp_text(const std::string &message, uint32_t now_millis) {
    this->current_millis_ = now_millis;
    std::unique_ptr<OcppMessage> ocpp_message = this->protocol_.parse_message(message);
    if (this->debug_ocpp_messages_ &&
        (ocpp_message == nullptr || this->should_log_debug_ocpp_message_(*ocpp_message)))
        log_ocpp_debug_message(this->connection_id_, "<<", message);
    if (ocpp_message == nullptr)
        return;
    this->handle_ocpp_message_(*ocpp_message);
}

void ChargePoint::loop(uint32_t now_millis) {
    this->current_millis_ = now_millis;
    for (auto *connector : this->connectors_) {
        if (connector != nullptr)
            connector->loop(now_millis);
    }
    if (!this->connected_)
        return;
    this->expire_in_flight_call_(now_millis);
    if (this->startup_notifications_delay_ms_ == 0)
        return;
    if (now_millis - this->connected_at_millis_ < this->startup_notifications_delay_ms_)
        return;
    this->send_startup_notification_triggers_();
}

bool ChargePoint::pop_queued_message(std::string *message, uint32_t now_millis) {
    if (message == nullptr || this->messages_.empty())
        return false;

    auto selected = this->messages_.end();
    for (auto it = this->messages_.begin(); it != this->messages_.end(); ++it) {
        if (it->message_type_id == OcppMessageType::CALL && this->in_flight_call_ != nullptr)
            continue;
        selected = it;
        break;
    }
    if (selected == this->messages_.end())
        return false;

    QueuedMessage queued_message = std::move(*selected);
    this->messages_.erase(selected);
    if (queued_message.message_type_id == OcppMessageType::CALL) {
        queued_message.sent_at_millis = now_millis;
        this->in_flight_call_.reset(new QueuedMessage(queued_message));
    }
    if (this->debug_ocpp_messages_ && this->should_log_debug_ocpp_message_(queued_message))
        log_ocpp_debug_message(this->connection_id_, ">>", queued_message.payload);
    *message = std::move(queued_message.payload);
    return true;
}

bool ChargePoint::send_message_(QueuedMessage message) {
    if (this->messages_.size() >= this->max_queued_messages_) {
        ESP_LOGW(TAG, "Dropping outbound OCPP message for '%s'; queue is full", this->connection_id_.c_str());
        return false;
    }
    this->messages_.push_back(std::move(message));
    return true;
}

void ChargePoint::handle_ocpp_message_(const OcppMessage &message) {
    if (message.message_type_id == OcppMessageType::CALL_RESULT) {
        ESP_LOGD(TAG, "OCPP call result: charge_point='%s' uniqueId='%s'", this->connection_id_.c_str(),
                message.unique_id.c_str());
        if (message.action == "GetConfiguration")
            this->handle_get_configuration_response_(static_cast<const GetConfigurationResponse &>(message));
        else if (message.action == "ChangeConfiguration")
            this->handle_change_configuration_reply_(message);
        this->handle_ocpp_call_reply_(message);
        this->handle_startup_notification_trigger_reply_(message);
        return;
    }

    if (message.message_type_id == OcppMessageType::CALL_ERROR) {
        ESP_LOGW(TAG, "OCPP call error: charge_point='%s' uniqueId='%s'", this->connection_id_.c_str(),
                message.unique_id.c_str());
        this->handle_change_configuration_reply_(message);
        this->handle_ocpp_call_reply_(message);
        this->handle_startup_notification_trigger_reply_(message);
        return;
    }

    if (message.action.empty()) {
        ESP_LOGW(TAG, "Ignoring OCPP CALL message without action: charge_point='%s' uniqueId='%s'",
                this->connection_id_.c_str(), message.unique_id.c_str());
        return;
    }
    this->handle_ocpp_call_(message);
}

void ChargePoint::handle_ocpp_call_(const OcppMessage &call) {
    ESP_LOGD(TAG, "OCPP message: charge_point='%s' action='%s' uniqueId='%s'", this->connection_id_.c_str(),
            call.action.c_str(), call.unique_id.c_str());

    if (call.action == "BootNotification") {
        bool was_boot_trigger_in_flight = this->boot_notification_trigger_in_flight_;
        this->boot_notification_pending_ = false;
        this->boot_notification_trigger_in_flight_ = false;
        this->set_online_(true);
        const auto &boot_notification = static_cast<const BootNotification &>(call);
        this->publish_charger_info_(boot_notification);
        bool boot_response_queued = this->send_message_({this->protocol_.make_boot_notification_response(call.unique_id),
                                                         OcppMessageType::CALL_RESULT, call.unique_id, call.action});
        if (boot_response_queued)
            this->send_get_configuration_request_();
        if (was_boot_trigger_in_flight)
            this->send_startup_notification_triggers_();
    } else if (call.action == "Heartbeat") {
        this->set_online_(true);
        this->send_message_({this->protocol_.make_heartbeat_response(call.unique_id), OcppMessageType::CALL_RESULT,
                             call.unique_id, call.action});
    } else if (this->protocol_.get_version() == OcppProtocolVersion::OCPP_1_6 && call.action == "Authorize") {
        this->set_online_(true);
        this->handle_authorize_(static_cast<const Authorize &>(call));
    } else if (this->protocol_.get_version() == OcppProtocolVersion::OCPP_1_6 && call.action == "StartTransaction") {
        this->set_online_(true);
        this->handle_start_transaction_(static_cast<const StartTransaction &>(call));
    } else if (this->protocol_.get_version() == OcppProtocolVersion::OCPP_1_6 && call.action == "StopTransaction") {
        this->set_online_(true);
        this->handle_stop_transaction_(static_cast<const StopTransaction &>(call));
    } else if (call.action == "MeterValues") {
        this->set_online_(true);
        const auto &meter_values = static_cast<const MeterValues &>(call);
        this->publish_meter_values_(meter_values);
        this->send_message_({this->protocol_.make_meter_values_response(call.unique_id), OcppMessageType::CALL_RESULT,
                             call.unique_id, call.action});
    } else if (call.action == "StatusNotification") {
        this->status_notification_pending_ = false;
        this->status_notification_trigger_in_flight_ = false;
        this->set_online_(true);
        const auto &status_notification = static_cast<const StatusNotification &>(call);
        this->publish_status_notification_(status_notification);
        this->send_message_({this->protocol_.make_status_notification_response(call.unique_id), OcppMessageType::CALL_RESULT,
                             call.unique_id, call.action});
    } else {
        ESP_LOGW(TAG, "Unsupported OCPP action '%s' from charge point '%s'", call.action.c_str(),
                this->connection_id_.c_str());
        this->send_message_({this->protocol_.make_ocpp_error(call.unique_id, "NotImplemented",
                                                            "This OCPP action is not implemented"),
                             OcppMessageType::CALL_ERROR, call.unique_id, call.action});
    }
}

void ChargePoint::handle_authorize_(const Authorize &authorize) {
    this->send_message_({this->protocol_.make_authorize_response(authorize.unique_id), OcppMessageType::CALL_RESULT,
                         authorize.unique_id, authorize.action});
}

void ChargePoint::handle_ocpp_call_reply_(const OcppMessage &message) {
    if (this->in_flight_call_ == nullptr)
        return;
    if (message.unique_id != this->in_flight_call_->unique_id)
        return;
    this->in_flight_call_.reset();
}

void ChargePoint::handle_start_transaction_(const StartTransaction &start_transaction) {
    uint32_t transaction_id = this->next_transaction_id_++;
    Connector *connector = this->find_connector_(start_transaction.connector_id);
    if (connector == nullptr) {
        ESP_LOGW(TAG, "Accepting StartTransaction for unknown connector: charge_point='%s' connector_id=%u",
                this->connection_id_.c_str(), static_cast<unsigned>(start_transaction.connector_id));
    } else {
        connector->set_active_transaction_id(transaction_id);
    }

    this->send_message_({this->protocol_.make_start_transaction_response(start_transaction.unique_id, transaction_id),
                         OcppMessageType::CALL_RESULT, start_transaction.unique_id, start_transaction.action});
}

void ChargePoint::handle_stop_transaction_(const StopTransaction &stop_transaction) {
    Connector *connector = this->find_connector_by_transaction_id_(stop_transaction.transaction_id);
    if (connector == nullptr) {
        ESP_LOGW(TAG, "Accepting StopTransaction for unknown transaction: charge_point='%s' transaction_id=%u",
                this->connection_id_.c_str(), static_cast<unsigned>(stop_transaction.transaction_id));
    } else {
        connector->clear_active_transaction();
    }

    this->send_message_({this->protocol_.make_stop_transaction_response(stop_transaction.unique_id),
                         OcppMessageType::CALL_RESULT, stop_transaction.unique_id, stop_transaction.action});
}

const std::string &ChargePoint::debug_action_for_message_(const OcppMessage &message) const {
    if (!message.action.empty())
        return message.action;
    if (this->in_flight_call_ != nullptr && message.unique_id == this->in_flight_call_->unique_id)
        return this->in_flight_call_->action;
    return message.action;
}

bool ChargePoint::should_log_debug_ocpp_message_(const OcppMessage &message) const {
    const std::string &action = this->debug_action_for_message_(message);
    return action.empty() || !this->is_debug_ocpp_action_excluded(action);
}

bool ChargePoint::should_log_debug_ocpp_message_(const QueuedMessage &message) const {
    return message.action.empty() || !this->is_debug_ocpp_action_excluded(message.action);
}

void ChargePoint::handle_get_configuration_response_(const GetConfigurationResponse &message) {
    this->meter_value_sample_interval_ = message.meter_value_sample_interval;
    this->meter_values_sampled_data_ = message.meter_values_sampled_data;
    this->connector_switch_3_to_1_phase_supported_ = message.connector_switch_3_to_1_phase_supported;
    this->change_configuration_stage_ = ChangeConfigurationStage::METER_VALUES_SAMPLED_DATA;
    this->meter_values_sampled_data_fallback_index_ = 0;

    ESP_LOGI(TAG,
             "GetConfiguration response: charge_point_id='%s' MeterValueSampleInterval='%s' MeterValuesSampledData='%s' "
             "ConnectorSwitch3to1PhaseSupported='%s'",
             this->connection_id_.c_str(), this->meter_value_sample_interval_.c_str(),
             this->meter_values_sampled_data_.c_str(), this->connector_switch_3_to_1_phase_supported_.c_str());

    if (!this->send_meter_values_sampled_data_change_request_())
        this->change_configuration_stage_ = ChangeConfigurationStage::IDLE;
}

void ChargePoint::handle_change_configuration_reply_(const OcppMessage &message) {
    if (message.unique_id == CHANGE_CONFIGURATION_METER_VALUES_SAMPLED_DATA_UNIQUE_ID) {
        if (this->change_configuration_stage_ != ChangeConfigurationStage::METER_VALUES_SAMPLED_DATA)
            return;
        if (message.message_type_id == OcppMessageType::CALL_RESULT && message.action == "ChangeConfiguration") {
            const auto &response = static_cast<const ChangeConfigurationResponse &>(message);

            if (response.status == "Rejected" &&
                this->meter_values_sampled_data_fallback_index_ + 1 <
                    sizeof(METER_VALUES_SAMPLED_DATA_FALLBACKS) / sizeof(METER_VALUES_SAMPLED_DATA_FALLBACKS[0])) {
                this->meter_values_sampled_data_fallback_index_++;
                ESP_LOGW(TAG,
                         "ChangeConfiguration rejected MeterValuesSampledData for '%s'; retrying fallback %u/%u",
                         this->connection_id_.c_str(),
                         static_cast<unsigned>(this->meter_values_sampled_data_fallback_index_ + 1),
                         static_cast<unsigned>(sizeof(METER_VALUES_SAMPLED_DATA_FALLBACKS) /
                                               sizeof(METER_VALUES_SAMPLED_DATA_FALLBACKS[0])));
                if (!this->send_meter_values_sampled_data_change_request_())
                    this->change_configuration_stage_ = ChangeConfigurationStage::IDLE;
                return;
            }

            ESP_LOGI(TAG, "ChangeConfiguration MeterValuesSampledData reply: charge_point_id='%s' status='%s'",
                     this->connection_id_.c_str(), response.status.c_str());
        } else {
            ESP_LOGW(TAG, "ChangeConfiguration MeterValuesSampledData failed: charge_point_id='%s' uniqueId='%s'",
                     this->connection_id_.c_str(), message.unique_id.c_str());
        }

        this->change_configuration_stage_ = ChangeConfigurationStage::METER_VALUE_SAMPLE_INTERVAL;
        if (!this->send_meter_value_sample_interval_change_request_())
            this->change_configuration_stage_ = ChangeConfigurationStage::IDLE;
        return;
    }

    if (message.unique_id == CHANGE_CONFIGURATION_METER_VALUE_SAMPLE_INTERVAL_UNIQUE_ID) {
        if (this->change_configuration_stage_ != ChangeConfigurationStage::METER_VALUE_SAMPLE_INTERVAL)
            return;
        if (message.message_type_id == OcppMessageType::CALL_RESULT && message.action == "ChangeConfiguration") {
            const auto &response = static_cast<const ChangeConfigurationResponse &>(message);
            ESP_LOGI(TAG, "ChangeConfiguration MeterValueSampleInterval reply: charge_point_id='%s' status='%s'",
                     this->connection_id_.c_str(), response.status.c_str());
        } else {
            ESP_LOGW(TAG, "ChangeConfiguration MeterValueSampleInterval failed: charge_point_id='%s' uniqueId='%s'",
                     this->connection_id_.c_str(), message.unique_id.c_str());
        }
        this->change_configuration_stage_ = ChangeConfigurationStage::IDLE;
    }
}

bool ChargePoint::send_meter_value_sample_interval_change_request_() {
    std::string request = this->protocol_.make_change_configuration_request(
        CHANGE_CONFIGURATION_METER_VALUE_SAMPLE_INTERVAL_UNIQUE_ID,
        METER_VALUE_SAMPLE_INTERVAL_KEY,
        METER_VALUE_SAMPLE_INTERVAL_VALUE
    );
    if (request.empty())
        return false;

    return this->send_message_({std::move(request), OcppMessageType::CALL,
                                CHANGE_CONFIGURATION_METER_VALUE_SAMPLE_INTERVAL_UNIQUE_ID, "ChangeConfiguration"});
}

bool ChargePoint::send_meter_values_sampled_data_change_request_() {
    if (this->meter_values_sampled_data_fallback_index_ >=
        sizeof(METER_VALUES_SAMPLED_DATA_FALLBACKS) / sizeof(METER_VALUES_SAMPLED_DATA_FALLBACKS[0]))
        return false;

    std::string request = this->protocol_.make_change_configuration_request(
        CHANGE_CONFIGURATION_METER_VALUES_SAMPLED_DATA_UNIQUE_ID,
        METER_VALUES_SAMPLED_DATA_KEY,
        METER_VALUES_SAMPLED_DATA_FALLBACKS[this->meter_values_sampled_data_fallback_index_]
    );
    if (request.empty())
        return false;

    return this->send_message_({std::move(request), OcppMessageType::CALL,
                                CHANGE_CONFIGURATION_METER_VALUES_SAMPLED_DATA_UNIQUE_ID, "ChangeConfiguration"});
}

void ChargePoint::handle_startup_notification_trigger_reply_(const OcppMessage &message) {
    if (message.unique_id == TRIGGER_BOOT_NOTIFICATION_UNIQUE_ID) {
        this->boot_notification_trigger_in_flight_ = false;
        this->send_startup_notification_triggers_();
    } else if (message.unique_id == TRIGGER_STATUS_NOTIFICATION_UNIQUE_ID) {
        this->status_notification_trigger_in_flight_ = false;
    }
}

void ChargePoint::send_get_configuration_request_() {
    std::string request = this->protocol_.make_get_configuration_request(GET_CONFIGURATION_UNIQUE_ID);
    if (request.empty())
        return;

    this->send_message_({std::move(request), OcppMessageType::CALL, GET_CONFIGURATION_UNIQUE_ID, "GetConfiguration"});
}

void ChargePoint::send_startup_notification_triggers_() {
    if (!this->connected_ || this->startup_notifications_delay_ms_ == 0)
        return;

    if (this->in_flight_call_ != nullptr)
        return;

    if (this->boot_notification_trigger_in_flight_ || this->status_notification_trigger_in_flight_)
        return;

    if (this->boot_notification_pending_) {
        this->send_boot_notification_trigger_();
        return;
    }

    if (this->status_notification_pending_)
        this->send_status_notification_trigger_();
}

void ChargePoint::send_boot_notification_trigger_() {
    this->boot_notification_pending_ = false;
    this->boot_notification_trigger_in_flight_ = true;
    this->send_message_({this->protocol_.make_trigger_boot_notification(TRIGGER_BOOT_NOTIFICATION_UNIQUE_ID),
                         OcppMessageType::CALL, TRIGGER_BOOT_NOTIFICATION_UNIQUE_ID, "TriggerMessage"});
}

void ChargePoint::send_status_notification_trigger_() {
    this->status_notification_pending_ = false;
    this->status_notification_trigger_in_flight_ = true;
    this->send_message_({this->protocol_.make_trigger_status_notification(TRIGGER_STATUS_NOTIFICATION_UNIQUE_ID),
                         OcppMessageType::CALL, TRIGGER_STATUS_NOTIFICATION_UNIQUE_ID, "TriggerMessage"});
}

void ChargePoint::expire_in_flight_call_(uint32_t now_millis) {
    if (this->in_flight_call_ == nullptr)
        return;
    if (now_millis - this->in_flight_call_->sent_at_millis < DEFAULT_CALL_TIMEOUT_MS)
        return;

    ESP_LOGW(TAG, "OCPP call timed out: charge_point='%s' action='%s' uniqueId='%s'", this->connection_id_.c_str(),
            this->in_flight_call_->action.c_str(), this->in_flight_call_->unique_id.c_str());
    OcppMessage timed_out_message(OcppMessageType::CALL_ERROR, this->in_flight_call_->unique_id);
    this->in_flight_call_.reset();
    this->handle_change_configuration_reply_(timed_out_message);
    this->handle_startup_notification_trigger_reply_(timed_out_message);
}

void ChargePoint::set_online_(bool online) {
    if (this->online_ == online)
        return;
    this->online_ = online;
    if (this->online_binary_sensor_ != nullptr)
        this->online_binary_sensor_->publish_state(online);
}

void ChargePoint::publish_charger_info_(const BootNotification &boot_notification) {
    if (this->charger_info_text_sensor_ == nullptr)
        return;

    std::string info;
    if (!boot_notification.charge_point_vendor.empty())
        info = "vendor: " + boot_notification.charge_point_vendor;
    if (!boot_notification.charge_point_model.empty()) {
        if (!info.empty())
            info += ", ";
        info += "model: " + boot_notification.charge_point_model;
    }
    if (!boot_notification.firmware_version.empty()) {
        if (!info.empty())
            info += ", ";
        info += "firmware: " + boot_notification.firmware_version;
    }
    this->charger_info_text_sensor_->publish_state(info);
}

void ChargePoint::publish_meter_values_(const MeterValues &meter_values) {
    Connector *connector = this->find_connector_(meter_values.connector_id);
    if (connector == nullptr) {
        ESP_LOGW(TAG, "Ignoring MeterValues for unknown connector: charge_point='%s' connector_id=%u",
                this->connection_id_.c_str(), static_cast<unsigned>(meter_values.connector_id));
        return;
    }
    connector->publish_meter_values(this->connection_id_, meter_values);
}

void ChargePoint::publish_status_notification_(const StatusNotification &status_notification) {
    Connector *connector = this->find_connector_(status_notification.connector_id);
    if (connector == nullptr) {
        ESP_LOGW(TAG, "Ignoring StatusNotification for unknown connector: charge_point='%s' connector_id=%u",
                this->connection_id_.c_str(), static_cast<unsigned>(status_notification.connector_id));
        return;
    }
    connector->publish_status_notification(status_notification, this->current_millis_);
}

Connector *ChargePoint::find_connector_(uint32_t connector_id) {
    for (auto *connector : this->connectors_) {
        if (connector != nullptr && connector->get_connector_id() == connector_id)
            return connector;
    }
    return nullptr;
}

Connector *ChargePoint::find_connector_by_transaction_id_(uint32_t transaction_id) {
    for (auto *connector : this->connectors_) {
        if (connector != nullptr && connector->get_active_transaction_id() == transaction_id)
            return connector;
    }
    return nullptr;
}

}  // namespace esphome::ocpp
