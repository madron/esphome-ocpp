#include "ocpp.h"

#ifdef USE_OCPP

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";
static constexpr const char *CURRENT_TIME = "1970-01-01T00:00:00Z";

std::string json_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 2);
  for (char c : value) {
    if (c == '"' || c == '\\')
      out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

bool parse_float(const char *value, float *out) {
  if (value == nullptr || value[0] == '\0')
    return false;
  char *end = nullptr;
  float parsed = std::strtof(value, &end);
  if (end == value)
    return false;
  *out = parsed;
  return true;
}

bool valid_current_import(float value) { return std::isfinite(value) && value >= 0.0f; }

}  // namespace

void OcppServer::remote_start_(uint8_t connector_id, std::string id_tag, bool use_current_limit, float current_limit) {
  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction; no OCPP wallbox is connected");
    return;
  }
  if (use_current_limit && current_limit <= 0) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction with non-positive current limit %.1f A", current_limit);
    return;
  }

  if (this->remote_start_in_flight_ && this->remote_start_in_flight_connector_id_ == connector_id &&
      this->remote_start_in_flight_id_tag_ == id_tag &&
      this->remote_start_in_flight_use_current_limit_ == use_current_limit &&
      this->remote_start_in_flight_current_limit_ == current_limit) {
    ESP_LOGD(TAG, "Coalescing duplicate RemoteStartTransaction while previous request is pending");
    return;
  }

  this->pending_remote_start_ = true;
  this->pending_remote_start_connector_id_ = connector_id;
  this->pending_remote_start_id_tag_ = std::move(id_tag);
  this->pending_remote_start_use_current_limit_ = use_current_limit;
  this->pending_remote_start_current_limit_ = current_limit;
  this->send_pending_remote_start_if_ready_();
}

void OcppServer::send_pending_remote_start_if_ready_() {
  if (!this->pending_remote_start_ || this->remote_start_in_flight_)
    return;
  if (this->send_remote_start_now_(this->pending_remote_start_connector_id_, this->pending_remote_start_id_tag_,
                                  this->pending_remote_start_use_current_limit_,
                                  this->pending_remote_start_current_limit_))
    this->pending_remote_start_ = false;
}

bool OcppServer::send_remote_start_now_(uint8_t connector_id, const std::string &id_tag, bool use_current_limit,
                                        float current_limit) {
  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction; no OCPP wallbox is connected");
    return false;
  }
  if (use_current_limit && current_limit <= 0) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction with non-positive current limit %.1f A", current_limit);
    return true;
  }

  float limit = current_limit;
  const auto *connector = this->find_connector_(connector_id);
  if (this->has_charger_ && connector == nullptr) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction; connector %u is not configured for charge point '%s'",
             connector_id, this->charge_point_id_.c_str());
    return true;
  }
  if (connector != nullptr && !connector->enabled) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction; connector %u is disabled", connector_id);
    return true;
  }
  if (connector != nullptr && connector->has_active_transaction) {
    ESP_LOGI(TAG, "Not sending RemoteStartTransaction; connector %u already has active transactionId=%u", connector_id,
             connector->active_transaction_id);
    return true;
  }
  if (use_current_limit && connector != nullptr && limit > connector->max_current) {
    ESP_LOGW(TAG, "Clamping RemoteStartTransaction current limit %.1f A to configured connector maximum %.1f A",
             limit, connector->max_current);
    limit = connector->max_current;
  }

  std::string payload = "{";
  payload += "\"connectorId\":" + to_string(connector_id);
  payload += ",\"idTag\":\"" + json_escape(id_tag) + "\"";
  if (use_current_limit) {
    char limit_buf[16];
    std::snprintf(limit_buf, sizeof(limit_buf), "%.1f", limit);
    payload += ",\"chargingProfile\":{\"chargingProfileId\":1,\"stackLevel\":0,";
    payload += "\"chargingProfilePurpose\":\"TxProfile\",\"chargingProfileKind\":\"Relative\",";
    payload += "\"chargingSchedule\":{\"chargingRateUnit\":\"A\",\"chargingSchedulePeriod\":[{";
    payload += "\"startPeriod\":0,\"limit\":";
    payload += limit_buf;
    payload += "}]}}";
  }
  payload += "}";

  if (use_current_limit) {
    ESP_LOGI(TAG, "Sending RemoteStartTransaction: charge_point='%s' connectorId=%u idTag='%s' current_limit=%.1f A",
             this->charge_point_id_.c_str(), connector_id, id_tag.c_str(), limit);
    this->pending_profile_connector_id_ = connector_id;
    this->pending_profile_current_limit_ = limit;
  } else {
    ESP_LOGI(TAG, "Sending RemoteStartTransaction: charge_point='%s' connectorId=%u idTag='%s' current_limit=none",
             this->charge_point_id_.c_str(), connector_id, id_tag.c_str());
    this->pending_profile_connector_id_ = 0;
    this->pending_profile_current_limit_ = 0.0f;
  }
  std::string unique_id = this->send_ocpp_call_("remote-start", "RemoteStartTransaction", payload, connector_id, 0,
                                               use_current_limit ? limit : 0.0f, true);
  this->remote_start_in_flight_ = this->find_pending_call_(unique_id) != nullptr;
  if (this->remote_start_in_flight_) {
    this->remote_start_in_flight_connector_id_ = connector_id;
    this->remote_start_in_flight_id_tag_ = id_tag;
    this->remote_start_in_flight_use_current_limit_ = use_current_limit;
    this->remote_start_in_flight_current_limit_ = current_limit;
  }
  return this->remote_start_in_flight_;
}


void OcppServer::request_remote_stop_(uint32_t transaction_id) {
  if (this->remote_stop_in_flight_ && this->remote_stop_in_flight_transaction_id_ == transaction_id) {
    ESP_LOGD(TAG, "Coalescing duplicate RemoteStopTransaction while previous request is pending");
    return;
  }
  this->pending_remote_stop_ = true;
  this->pending_remote_stop_transaction_id_ = transaction_id;
  this->send_pending_remote_stop_if_ready_();
}

void OcppServer::send_pending_remote_stop_if_ready_() {
  if (!this->pending_remote_stop_ || this->remote_stop_in_flight_)
    return;
  if (this->send_remote_stop_now_(this->pending_remote_stop_transaction_id_))
    this->pending_remote_stop_ = false;
}

bool OcppServer::send_remote_stop_now_(uint32_t transaction_id) {
  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGW(TAG, "Cannot send RemoteStopTransaction; no OCPP wallbox is connected");
    return false;
  }

  std::string payload = "{\"transactionId\":" + to_string(transaction_id) + "}";

  ESP_LOGI(TAG, "Sending RemoteStopTransaction: charge_point='%s' transactionId=%u", this->charge_point_id_.c_str(),
           transaction_id);
  std::string unique_id = this->send_ocpp_call_("remote-stop", "RemoteStopTransaction", payload, 0, transaction_id,
                                               0.0f, true);
  this->remote_stop_in_flight_ = this->find_pending_call_(unique_id) != nullptr;
  if (this->remote_stop_in_flight_)
    this->remote_stop_in_flight_transaction_id_ = transaction_id;
  return this->remote_stop_in_flight_;
}


std::string OcppServer::next_unique_id_(const char *prefix) {
  return std::string(prefix) + "-" + to_string(this->next_message_id_++);
}

void OcppServer::track_pending_call_(const std::string &unique_id, const char *action, uint8_t connector_id,
                                     uint32_t transaction_id, float current_limit) {
  PendingOcppCall *slot = nullptr;
  for (auto &call : this->pending_calls_) {
    if (call.active && std::strcmp(call.unique_id, unique_id.c_str()) == 0) {
      slot = &call;
      break;
    }
    if (!call.active && slot == nullptr)
      slot = &call;
  }
  if (slot == nullptr) {
    slot = &this->pending_calls_[0];
    ESP_LOGW(TAG, "Pending OCPP call table full; replacing uniqueId='%s' action='%s'", slot->unique_id,
             slot->action == nullptr ? "" : slot->action);
  }
  slot->active = true;
  std::snprintf(slot->unique_id, sizeof(slot->unique_id), "%s", unique_id.c_str());
  slot->action = action;
  slot->connector_id = connector_id;
  slot->transaction_id = transaction_id;
  slot->current_limit = current_limit;
}

OcppServer::PendingOcppCall *OcppServer::find_pending_call_(const std::string &unique_id) {
  for (auto &call : this->pending_calls_) {
    if (call.active && std::strcmp(call.unique_id, unique_id.c_str()) == 0)
      return &call;
  }
  return nullptr;
}

void OcppServer::clear_pending_call_(const std::string &unique_id) {
  auto *call = this->find_pending_call_(unique_id);
  if (call != nullptr)
    *call = PendingOcppCall{};
}

void OcppServer::clear_pending_calls_() {
  for (auto &call : this->pending_calls_)
    call = PendingOcppCall{};
}


void OcppServer::handle_ws_text_(const std::string &message) {
  auto doc = json::parse_json(message);
  if (doc.isNull() || doc.overflowed() || !doc.is<JsonArray>()) {
    ESP_LOGW(TAG, "Ignoring invalid OCPP JSON message: %s", message.c_str());
    return;
  }
  JsonArray root = doc.as<JsonArray>();
  int message_type = root[0] | 0;
  if (message_type == 3) {
    if (root.size() < 3) {
      ESP_LOGW(TAG, "Ignoring invalid OCPP CALLRESULT message: %s", message.c_str());
      return;
    }
    this->handle_call_result_(root[1] | "", root[2].as<JsonObject>());
    return;
  }
  if (message_type == 4) {
    if (root.size() < 5) {
      ESP_LOGW(TAG, "Ignoring invalid OCPP CALLERROR message: %s", message.c_str());
      return;
    }
    this->handle_call_error_(root[1] | "", root[2] | "", root[3] | "");
    return;
  }
  if (root.size() < 4 || message_type != 2) {
    ESP_LOGW(TAG, "Ignoring unsupported OCPP message: %s", message.c_str());
    return;
  }

  std::string unique_id = root[1] | "";
  std::string action = root[2] | "";
  if (action == "MeterValues") {
    ESP_LOGV(TAG, "OCPP message: charge_point='%s' action='%s' uniqueId='%s'", this->charge_point_id_.c_str(),
             action.c_str(), unique_id.c_str());
  } else {
    ESP_LOGD(TAG, "OCPP message: charge_point='%s' action='%s' uniqueId='%s'", this->charge_point_id_.c_str(),
             action.c_str(), unique_id.c_str());
  }
  if (action == "BootNotification") {
    this->handle_boot_notification_(unique_id, root[3].as<JsonObject>());
  } else if (action == "Heartbeat") {
    this->handle_heartbeat_(unique_id);
  } else if (action == "Authorize") {
    this->handle_authorize_(unique_id, root[3].as<JsonObject>());
  } else if (action == "StatusNotification") {
    this->handle_status_notification_(unique_id, root[3].as<JsonObject>());
  } else if (action == "StartTransaction") {
    this->handle_start_transaction_(unique_id, root[3].as<JsonObject>());
  } else if (action == "StopTransaction") {
    this->handle_stop_transaction_(unique_id, root[3].as<JsonObject>());
  } else if (action == "MeterValues") {
    this->handle_meter_values_(unique_id, root[3].as<JsonObject>());
  } else {
    ESP_LOGW(TAG, "Unsupported OCPP action '%s' from charge point '%s'", action.c_str(), this->charge_point_id_.c_str());
    this->send_ocpp_error_(unique_id, "NotImplemented", "This OCPP action is not implemented");
  }
}

void OcppServer::handle_boot_notification_(const std::string &unique_id, JsonObject payload) {
  const char *vendor = payload["chargePointVendor"] | "";
  const char *model = payload["chargePointModel"] | "";
  const char *serial = payload["chargePointSerialNumber"] | "";
  const char *firmware = payload["firmwareVersion"] | "";

  ESP_LOGI(TAG, "BootNotification accepted: charge_point='%s' vendor='%s' model='%s' serial='%s' firmware='%s'",
           this->charge_point_id_.c_str(), vendor, model, serial, firmware);

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME +
                         "\",\"interval\":300,\"status\":\"Accepted\"}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_heartbeat_(const std::string &unique_id) {
  ESP_LOGD(TAG, "Heartbeat from charge point '%s'", this->charge_point_id_.c_str());
  std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"currentTime\":\"" + CURRENT_TIME + "\"}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_authorize_(const std::string &unique_id, JsonObject payload) {
  const char *id_tag = payload["idTag"] | "";
  ESP_LOGI(TAG, "Authorize accepted: charge_point='%s' idTag='%s'", this->charge_point_id_.c_str(), id_tag);

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"idTagInfo\":{\"status\":\"Accepted\"}}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_status_notification_(const std::string &unique_id, JsonObject payload) {
  int connector_id = payload["connectorId"] | -1;
  const char *status = payload["status"] | "";
  const char *error_code = payload["errorCode"] | "";
  const char *timestamp = payload["timestamp"] | "";
  const char *info = payload["info"] | "";
  const char *vendor_id = payload["vendorId"] | "";
  const char *vendor_error_code = payload["vendorErrorCode"] | "";

  ESP_LOGI(TAG,
           "StatusNotification: charge_point='%s' connectorId=%d status='%s' errorCode='%s' timestamp='%s' "
           "info='%s' vendorId='%s' vendorErrorCode='%s'",
           this->charge_point_id_.c_str(), connector_id, status, error_code, timestamp, info, vendor_id,
           vendor_error_code);
  auto *connector = this->find_connector_(connector_id);
  bool started_charging = false;
  if (this->has_charger_ && connector_id > 0 && connector == nullptr) {
    ESP_LOGW(TAG, "StatusNotification referenced unconfigured connector %d for charge point '%s'", connector_id,
             this->charge_point_id_.c_str());
  }
  if (connector != nullptr) {
    const char *connector_state = connector_state_from_ocpp_status(status);
    if (connector->state != connector_state) {
      connector->state = connector_state;
      this->publish_connector_state_if_configured_(connector);
    }
    bool is_charging = std::strcmp(status, "Charging") == 0;
    started_charging = is_charging && !connector->is_charging;
    bool stopped_charging = !is_charging && connector->is_charging;
    connector->is_charging = is_charging;
    if (started_charging || stopped_charging) {
      this->reset_session_current_(connector);
      this->publish_current_if_configured_(connector);
    }
    if (!is_charging)
      connector->charging_profile_applied = false;
  }

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{}]";
  this->send_ws_text_(response);

  if (started_charging && connector_id > 0 && connector_id <= 255)
    this->send_preferred_current_limit_if_needed_(static_cast<uint8_t>(connector_id));
}

void OcppServer::handle_start_transaction_(const std::string &unique_id, JsonObject payload) {
  int connector_id = payload["connectorId"] | -1;
  const char *id_tag = payload["idTag"] | "";
  int meter_start = payload["meterStart"] | 0;
  const char *timestamp = payload["timestamp"] | "";
  uint32_t transaction_id = this->next_transaction_id_++;
  const auto *connector = this->find_connector_(connector_id);
  if (this->has_charger_ && connector == nullptr) {
    ESP_LOGW(TAG, "StartTransaction referenced unconfigured connector %d for charge point '%s'", connector_id,
             this->charge_point_id_.c_str());
  }

  if (payload["reservationId"].is<int>()) {
    ESP_LOGI(TAG,
             "StartTransaction accepted: charge_point='%s' connectorId=%d idTag='%s' meterStart=%d timestamp='%s' "
             "reservationId=%d transactionId=%u",
             this->charge_point_id_.c_str(), connector_id, id_tag, meter_start, timestamp,
             payload["reservationId"].as<int>(), transaction_id);
  } else {
    ESP_LOGI(TAG,
             "StartTransaction accepted: charge_point='%s' connectorId=%d idTag='%s' meterStart=%d timestamp='%s' "
             "transactionId=%u",
             this->charge_point_id_.c_str(), connector_id, id_tag, meter_start, timestamp, transaction_id);
  }

  if (connector_id > 0 && connector_id <= 255)
    this->mark_transaction_started_(static_cast<uint8_t>(connector_id), transaction_id, id_tag);

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{\"transactionId\":" + to_string(transaction_id) +
                         ",\"idTagInfo\":{\"status\":\"Accepted\"}}]";
  this->send_ws_text_(response);

  auto *started_connector = this->find_connector_(connector_id);
  if (started_connector != nullptr) {
    const bool allocation_updated = this->update_connector_allocation_(started_connector);
    if (!started_connector->enabled) {
      ESP_LOGI(TAG, "Stopping transaction %u because connector %d is disabled", transaction_id, connector_id);
      this->remote_stop(transaction_id);
      this->pending_profile_connector_id_ = 0;
      this->pending_profile_current_limit_ = 0.0f;
      return;
    }
    if (!allocation_updated)
      return;
    if (started_connector->allocated_current <= 0.0f) {
      ESP_LOGI(TAG, "Stopping transaction %u because connector %d allocated_current=0.0 A", transaction_id,
               connector_id);
      this->remote_stop(transaction_id);
      this->pending_profile_connector_id_ = 0;
      this->pending_profile_current_limit_ = 0.0f;
      return;
    }
  }

  if (this->pending_profile_current_limit_ > 0.0f && this->pending_profile_connector_id_ == connector_id) {
    float allocated_current = started_connector != nullptr ? started_connector->allocated_current
                                                           : this->pending_profile_current_limit_;
    this->request_set_charging_profile_(static_cast<uint8_t>(connector_id), transaction_id, allocated_current);
    this->pending_profile_connector_id_ = 0;
    this->pending_profile_current_limit_ = 0.0f;
  }

  if (connector_id > 0 && connector_id <= 255)
    this->send_preferred_current_limit_if_needed_(static_cast<uint8_t>(connector_id));
}

void OcppServer::handle_stop_transaction_(const std::string &unique_id, JsonObject payload) {
  int transaction_id = payload["transactionId"] | -1;
  int meter_stop = payload["meterStop"] | 0;
  const char *timestamp = payload["timestamp"] | "";
  const char *id_tag = payload["idTag"] | "";
  const char *reason = payload["reason"] | "";

  ESP_LOGI(TAG,
           "StopTransaction accepted: charge_point='%s' transactionId=%d idTag='%s' meterStop=%d timestamp='%s' "
           "reason='%s'",
           this->charge_point_id_.c_str(), transaction_id, id_tag, meter_stop, timestamp, reason);
  uint8_t stopped_connector_id = 0;
  if (transaction_id >= 0) {
    auto *connector = this->find_transaction_connector_(static_cast<uint32_t>(transaction_id));
    if (connector != nullptr)
      stopped_connector_id = connector->id;
    this->note_transaction_id_(static_cast<uint32_t>(transaction_id));
    this->clear_transaction_(static_cast<uint32_t>(transaction_id));
  }

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{}]";
  this->send_ws_text_(response);

  if (stopped_connector_id != 0 && this->pending_session_restart_connector_id_ == stopped_connector_id) {
    this->pending_session_restart_connector_id_ = 0;
    const auto *connector = this->find_connector_(stopped_connector_id);
    if (connector != nullptr && !connector->enabled) {
      ESP_LOGI(TAG, "Not restarting connector session: connectorId=%u is disabled", stopped_connector_id);
      return;
    }
    this->remote_start(stopped_connector_id);
  }
}

void OcppServer::handle_meter_values_(const std::string &unique_id, JsonObject payload) {
  int connector_id = payload["connectorId"] | -1;
  int transaction_id = payload["transactionId"] | -1;
  auto *connector = this->find_connector_(connector_id);
  bool current_updated = false;
  bool power_updated = false;
  if (transaction_id >= 0) {
    this->note_transaction_id_(static_cast<uint32_t>(transaction_id));
    if (connector_id > 0 && connector_id <= 255)
      this->recover_transaction_from_meter_values_(static_cast<uint8_t>(connector_id),
                                                   static_cast<uint32_t>(transaction_id));
  }
  JsonArray meter_values = payload["meterValue"].as<JsonArray>();

  if (meter_values.isNull()) {
    ESP_LOGV(TAG, "MeterValues: charge_point='%s' connectorId=%d transactionId=%d", this->charge_point_id_.c_str(),
             connector_id, transaction_id);
  } else {
    for (JsonObject meter_value : meter_values) {
      const char *timestamp = meter_value["timestamp"] | "";
      JsonArray sampled_values = meter_value["sampledValue"].as<JsonArray>();
      if (sampled_values.isNull()) {
        ESP_LOGV(TAG, "MeterValues: charge_point='%s' connectorId=%d transactionId=%d timestamp='%s'",
                 this->charge_point_id_.c_str(), connector_id, transaction_id, timestamp);
        continue;
      }
      for (JsonObject sampled_value : sampled_values) {
        const char *value = sampled_value["value"] | "";
        const char *measurand = sampled_value["measurand"] | "";
        const char *unit = sampled_value["unit"] | "";
        const char *phase = sampled_value["phase"] | "";
        const char *context = sampled_value["context"] | "";
        const char *location = sampled_value["location"] | "";
        float parsed_value;
        if (parse_float(value, &parsed_value)) {
          if (std::strcmp(measurand, "Current.Import") == 0 && (unit[0] == '\0' || std::strcmp(unit, "A") == 0)) {
            bool latest_current_changed = false;
            if (valid_current_import(parsed_value) && connector != nullptr) {
              latest_current_changed = !connector->has_latest_current_import ||
                                       connector->latest_current_import != parsed_value;
              bool first_session_current = connector->is_charging && !connector->has_session_current_import;
              connector->latest_current_import = parsed_value;
              connector->has_latest_current_import = true;
              connector->has_session_current_import = connector->is_charging;
              if (first_session_current) {
                ESP_LOGI(TAG, "Current.Import available for active session: charge_point='%s' connectorId=%d",
                         this->charge_point_id_.c_str(), connector_id);
              }
              if (!connector->is_charging)
                ESP_LOGV(TAG, "Ignoring Current.Import for allocation because connectorId=%d is not charging",
                         connector_id);
            } else if (!valid_current_import(parsed_value)) {
              ESP_LOGW(TAG, "Ignoring invalid Current.Import %.3f A for connectorId=%d", parsed_value, connector_id);
            }
            current_updated = current_updated || (connector != nullptr && latest_current_changed);
          } else if (std::strcmp(measurand, "Power.Active.Import") == 0 &&
                     (unit[0] == '\0' || std::strcmp(unit, "W") == 0)) {
            if (connector != nullptr) {
              power_updated = power_updated || !connector->has_latest_power_active_import ||
                              connector->latest_power_active_import != parsed_value;
              connector->latest_power_active_import = parsed_value;
              connector->has_latest_power_active_import = true;
            } else {
              power_updated = true;
            }
          }
        }
        ESP_LOGV(TAG,
                 "MeterValues: charge_point='%s' connectorId=%d transactionId=%d timestamp='%s' value='%s' "
                 "measurand='%s' unit='%s' phase='%s' context='%s' location='%s'",
                 this->charge_point_id_.c_str(), connector_id, transaction_id, timestamp, value, measurand, unit,
                 phase, context, location);
      }
    }
  }

  if ((current_updated || power_updated) && connector != nullptr) {
    ESP_LOGV(TAG, "Latest meter values: connectorId=%d current=%.1f A power=%.1f W", connector_id,
             connector->latest_current_import, connector->latest_power_active_import);
  }
  if (connector != nullptr && current_updated && connector->current_sensor != nullptr)
    connector->current_sensor->publish_state(connector->latest_current_import);
  if (connector != nullptr && current_updated && connector->has_active_transaction) {
    const float previous_allocated_current = connector->allocated_current;
    const bool allocation_updated = this->update_connector_allocation_(connector);
    if (this->client_ != nullptr && this->handshake_done_ && connector->has_active_transaction &&
        allocation_updated && connector->allocated_current != previous_allocated_current) {
      connector->charging_profile_applied = false;
      this->send_preferred_current_limit_if_needed_(connector->id);
    }
  }
  std::string response = "[3,\"" + json_escape(unique_id) + "\",{}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_call_result_(const std::string &unique_id, JsonObject payload) {
  const char *status = payload["status"] | "";
  auto *pending = this->find_pending_call_(unique_id);
  const bool set_charging_profile_call = pending != nullptr && pending->action != nullptr &&
                                         std::strcmp(pending->action, "SetChargingProfile") == 0;
  const bool remote_start_call = pending != nullptr && pending->action != nullptr &&
                                 std::strcmp(pending->action, "RemoteStartTransaction") == 0;
  const bool remote_stop_call = pending != nullptr && pending->action != nullptr &&
                                std::strcmp(pending->action, "RemoteStopTransaction") == 0;
  const float set_charging_profile_current_limit = pending != nullptr ? pending->current_limit : 0.0f;
  if (pending != nullptr && status[0] != '\0') {
    ESP_LOGI(TAG,
             "OCPP CALLRESULT: charge_point='%s' uniqueId='%s' action='%s' status='%s' connectorId=%u "
             "transactionId=%u current_limit=%.1f A",
             this->charge_point_id_.c_str(), unique_id.c_str(), pending->action == nullptr ? "" : pending->action, status, pending->connector_id,
             pending->transaction_id, pending->current_limit);
  } else if (pending != nullptr) {
    ESP_LOGI(TAG,
             "OCPP CALLRESULT: charge_point='%s' uniqueId='%s' action='%s' connectorId=%u transactionId=%u "
             "current_limit=%.1f A",
             this->charge_point_id_.c_str(), unique_id.c_str(), pending->action == nullptr ? "" : pending->action, pending->connector_id,
             pending->transaction_id, pending->current_limit);
  } else if (status[0] != '\0') {
    ESP_LOGI(TAG, "OCPP CALLRESULT: charge_point='%s' uniqueId='%s' status='%s'", this->charge_point_id_.c_str(),
             unique_id.c_str(), status);
  } else {
    ESP_LOGI(TAG, "OCPP CALLRESULT: charge_point='%s' uniqueId='%s'", this->charge_point_id_.c_str(),
             unique_id.c_str());
  }
  if (set_charging_profile_call) {
    this->set_charging_profile_in_flight_ = false;
    if (std::strcmp(status, "Accepted") == 0)
      this->note_set_charging_profile_accepted_(set_charging_profile_current_limit);
  }
  if (remote_start_call) {
    this->remote_start_in_flight_ = false;
    this->remote_start_in_flight_connector_id_ = 0;
    this->remote_start_in_flight_id_tag_.clear();
    this->remote_start_in_flight_use_current_limit_ = false;
    this->remote_start_in_flight_current_limit_ = 0.0f;
  }
  if (remote_stop_call) {
    this->remote_stop_in_flight_ = false;
    this->remote_stop_in_flight_transaction_id_ = 0;
  }
  this->clear_pending_call_(unique_id);
  if (remote_stop_call)
    this->send_pending_remote_stop_if_ready_();
  if (remote_start_call)
    this->send_pending_remote_start_if_ready_();
  if (set_charging_profile_call)
    this->send_pending_set_charging_profile_if_ready_();
}

void OcppServer::handle_call_error_(const std::string &unique_id, const std::string &error_code,
                                    const std::string &description) {
  auto *pending = this->find_pending_call_(unique_id);
  const bool set_charging_profile_call = pending != nullptr && pending->action != nullptr &&
                                         std::strcmp(pending->action, "SetChargingProfile") == 0;
  const bool remote_start_call = pending != nullptr && pending->action != nullptr &&
                                 std::strcmp(pending->action, "RemoteStartTransaction") == 0;
  const bool remote_stop_call = pending != nullptr && pending->action != nullptr &&
                                std::strcmp(pending->action, "RemoteStopTransaction") == 0;
  if (pending != nullptr) {
    ESP_LOGW(TAG,
             "OCPP CALLERROR: charge_point='%s' uniqueId='%s' action='%s' errorCode='%s' description='%s' "
             "connectorId=%u transactionId=%u current_limit=%.1f A",
             this->charge_point_id_.c_str(), unique_id.c_str(), pending->action == nullptr ? "" : pending->action, error_code.c_str(),
             description.c_str(), pending->connector_id, pending->transaction_id, pending->current_limit);
  } else {
    ESP_LOGW(TAG, "OCPP CALLERROR: charge_point='%s' uniqueId='%s' errorCode='%s' description='%s'",
             this->charge_point_id_.c_str(), unique_id.c_str(), error_code.c_str(), description.c_str());
  }
  if (set_charging_profile_call)
    this->set_charging_profile_in_flight_ = false;
  if (remote_start_call) {
    this->remote_start_in_flight_ = false;
    this->remote_start_in_flight_connector_id_ = 0;
    this->remote_start_in_flight_id_tag_.clear();
    this->remote_start_in_flight_use_current_limit_ = false;
    this->remote_start_in_flight_current_limit_ = 0.0f;
  }
  if (remote_stop_call) {
    this->remote_stop_in_flight_ = false;
    this->remote_stop_in_flight_transaction_id_ = 0;
  }
  this->clear_pending_call_(unique_id);
  if (remote_stop_call)
    this->send_pending_remote_stop_if_ready_();
  if (remote_start_call)
    this->send_pending_remote_start_if_ready_();
  if (set_charging_profile_call)
    this->send_pending_set_charging_profile_if_ready_();
}

void OcppServer::request_set_charging_profile_(uint8_t connector_id, uint32_t transaction_id, float current_limit) {
  this->pending_set_charging_profile_ = true;
  this->pending_set_charging_profile_connector_id_ = connector_id;
  this->pending_set_charging_profile_transaction_id_ = transaction_id;
  this->pending_set_charging_profile_current_limit_ = current_limit;
  this->send_pending_set_charging_profile_if_ready_();
}

void OcppServer::send_pending_set_charging_profile_if_ready_() {
  if (!this->pending_set_charging_profile_ || this->set_charging_profile_in_flight_)
    return;

  const uint8_t connector_id = this->pending_set_charging_profile_connector_id_;
  const uint32_t transaction_id = this->pending_set_charging_profile_transaction_id_;
  const float current_limit = this->pending_set_charging_profile_current_limit_;
  if (this->send_set_charging_profile_now_(connector_id, transaction_id, current_limit))
    this->pending_set_charging_profile_ = false;
}

bool OcppServer::send_set_charging_profile_now_(uint8_t connector_id, uint32_t transaction_id, float current_limit) {
  char limit_buf[16];
  std::snprintf(limit_buf, sizeof(limit_buf), "%.1f", current_limit);

  std::string payload = "{";
  payload += "\"connectorId\":" + to_string(connector_id);
  payload += ",\"csChargingProfiles\":{\"chargingProfileId\":1";
  payload += ",\"transactionId\":" + to_string(transaction_id);
  payload += ",\"stackLevel\":1,\"chargingProfilePurpose\":\"TxProfile\"";
  payload += ",\"chargingProfileKind\":\"Relative\"";
  payload += ",\"chargingSchedule\":{\"chargingRateUnit\":\"A\",\"chargingSchedulePeriod\":[{";
  payload += "\"startPeriod\":0,\"limit\":";
  payload += limit_buf;
  payload += "}]}}}";

  ESP_LOGI(TAG,
           "Sending SetChargingProfile: charge_point='%s' connectorId=%u transactionId=%u current_limit=%.1f A",
           this->charge_point_id_.c_str(), connector_id, transaction_id, current_limit);
  auto *connector = this->find_connector_(connector_id);
  if (connector != nullptr && connector->has_active_transaction && connector->active_transaction_id == transaction_id)
    connector->charging_profile_applied = true;
  std::string unique_id = this->send_ocpp_call_("set-charging-profile", "SetChargingProfile", payload, connector_id,
                                               transaction_id, current_limit, true);
  this->set_charging_profile_in_flight_ = this->find_pending_call_(unique_id) != nullptr;
  return this->set_charging_profile_in_flight_;
}

std::string OcppServer::send_ocpp_call_(const char *unique_prefix, const char *action, const std::string &payload_json,
                                        uint8_t connector_id, uint32_t transaction_id, float current_limit,
                                        bool fixed_unique_id) {
  std::string unique_id = fixed_unique_id ? std::string(unique_prefix) : this->next_unique_id_(unique_prefix);
  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGW(TAG, "Cannot send %s; no OCPP wallbox is connected", action);
    return unique_id;
  }
  std::string message = "[2,\"" + json_escape(unique_id) + "\",\"" + action + "\"," + payload_json + "]";
  this->track_pending_call_(unique_id, action, connector_id, transaction_id, current_limit);
  if (!this->queue_ws_text_(std::move(message)))
    this->clear_pending_call_(unique_id);
  return unique_id;
}

bool OcppServer::queue_ws_text_(std::string message) {
  if (this->client_ == nullptr || !this->handshake_done_)
    return false;

  if (this->tx_queue_count_ >= this->tx_queue_.size()) {
    ESP_LOGW(TAG, "Outbound OCPP queue full; sending message immediately");
    this->send_ws_text_(message);
    return true;
  }

  uint8_t slot = (this->tx_queue_head_ + this->tx_queue_count_) % this->tx_queue_.size();
  this->tx_queue_[slot] = std::move(message);
  this->tx_queue_count_++;
  return true;
}

bool OcppServer::flush_queued_ws_text_() {
  if (this->tx_queue_count_ == 0)
    return false;
  if (this->client_ == nullptr || !this->handshake_done_) {
    this->clear_queued_ws_text_();
    return false;
  }

  std::string message = std::move(this->tx_queue_[this->tx_queue_head_]);
  this->tx_queue_[this->tx_queue_head_].clear();
  this->tx_queue_head_ = (this->tx_queue_head_ + 1) % this->tx_queue_.size();
  this->tx_queue_count_--;
  this->send_ws_text_(message);
  return true;
}

void OcppServer::clear_queued_ws_text_() {
  for (auto &message : this->tx_queue_)
    message.clear();
  this->tx_queue_head_ = 0;
  this->tx_queue_count_ = 0;
}

void OcppServer::send_ws_text_(const std::string &message) {
  if (this->client_ == nullptr)
    return;
  std::string frame;
  frame.push_back(static_cast<char>(0x81));
  if (message.size() < 126) {
    frame.push_back(static_cast<char>(message.size()));
  } else {
    frame.push_back(126);
    frame.push_back(static_cast<char>((message.size() >> 8) & 0xFF));
    frame.push_back(static_cast<char>(message.size() & 0xFF));
  }
  frame += message;
  this->client_->write(frame.data(), frame.size());
}

void OcppServer::send_ocpp_error_(const std::string &unique_id, const char *code, const char *description) {
  std::string response = "[4,\"" + json_escape(unique_id) + "\",\"" + code + "\",\"" + description + "\",{}]";
  this->send_ws_text_(response);
}

}  // namespace esphome::ocpp

#endif  // USE_OCPP
