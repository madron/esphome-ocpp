#include "ocpp.h"

#ifdef USE_OCPP

#include "esphome/core/alloc_helpers.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace esphome::ocpp {
namespace {

static const char *const TAG = "ocpp";
static const char *const REMOTE_START_ID_TAG = "esphome-ocpp";
static constexpr size_t MAX_RX_BUFFER = 4096;
static constexpr size_t MAX_WS_PAYLOAD = 2048;
static constexpr size_t MAX_WS_FRAMES_PER_LOOP = 1;
static constexpr const char *CURRENT_TIME = "1970-01-01T00:00:00Z";
static constexpr const char *WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

uint32_t rol(uint32_t value, uint8_t bits) { return (value << bits) | (value >> (32 - bits)); }

std::array<uint8_t, 20> sha1(const std::string &input) {
  uint64_t bit_len = static_cast<uint64_t>(input.size()) * 8;
  std::string data(input);
  data.push_back(static_cast<char>(0x80));
  while ((data.size() % 64) != 56)
    data.push_back(0);
  for (int i = 7; i >= 0; i--)
    data.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));

  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
    uint32_t w[80];
    for (size_t i = 0; i < 16; i++) {
      size_t j = chunk + i * 4;
      w[i] = (uint32_t(static_cast<uint8_t>(data[j])) << 24) |
             (uint32_t(static_cast<uint8_t>(data[j + 1])) << 16) |
             (uint32_t(static_cast<uint8_t>(data[j + 2])) << 8) | uint32_t(static_cast<uint8_t>(data[j + 3]));
    }
    for (size_t i = 16; i < 80; i++)
      w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (size_t i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t temp = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = temp;
    }
    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::array<uint8_t, 20> out{};
  uint32_t h[5] = {h0, h1, h2, h3, h4};
  for (size_t i = 0; i < 5; i++) {
    out[i * 4] = h[i] >> 24;
    out[i * 4 + 1] = h[i] >> 16;
    out[i * 4 + 2] = h[i] >> 8;
    out[i * 4 + 3] = h[i];
  }
  return out;
}

bool equals_ignore_case(const std::string &a, const char *b) {
  size_t len = std::strlen(b);
  if (a.size() != len)
    return false;
  for (size_t i = 0; i < len; i++) {
    auto ca = std::tolower(static_cast<unsigned char>(a[i]));
    auto cb = std::tolower(static_cast<unsigned char>(b[i]));
    if (ca != cb)
      return false;
  }
  return true;
}

std::string trim(const std::string &value) {
  size_t begin = value.find_first_not_of(" \t");
  if (begin == std::string::npos)
    return "";
  size_t end = value.find_last_not_of(" \t");
  return value.substr(begin, end - begin + 1);
}

std::string header_value(const std::string &request, const char *name) {
  size_t pos = request.find("\r\n");
  while (pos != std::string::npos) {
    size_t next = request.find("\r\n", pos + 2);
    if (next == std::string::npos)
      break;
    std::string line = request.substr(pos + 2, next - pos - 2);
    size_t colon = line.find(':');
    if (colon != std::string::npos && equals_ignore_case(line.substr(0, colon), name))
      return trim(line.substr(colon + 1));
    pos = next;
  }
  return "";
}

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

bool current_sensor_value(sensor::Sensor *sensor, float *out) {
  if (sensor == nullptr || out == nullptr || !std::isfinite(sensor->state))
    return false;
  *out = std::max(sensor->state, 0.0f);
  return true;
}

bool power_sensor_value(sensor::Sensor *sensor, float *out) {
  if (sensor == nullptr || out == nullptr || !std::isfinite(sensor->state))
    return false;
  *out = sensor->state;
  return true;
}

bool finite_sensor_value(sensor::Sensor *sensor, float *out) {
  if (sensor == nullptr || out == nullptr || !std::isfinite(sensor->state))
    return false;
  *out = sensor->state;
  return true;
}

bool drawn_current_changed(const std::array<float, 3> &a, const std::array<float, 3> &b) {
  for (uint8_t i = 0; i < a.size(); i++) {
    if (a[i] != b[i])
      return true;
  }
  return false;
}

int phase_index(const char *phase) {
  if (phase == nullptr || phase[0] == '\0')
    return -1;
  if (std::strcmp(phase, "L1") == 0 || std::strcmp(phase, "L1-N") == 0)
    return 0;
  if (std::strcmp(phase, "L2") == 0 || std::strcmp(phase, "L2-N") == 0)
    return 1;
  if (std::strcmp(phase, "L3") == 0 || std::strcmp(phase, "L3-N") == 0)
    return 2;
  return -1;
}

}  // namespace

void OcppServer::set_path(std::string path) {
  if (path.empty() || path[0] != '/')
    path.insert(path.begin(), '/');
  while (path.size() > 1 && path.back() == '/')
    path.pop_back();
  this->path_ = std::move(path);
}

void OcppServer::set_site(uint8_t phases, float voltage) {
  configure_site(&this->site_, phases, voltage);
}

void OcppServer::set_site_energy_policy(std::string policy) {
  if (policy == "solar") {
    this->site_.limits.energy_policy = SiteEnergyPolicy::SOLAR;
  } else {
    this->site_.limits.energy_policy = SiteEnergyPolicy::NORMAL;
  }
}

void OcppServer::set_solar_export_margin_power(float export_margin_power) {
  this->site_.limits.solar_export_margin_power = std::max(export_margin_power, 0.0f);
  this->update_and_publish_site_headroom_current_if_configured_();
}

void OcppServer::set_solar_export_margin_power_number(OcppSolarExportMarginNumber *number, float initial_value) {
  if (number == nullptr)
    return;
  this->site_.solar_export_margin_power_number = number;
  number->set_parent(this);
  this->set_solar_export_margin_power(initial_value);
  number->publish_state(this->site_.limits.solar_export_margin_power);
}

void OcppServer::set_storage_capacity(float capacity_kwh) {
  if (capacity_kwh > 0.0f)
    this->site_.limits.storage_capacity_kwh = capacity_kwh;
}

void OcppSolarExportMarginNumber::control(float value) {
  if (this->parent_ == nullptr)
    return;
  const float safe_value = std::max(value, 0.0f);
  this->parent_->set_solar_export_margin_power(safe_value);
  this->publish_state(safe_value);
}

void OcppServer::set_grid_max_power(float max_power) {
  this->site_.limits.grid_max_power = max_power;
}

void OcppServer::set_grid_max_phase_imbalance(float max_phase_imbalance) {
  this->site_.limits.grid_max_phase_imbalance = max_phase_imbalance;
}

void OcppServer::set_grid_max_current(float max_current) {
  this->site_.limits.grid_max_current = max_current;
}

void OcppServer::set_grid_headroom_current_sensor(uint8_t phase, sensor::Sensor *headroom_current_sensor) {
  if (phase >= this->site_.grid_headroom_current_sensors.size())
    return;
  this->site_.grid_headroom_current_sensors[phase] = headroom_current_sensor;
}

void OcppServer::set_site_headroom_current_sensor(uint8_t phase, sensor::Sensor *headroom_current_sensor) {
  if (phase >= this->site_.headroom_current_sensors.size())
    return;
  this->site_.headroom_current_sensors[phase] = headroom_current_sensor;
}

void OcppServer::set_site_drawn_current_sensor(uint8_t phase, sensor::Sensor *drawn_current_sensor) {
  if (phase >= this->site_.drawn_current_sensors.size())
    return;
  this->site_.drawn_current_sensors[phase] = drawn_current_sensor;
}

void OcppServer::add_charger(std::string charge_point_id, float max_current, uint8_t phases) {
  if (this->has_charger_ && charger_has_charge_point_id(this->charger_, charge_point_id)) {
    this->charger_.max_current = max_current;
    this->charger_.phases = phases == 3 ? 3 : 1;
    return;
  }
  configure_charger(&this->charger_, std::move(charge_point_id), max_current, phases);
  this->has_charger_ = true;
}

void OcppServer::set_charger_phase_mapping(std::string charge_point_id, uint8_t charger_phase, uint8_t site_phase) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || charger_phase >= 3 || site_phase >= 3)
    return;
  charger->phase_mapping[charger_phase] = site_phase;
}

void OcppServer::set_charger_drawn_current_sensor(std::string charge_point_id, sensor::Sensor *drawn_current_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr)
    return;
  charger->drawn_current_sensor = drawn_current_sensor;
}

void OcppServer::set_charger_drawn_current_source_sensor(std::string charge_point_id,
                                                         sensor::Sensor *drawn_current_source_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr)
    return;
  charger->drawn_current_source_sensor = drawn_current_source_sensor;
}

void OcppServer::set_charger_drawn_current_source_phase_sensor(std::string charge_point_id, uint8_t phase,
                                                               sensor::Sensor *drawn_current_source_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || phase >= 3)
    return;
  charger->drawn_current_source_sensors[phase] = drawn_current_source_sensor;
}

void OcppServer::add_connector(std::string charge_point_id, uint8_t connector_id, float max_current) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr) {
    this->add_charger(charge_point_id, max_current);
    charger = this->find_charger_(charge_point_id);
  }
  if (charger == nullptr)
    return;
  charger->connector = ConfiguredConnector{connector_id,
                                           effective_connector_max_current(charger->max_current, max_current)};
  this->update_connector_allocation_(&charger->connector);
  charger->has_connector = true;
}

void OcppServer::set_connector_available_current_sensor(std::string charge_point_id, uint8_t connector_id,
                                                        sensor::Sensor *available_current_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.available_current_sensor = available_current_sensor;
}

void OcppServer::set_connector_allocated_current_sensor(std::string charge_point_id, uint8_t connector_id,
                                                        sensor::Sensor *allocated_current_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.allocated_current_sensor = allocated_current_sensor;
}

void OcppServer::set_connector_drawn_current_max_sensor(std::string charge_point_id, uint8_t connector_id,
                                                        sensor::Sensor *drawn_current_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.drawn_current_sensor = drawn_current_sensor;
}

void OcppServer::set_connector_drawn_current_sensor(std::string charge_point_id, uint8_t connector_id, uint8_t phase,
                                                    sensor::Sensor *drawn_current_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id || phase >= 3)
    return;
  charger->connector.drawn_current_sensors[phase] = drawn_current_sensor;
}

void OcppServer::set_connector_current_sensor(std::string charge_point_id, uint8_t connector_id,
                                              sensor::Sensor *current_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.current_sensor = current_sensor;
}

void OcppServer::set_connector_power_sensor(std::string charge_point_id, uint8_t connector_id,
                                            sensor::Sensor *power_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.power_sensor = power_sensor;
}

void OcppServer::set_connector_state_sensor(std::string charge_point_id, uint8_t connector_id,
                                            text_sensor::TextSensor *state_sensor) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.state_sensor = state_sensor;
}

void OcppServer::set_connector_current_limit_number(std::string charge_point_id, uint8_t connector_id,
                                                    OcppCurrentLimitNumber *current_limit_number,
                                                    float initial_limit) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.current_limit_number = current_limit_number;
  charger->connector.preferred_current_limit = initial_limit;
  charger->connector.has_preferred_current_limit = true;
  this->update_connector_allocation_(&charger->connector);
  this->publish_connector_allocation_if_configured_(&charger->connector);
  current_limit_number->set_parent(this, connector_id);
}

void OcppServer::set_connector_enabled_switch(std::string charge_point_id, uint8_t connector_id,
                                              OcppConnectorEnabledSwitch *enabled_switch) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.enabled_switch = enabled_switch;
  enabled_switch->set_parent(this, connector_id);
}

void OcppServer::set_connector_restart_button(std::string charge_point_id, uint8_t connector_id,
                                              OcppConnectorButton *restart_button) {
  auto *charger = this->find_charger_(charge_point_id);
  if (charger == nullptr || !charger->has_connector || charger->connector.id != connector_id)
    return;
  charger->connector.restart_button = restart_button;
  restart_button->set_parent(this, connector_id);
}

bool OcppServer::has_latest_current_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr && connector->has_latest_current_import;
}

bool OcppServer::has_session_current_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr && connector->has_session_current_import;
}

bool OcppServer::has_latest_power_active_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr && connector->has_latest_power_active_import;
}

float OcppServer::get_latest_current_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr ? connector->latest_current_import : 0.0f;
}

float OcppServer::get_effective_drawn_current(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr ? this->effective_drawn_current_(*connector) : 0.0f;
}

float OcppServer::get_latest_power_active_import(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector != nullptr ? connector->latest_power_active_import : 0.0f;
}

void OcppServer::setup() {
  this->server_ = socket::socket_ip_loop_monitored(SOCK_STREAM, 0);
  if (this->server_ == nullptr) {
    ESP_LOGE(TAG, "Could not create OCPP listen socket");
    this->mark_failed();
    return;
  }

  int enable = 1;
  this->server_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  sockaddr_storage addr{};
  socklen_t addr_len = socket::set_sockaddr_any(reinterpret_cast<sockaddr *>(&addr), sizeof(addr), this->port_);
  if (addr_len == 0 || this->server_->bind(reinterpret_cast<sockaddr *>(&addr), addr_len) != 0 ||
      this->server_->listen(1) != 0 || this->server_->setblocking(false) != 0) {
    ESP_LOGE(TAG, "Could not start OCPP listener on port %u", this->port_);
    this->mark_failed();
  }

  if (this->has_charger_ && this->charger_.has_connector && this->charger_.connector.current_limit_number != nullptr &&
      this->charger_.connector.has_preferred_current_limit) {
    this->charger_.connector.current_limit_number->publish_state(this->charger_.connector.preferred_current_limit);
  }
  if (this->has_charger_) {
    this->update_charger_drawn_current_(&this->charger_);
    this->publish_charger_drawn_current_if_configured_(&this->charger_);
  }
  this->update_grid_headroom_current_();
  this->publish_grid_headroom_current_if_configured_();
  this->update_site_headroom_current_();
  this->publish_site_headroom_current_if_configured_();
  this->update_site_drawn_current_();
  this->publish_site_drawn_current_if_configured_();
  if (this->has_charger_ && this->charger_.has_connector) {
    auto *connector = &this->charger_.connector;
    this->update_connector_allocation_(connector);
    this->publish_connector_allocation_if_configured_(connector);
    this->publish_connector_state_if_configured_(connector);
    this->publish_drawn_current_if_configured_(connector);
    if (connector->enabled_switch != nullptr)
      connector->enabled_switch->publish_state(connector->enabled);
  }
}

void OcppServer::loop() {
  if (this->server_ != nullptr && this->server_->ready())
    this->accept_client_();
  if (this->flush_queued_ws_text_())
    return;
  if (this->client_ != nullptr && this->client_->ready())
    this->read_client_();
  if (this->has_charger_)
    this->update_and_publish_charger_drawn_current_if_configured_(&this->charger_);
  this->update_and_publish_grid_headroom_current_if_configured_();
  this->update_and_publish_site_headroom_current_if_configured_();
  this->update_and_publish_site_drawn_current_if_configured_();
}

void OcppServer::dump_config() {
  ESP_LOGCONFIG(TAG, "OCPP server:");
  ESP_LOGCONFIG(TAG, "  Listen: 0.0.0.0:%u%s", this->port_, this->path_.c_str());
  ESP_LOGCONFIG(TAG, "  Allocation: strategy=equal min_current=%.1f A", this->allocation_min_current_);
  ESP_LOGCONFIG(TAG, "  Site: phases=%u voltage=%.1f V", this->site_.limits.phases, this->site_.limits.voltage);
  ESP_LOGCONFIG(TAG, "    Policy=%s", this->site_.limits.energy_policy == SiteEnergyPolicy::SOLAR ? "solar" : "normal");
  if (this->site_.limits.energy_policy == SiteEnergyPolicy::SOLAR)
    ESP_LOGCONFIG(TAG, "    Solar export_margin_power=%.0f W", this->site_.limits.solar_export_margin_power);
  if (this->site_.limits.grid_max_power.has_value())
    ESP_LOGCONFIG(TAG, "    Grid max_power=%.0f W", this->site_.limits.grid_max_power.value());
  if (this->site_.limits.grid_max_phase_imbalance.has_value())
    ESP_LOGCONFIG(TAG, "    Grid max_phase_imbalance=%.0f W", this->site_.limits.grid_max_phase_imbalance.value());
  if (this->site_.limits.grid_max_current.has_value())
    ESP_LOGCONFIG(TAG, "    Grid max_current=%.1f A per phase", this->site_.limits.grid_max_current.value());
  if (this->site_.limits.storage_capacity_kwh.has_value())
    ESP_LOGCONFIG(TAG, "    Storage capacity=%.2f kWh", this->site_.limits.storage_capacity_kwh.value());
  ESP_LOGCONFIG(TAG, "  Configured charger: %s", this->has_charger_ ? this->charger_.charge_point_id.c_str() : "none");
  if (this->has_charger_)
    ESP_LOGCONFIG(TAG, "    Charger max_current=%.1f A per phase", this->charger_.max_current);
  if (this->has_charger_ && this->charger_.has_connector) {
    ESP_LOGCONFIG(TAG, "    Connector %u max_current=%.1f A", this->charger_.connector.id,
                  this->charger_.connector.max_current);
  }
  ESP_LOGCONFIG(TAG,
                "  Implemented messages: BootNotification, Heartbeat, Authorize, StatusNotification, "
                "StartTransaction, StopTransaction, MeterValues");
}

float OcppServer::get_setup_priority() const { return setup_priority::WIFI - 1.0f; }

void OcppServer::disconnect() {
  if (this->client_ == nullptr) {
    ESP_LOGI(TAG, "No OCPP wallbox connection to disconnect");
    return;
  }
  ESP_LOGI(TAG, "Disconnecting OCPP wallbox '%s' on request", this->charge_point_id_.c_str());
  this->close_client_();
}

void OcppServer::remote_start(uint8_t connector_id) {
  auto *connector = this->find_connector_(connector_id);
  if (connector != nullptr) {
    this->update_connector_allocation_(connector, true);
    this->publish_connector_allocation_if_configured_(connector);
    if (connector->allocated_current <= 0.0f) {
      ESP_LOGI(TAG, "Not starting connector session: connectorId=%u allocated_current=0.0 A", connector_id);
      return;
    }
    this->remote_start_(connector_id, REMOTE_START_ID_TAG, true, connector->allocated_current);
    return;
  }
  this->remote_start_(connector_id, REMOTE_START_ID_TAG, false, 0.0f);
}

void OcppServer::remote_start_(uint8_t connector_id, std::string id_tag, bool use_current_limit, float current_limit) {
  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction; no OCPP wallbox is connected");
    return;
  }
  if (use_current_limit && current_limit <= 0) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction with non-positive current limit %.1f A", current_limit);
    return;
  }

  float limit = current_limit;
  const auto *connector = this->find_connector_(connector_id);
  if (this->has_charger_ && connector == nullptr) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction; connector %u is not configured for charge point '%s'",
             connector_id, this->charge_point_id_.c_str());
    return;
  }
  if (connector != nullptr && !connector->enabled) {
    ESP_LOGW(TAG, "Cannot send RemoteStartTransaction; connector %u is disabled", connector_id);
    return;
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
  this->send_ocpp_call_("remote-start", "RemoteStartTransaction", payload, connector_id, 0,
                        use_current_limit ? limit : 0.0f);
}

void OcppServer::remote_stop() {
  auto *connector = this->find_active_transaction_connector_();
  if (connector == nullptr) {
    ESP_LOGW(TAG, "Cannot send RemoteStopTransaction; no connector has a known active transaction");
    return;
  }
  this->remote_stop(connector->active_transaction_id);
}

void OcppServer::remote_stop_connector(uint8_t connector_id) {
  auto *connector = this->find_connector_(connector_id);
  if (connector == nullptr || !connector->has_active_transaction) {
    ESP_LOGW(TAG, "Cannot send RemoteStopTransaction; connector %u has no known active transaction", connector_id);
    return;
  }
  this->remote_stop(connector->active_transaction_id);
}

void OcppServer::remote_stop(uint32_t transaction_id) {
  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGW(TAG, "Cannot send RemoteStopTransaction; no OCPP wallbox is connected");
    return;
  }

  std::string payload = "{\"transactionId\":" + to_string(transaction_id) + "}";

  ESP_LOGI(TAG, "Sending RemoteStopTransaction: charge_point='%s' transactionId=%u", this->charge_point_id_.c_str(),
           transaction_id);
  this->send_ocpp_call_("remote-stop", "RemoteStopTransaction", payload, 0, transaction_id);
}

void OcppServer::set_current_limit(uint8_t connector_id, float current_limit) {
  if (current_limit <= 0) {
    ESP_LOGW(TAG, "Cannot send SetChargingProfile with non-positive current limit %.1f A", current_limit);
    return;
  }

  float limit = current_limit;
  auto *connector = this->find_connector_(connector_id);
  if (this->has_charger_ && connector == nullptr) {
    ESP_LOGW(TAG, "Cannot send SetChargingProfile; connector %u is not configured for charge point '%s'", connector_id,
             this->charge_point_id_.c_str());
    return;
  }
  if (connector != nullptr && limit > connector->max_current) {
    ESP_LOGW(TAG, "Clamping SetChargingProfile current limit %.1f A to configured connector maximum %.1f A", limit,
             connector->max_current);
    limit = connector->max_current;
  }

  if (connector != nullptr) {
    connector->preferred_current_limit = limit;
    connector->has_preferred_current_limit = true;
    connector->charging_profile_applied = false;
    this->update_connector_allocation_(connector);
    this->publish_connector_allocation_if_configured_(connector);
    if (connector->current_limit_number != nullptr)
      connector->current_limit_number->publish_state(limit);
  }

  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGI(TAG, "Stored current limit %.1f A for connector %u; no OCPP wallbox is connected", limit, connector_id);
    return;
  }
  if (connector == nullptr || !connector->has_active_transaction) {
    ESP_LOGI(TAG, "Stored current limit %.1f A for connector %u; no active transaction ID is known yet", limit,
             connector_id);
    return;
  }

  if (!connector->enabled) {
    ESP_LOGI(TAG, "Stored current limit %.1f A for disabled connector %u", limit, connector_id);
    return;
  }

  if (connector->allocated_current <= 0.0f) {
    ESP_LOGI(TAG, "Stopping connector %u because allocated_current=0.0 A", connector_id);
    this->remote_stop(connector->active_transaction_id);
    return;
  }

  this->send_set_charging_profile_(connector_id, connector->active_transaction_id, connector->allocated_current);
}

void OcppServer::set_connector_enabled(uint8_t connector_id, bool enabled) {
  auto *connector = this->find_connector_(connector_id);
  if (this->has_charger_ && connector == nullptr) {
    ESP_LOGW(TAG, "Cannot %s connector; connector %u is not configured for charge point '%s'",
             enabled ? "enable" : "disable", connector_id, this->charge_point_id_.c_str());
    return;
  }
  if (connector == nullptr)
    return;

  connector->enabled = enabled;
  connector->charging_profile_applied = false;
  this->update_connector_allocation_(connector);
  this->publish_connector_allocation_if_configured_(connector);
  if (connector->enabled_switch != nullptr)
    connector->enabled_switch->publish_state(enabled);

  if (this->client_ == nullptr || !this->handshake_done_) {
    ESP_LOGI(TAG, "Stored connector %u as %s; no OCPP wallbox is connected", connector_id,
             enabled ? "enabled" : "disabled");
    return;
  }
  if (!enabled) {
    this->pending_session_restart_connector_id_ = 0;
    if (connector->has_active_transaction) {
      this->remote_stop(connector->active_transaction_id);
    } else {
      ESP_LOGI(TAG, "Stored connector %u as disabled; no active transaction ID is known yet", connector_id);
    }
    return;
  }

  if (connector->has_active_transaction) {
    if (connector->allocated_current <= 0.0f) {
      ESP_LOGI(TAG, "Stopping connector %u because allocated_current=0.0 A", connector_id);
      this->remote_stop(connector->active_transaction_id);
      return;
    }
    ESP_LOGI(TAG, "Stored connector %u as enabled; transaction already active", connector_id);
    this->send_set_charging_profile_(connector_id, connector->active_transaction_id, connector->allocated_current);
    return;
  }
  if (connector->allocated_current <= 0.0f) {
    ESP_LOGI(TAG, "Not starting connector session: connectorId=%u allocated_current=0.0 A", connector_id);
    return;
  }
  this->remote_start(connector_id);
}

bool OcppServer::is_connector_enabled(uint8_t connector_id) const {
  const auto *connector = this->find_connector_(connector_id);
  return connector == nullptr || connector->enabled;
}

void OcppServer::restart_connector_session(uint8_t connector_id) {
  auto *connector = this->find_connector_(connector_id);
  if (this->has_charger_ && connector == nullptr) {
    ESP_LOGW(TAG, "Cannot restart connector session; connector %u is not configured for charge point '%s'", connector_id,
             this->charge_point_id_.c_str());
    return;
  }
  if (connector != nullptr && connector->has_active_transaction) {
    ESP_LOGI(TAG, "Restarting connector session: connectorId=%u transactionId=%u", connector_id,
             connector->active_transaction_id);
    this->pending_session_restart_connector_id_ = connector->enabled ? connector_id : 0;
    this->remote_stop(connector->active_transaction_id);
    return;
  }
  if (connector != nullptr && !connector->enabled) {
    ESP_LOGI(TAG, "Not starting connector session: connectorId=%u is disabled", connector_id);
    return;
  }
  ESP_LOGI(TAG, "Starting connector session: connectorId=%u; no active transaction ID is known", connector_id);
  this->remote_start(connector_id);
}

void OcppServer::accept_client_() {
  sockaddr_storage addr{};
  socklen_t addr_len = sizeof(addr);
  auto client = this->server_->accept_loop_monitored(reinterpret_cast<sockaddr *>(&addr), &addr_len);
  if (client == nullptr)
    return;
  if (this->client_ != nullptr) {
    ESP_LOGW(TAG, "Rejecting additional OCPP connection; minimal server supports one wallbox at a time");
    client->close();
    return;
  }
  client->setblocking(false);
  this->client_ = std::move(client);
  this->rx_buffer_.clear();
  this->charge_point_id_.clear();
  this->handshake_done_ = false;
  ESP_LOGI(TAG, "OCPP wallbox connected");
}

void OcppServer::close_client_() {
  if (this->client_ != nullptr)
    this->client_->close();
  this->client_.reset();
  this->rx_buffer_.clear();
  this->handshake_done_ = false;
  this->charge_point_id_.clear();
  this->pending_profile_connector_id_ = 0;
  this->pending_session_restart_connector_id_ = 0;
  this->pending_profile_current_limit_ = 0.0f;
  this->clear_pending_calls_();
  this->clear_queued_ws_text_();
}

void OcppServer::read_client_() {
  uint8_t buffer[512];
  while (true) {
    ssize_t read = this->client_->read(buffer, sizeof(buffer));
    if (read > 0) {
      this->rx_buffer_.append(reinterpret_cast<const char *>(buffer), read);
      if (this->rx_buffer_.size() > MAX_RX_BUFFER) {
        ESP_LOGW(TAG, "Closing OCPP connection with oversized receive buffer");
        this->close_client_();
        return;
      }
      continue;
    }
    if (read == 0) {
      ESP_LOGI(TAG, "OCPP wallbox disconnected");
      this->close_client_();
      return;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      ESP_LOGW(TAG, "OCPP socket read failed: errno=%d", errno);
      this->close_client_();
      return;
    }
    break;
  }
  if (!this->handshake_done_)
    this->handle_http_handshake_();
  if (this->handshake_done_)
    this->handle_ws_frames_();
}

bool OcppServer::request_matches_path_(const std::string &uri) {
  if (uri == this->path_) {
    this->charge_point_id_ = "";
    return true;
  }
  std::string prefix = this->path_ + "/";
  if (uri.rfind(prefix, 0) != 0)
    return false;
  this->charge_point_id_ = uri.substr(prefix.size());
  return true;
}

ConfiguredCharger *OcppServer::find_charger_(const std::string &charge_point_id) {
  if (this->has_charger_ && charger_has_charge_point_id(this->charger_, charge_point_id))
    return &this->charger_;
  return nullptr;
}

const ConfiguredCharger *OcppServer::find_charger_(const std::string &charge_point_id) const {
  if (this->has_charger_ && charger_has_charge_point_id(this->charger_, charge_point_id))
    return &this->charger_;
  return nullptr;
}

ConfiguredConnector *OcppServer::find_connector_(int connector_id) {
  auto *charger = this->find_charger_(this->charge_point_id_);
  if (charger == nullptr && this->has_charger_ && this->charge_point_id_.empty())
    charger = &this->charger_;
  return find_configured_connector(charger, connector_id);
}

const ConfiguredConnector *OcppServer::find_connector_(int connector_id) const {
  const auto *charger = this->find_charger_(this->charge_point_id_);
  if (charger == nullptr && this->has_charger_ && this->charge_point_id_.empty())
    charger = &this->charger_;
  return find_configured_connector(charger, connector_id);
}

ConfiguredConnector *OcppServer::find_active_transaction_connector_() {
  return this->has_charger_ ? find_active_transaction_connector(&this->charger_) : nullptr;
}

ConfiguredConnector *OcppServer::find_transaction_connector_(uint32_t transaction_id) {
  return this->has_charger_ ? find_transaction_connector(&this->charger_, transaction_id) : nullptr;
}

void OcppServer::note_transaction_id_(uint32_t transaction_id) {
  if (this->next_transaction_id_ <= transaction_id)
    this->next_transaction_id_ = transaction_id + 1;
}

SitePowerMeasurements OcppServer::site_power_measurements_() const {
  SitePowerMeasurements measurements;
  float power = 0.0f;
  if (power_sensor_value(this->site_.grid_power_l1_sensor, &power))
    measurements.grid_power_l1 = power;
  if (power_sensor_value(this->site_.grid_power_l2_sensor, &power))
    measurements.grid_power_l2 = power;
  if (power_sensor_value(this->site_.grid_power_l3_sensor, &power))
    measurements.grid_power_l3 = power;
  if (power_sensor_value(this->site_.grid_power_aggregate_sensor, &power))
    measurements.grid_power_aggregate = power;
  if (power_sensor_value(this->site_.storage_power_l1_sensor, &power))
    measurements.storage_power_l1 = power;
  if (power_sensor_value(this->site_.storage_power_l2_sensor, &power))
    measurements.storage_power_l2 = power;
  if (power_sensor_value(this->site_.storage_power_l3_sensor, &power))
    measurements.storage_power_l3 = power;
  if (power_sensor_value(this->site_.storage_power_aggregate_sensor, &power))
    measurements.storage_power_aggregate = power;
  float value = 0.0f;
  if (finite_sensor_value(this->site_.storage_soc_sensor, &value))
    measurements.storage_soc = value;
  if (finite_sensor_value(this->site_.storage_energy_sensor, &value))
    measurements.storage_energy_kwh = value;
  normalize_site_storage_state(this->site_.limits, &measurements);
  return measurements;
}

bool OcppServer::update_grid_headroom_current_() {
  return update_grid_headroom_current(&this->site_, this->site_power_measurements_());
}

void OcppServer::update_and_publish_grid_headroom_current_if_configured_() {
  if (this->update_grid_headroom_current_())
    this->publish_grid_headroom_current_if_configured_();
}

void OcppServer::publish_grid_headroom_current_if_configured_() {
  publish_grid_headroom_current_if_configured(&this->site_);
}

bool OcppServer::update_site_headroom_current_() {
  return update_site_headroom_current(&this->site_, this->site_power_measurements_());
}

void OcppServer::update_and_publish_site_headroom_current_if_configured_() {
  if (this->update_site_headroom_current_()) {
    this->publish_site_headroom_current_if_configured_();
    if (this->has_charger_ && this->charger_.has_connector) {
      auto *connector = &this->charger_.connector;
      const float previous_allocated_current = connector->allocated_current;
      this->update_connector_allocation_(connector);
      this->publish_connector_allocation_if_configured_(connector);
      if (this->client_ != nullptr && this->handshake_done_ && connector->has_active_transaction &&
          connector->allocated_current != previous_allocated_current) {
        connector->charging_profile_applied = false;
        this->send_preferred_current_limit_if_needed_(connector->id);
      }
    }
  }
}

void OcppServer::publish_site_headroom_current_if_configured_() {
  publish_site_headroom_current_if_configured(&this->site_);
}

float OcppServer::site_available_current_(const ConfiguredCharger &charger, const ConfiguredConnector *connector) const {
  const auto charger_load_phases = charger_effective_load_phases(charger, connector);
  std::array<bool, 3> site_load_phases{};
  for (uint8_t i = 0; i < charger_load_phases.size(); i++) {
    if (!charger_load_phases[i])
      continue;
    const uint8_t site_phase = charger.phase_mapping[i] < site_load_phases.size() ? charger.phase_mapping[i] : 0;
    site_load_phases[site_phase] = true;
  }
  return site_available_current_for_load(this->site_.limits, this->site_power_measurements_(), site_load_phases);
}

uint8_t OcppServer::active_connector_count_(const ConfiguredConnector *prospective_connector) const {
  if (!this->has_charger_ || !this->charger_.has_connector)
    return 0;
  if (!this->charger_.connector.has_active_transaction && prospective_connector != &this->charger_.connector)
    return 0;
  return 1;
}

float OcppServer::connector_current_for_allocation_(const ConfiguredConnector &connector) const {
  if (!connector.has_active_transaction)
    return 0.0f;
  return this->effective_drawn_current_(connector);
}

void OcppServer::update_connector_allocation_(ConfiguredConnector *connector, bool include_connector_as_active) {
  float available_current = 0.0f;
  if (connector != nullptr && this->has_charger_) {
    const float site_available_current = this->site_available_current_(this->charger_, connector);
    const float connector_current = this->connector_current_for_allocation_(*connector);
    available_current = equal_available_current(site_available_current, connector_current,
                                                this->active_connector_count_(include_connector_as_active ? connector : nullptr));
  }
  update_connector_allocation(connector, available_current, this->allocation_min_current_);
}

void OcppServer::publish_connector_allocation_if_configured_(ConfiguredConnector *connector) {
  this->publish_available_current_if_configured_(connector);
  this->publish_allocated_current_if_configured_(connector);
}

void OcppServer::publish_available_current_if_configured_(ConfiguredConnector *connector) {
  if (connector != nullptr && connector->available_current_sensor != nullptr)
    connector->available_current_sensor->publish_state(connector->available_current);
}

void OcppServer::publish_allocated_current_if_configured_(ConfiguredConnector *connector) {
  if (connector != nullptr && connector->allocated_current_sensor != nullptr)
    connector->allocated_current_sensor->publish_state(connector->allocated_current);
}

bool OcppServer::update_charger_drawn_current_(ConfiguredCharger *charger) {
  if (charger == nullptr)
    return false;

  std::array<float, 3> drawn_current{};
  bool phase_specific = false;
  float source_current = 0.0f;
  if (current_sensor_value(charger->drawn_current_source_sensor, &source_current)) {
    drawn_current = charger_drawn_current_from_source(source_current);
  } else if (charger->drawn_current_source_sensors[0] != nullptr || charger->drawn_current_source_sensors[1] != nullptr ||
             charger->drawn_current_source_sensors[2] != nullptr) {
    bool source_ready = true;
    for (uint8_t i = 0; i < drawn_current.size(); i++)
      source_ready = current_sensor_value(charger->drawn_current_source_sensors[i], &drawn_current[i]) && source_ready;
    if (source_ready) {
      phase_specific = true;
    } else {
      drawn_current = charger_drawn_current_from_connectors(*charger);
      phase_specific = charger->has_connector && charger->connector.has_phase_specific_current_import;
    }
  } else {
    drawn_current = charger_drawn_current_from_connectors(*charger);
    phase_specific = charger->has_connector && charger->connector.has_phase_specific_current_import;
  }

  const bool changed = drawn_current_changed(charger->latest_drawn_current, drawn_current) ||
                       charger->latest_drawn_current_phase_specific != phase_specific;
  charger->latest_drawn_current = drawn_current;
  charger->latest_drawn_current_phase_specific = phase_specific;
  return changed;
}

void OcppServer::update_and_publish_charger_drawn_current_if_configured_(ConfiguredCharger *charger) {
  if (this->update_charger_drawn_current_(charger))
    this->publish_charger_drawn_current_if_configured_(charger);
}

void OcppServer::publish_charger_drawn_current_if_configured_(ConfiguredCharger *charger) {
  if (charger != nullptr && charger->drawn_current_sensor != nullptr)
    charger->drawn_current_sensor->publish_state(this->drawn_current_max_(*charger));
}

bool OcppServer::update_site_drawn_current_() {
  return update_site_drawn_current(&this->site_, this->has_charger_ ? &this->charger_ : nullptr);
}

void OcppServer::update_and_publish_site_drawn_current_if_configured_() {
  if (this->update_site_drawn_current_())
    this->publish_site_drawn_current_if_configured_();
}

void OcppServer::publish_site_drawn_current_if_configured_() {
  publish_site_drawn_current_if_configured(&this->site_);
}

void OcppServer::reset_session_current_(ConfiguredConnector *connector) {
  reset_connector_session_current(connector);
}

void OcppServer::publish_current_if_configured_(ConfiguredConnector *connector) {
  if (connector != nullptr && connector->current_sensor != nullptr)
    connector->current_sensor->publish_state(connector->latest_current_import);
}

void OcppServer::publish_connector_state_if_configured_(ConfiguredConnector *connector) {
  if (connector != nullptr && connector->state_sensor != nullptr)
    connector->state_sensor->publish_state(connector->state);
}

void OcppServer::publish_drawn_current_if_configured_(ConfiguredConnector *connector) {
  if (connector == nullptr)
    return;
  if (connector->drawn_current_sensor != nullptr)
    connector->drawn_current_sensor->publish_state(this->drawn_current_max_(*connector));
  for (uint8_t i = 0; i < connector->drawn_current_sensors.size(); i++) {
    if (connector->drawn_current_sensors[i] != nullptr)
      connector->drawn_current_sensors[i]->publish_state(connector->latest_drawn_current[i]);
  }
  if (this->has_charger_ && connector == &this->charger_.connector) {
    this->update_and_publish_charger_drawn_current_if_configured_(&this->charger_);
    this->update_and_publish_site_drawn_current_if_configured_();
  }
}

float OcppServer::drawn_current_max_(const ConfiguredCharger &charger) const {
  return charger_drawn_current_max(charger);
}

float OcppServer::drawn_current_max_(const ConfiguredConnector &connector) const {
  return connector_drawn_current_max(connector);
}

float OcppServer::effective_drawn_current_(const ConfiguredConnector &connector) const {
  return effective_connector_drawn_current(connector);
}

void OcppServer::mark_transaction_started_(uint8_t connector_id, uint32_t transaction_id, const char *id_tag) {
  auto *connector = this->find_connector_(connector_id);
  if (connector == nullptr)
    return;
  if (connector->has_active_transaction && connector->active_transaction_id != transaction_id) {
    ESP_LOGW(TAG, "Replacing active transaction state: connectorId=%u oldTransactionId=%u newTransactionId=%u",
             connector_id, connector->active_transaction_id, transaction_id);
  }
  connector->has_active_transaction = true;
  connector->active_transaction_id = transaction_id;
  connector->active_id_tag = id_tag == nullptr ? "" : id_tag;
  this->reset_session_current_(connector);
  this->publish_current_if_configured_(connector);
  this->publish_drawn_current_if_configured_(connector);
  this->note_transaction_id_(transaction_id);
}

void OcppServer::recover_transaction_from_meter_values_(uint8_t connector_id, uint32_t transaction_id) {
  auto *connector = this->find_connector_(connector_id);
  if (connector == nullptr)
    return;
  if (connector->has_active_transaction) {
    if (connector->active_transaction_id == transaction_id)
      return;
    ESP_LOGW(TAG,
             "MeterValues referenced transactionId=%u for connectorId=%u, but transactionId=%u was tracked; replacing",
             transaction_id, connector_id, connector->active_transaction_id);
  } else {
    ESP_LOGI(TAG, "Recovered active transaction from MeterValues: charge_point='%s' connectorId=%u transactionId=%u",
             this->charge_point_id_.c_str(), connector_id, transaction_id);
  }
  connector->has_active_transaction = true;
  connector->active_transaction_id = transaction_id;
  connector->active_id_tag.clear();
  this->reset_session_current_(connector);
  this->publish_current_if_configured_(connector);
  this->publish_drawn_current_if_configured_(connector);
  this->note_transaction_id_(transaction_id);
  if (connector->is_charging)
    this->send_preferred_current_limit_if_needed_(connector_id);
}

void OcppServer::clear_transaction_(uint32_t transaction_id) {
  auto *connector = this->find_transaction_connector_(transaction_id);
  if (connector == nullptr) {
    ESP_LOGD(TAG, "StopTransaction referenced untracked transactionId=%u", transaction_id);
    return;
  }
  ESP_LOGI(TAG, "Cleared active transaction: charge_point='%s' connectorId=%u transactionId=%u",
           this->charge_point_id_.c_str(), connector->id, transaction_id);
  connector->has_active_transaction = false;
  connector->active_transaction_id = 0;
  connector->active_id_tag.clear();
  connector->is_charging = false;
  connector->charging_profile_applied = false;
  this->reset_session_current_(connector);
  connector->latest_power_active_import = 0.0f;
  connector->has_latest_power_active_import = true;
  this->publish_current_if_configured_(connector);
  this->publish_drawn_current_if_configured_(connector);
  if (connector->power_sensor != nullptr)
    connector->power_sensor->publish_state(0.0f);
}

void OcppServer::send_preferred_current_limit_if_needed_(uint8_t connector_id) {
  auto *connector = this->find_connector_(connector_id);
  if (connector == nullptr || connector->charging_profile_applied)
    return;
  this->update_connector_allocation_(connector);
  this->publish_connector_allocation_if_configured_(connector);
  if (!connector->enabled)
    return;
  if (!connector->has_active_transaction) {
    ESP_LOGD(TAG, "Deferring SetChargingProfile for connectorId=%u; no active transaction ID is known yet", connector_id);
    return;
  }
  if (connector->allocated_current <= 0.0f) {
    ESP_LOGI(TAG, "Stopping connector %u because allocated_current=0.0 A", connector_id);
    this->remote_stop(connector->active_transaction_id);
    return;
  }
  this->send_set_charging_profile_(connector_id, connector->active_transaction_id, connector->allocated_current);
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

PendingOcppCall *OcppServer::find_pending_call_(const std::string &unique_id) {
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

void OcppServer::handle_http_handshake_() {
  size_t header_end = this->rx_buffer_.find("\r\n\r\n");
  if (header_end == std::string::npos)
    return;
  std::string request = this->rx_buffer_.substr(0, header_end + 4);
  this->rx_buffer_.erase(0, header_end + 4);

  size_t first_space = request.find(' ');
  size_t second_space = first_space == std::string::npos ? std::string::npos : request.find(' ', first_space + 1);
  std::string uri = second_space == std::string::npos ? "" : request.substr(first_space + 1, second_space - first_space - 1);
  size_t query = uri.find('?');
  if (query != std::string::npos)
    uri.erase(query);

  std::string key = header_value(request, "Sec-WebSocket-Key");
  if (request.rfind("GET ", 0) != 0 || key.empty() || !this->request_matches_path_(uri)) {
    static constexpr const char *BAD_REQUEST = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
    this->client_->write(BAD_REQUEST, std::strlen(BAD_REQUEST));
    this->close_client_();
    return;
  }
  const auto *configured_charger = this->find_charger_(this->charge_point_id_);
  if (this->has_charger_ && configured_charger == nullptr) {
    ESP_LOGW(TAG, "Rejecting unknown OCPP charge point '%s'", this->charge_point_id_.c_str());
    static constexpr const char *FORBIDDEN = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n";
    this->client_->write(FORBIDDEN, std::strlen(FORBIDDEN));
    this->close_client_();
    return;
  }

  std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n";
  response += "Sec-WebSocket-Accept: " + this->websocket_accept_key_(key) + "\r\n";
  if (header_value(request, "Sec-WebSocket-Protocol").find("ocpp1.6") != std::string::npos)
    response += "Sec-WebSocket-Protocol: ocpp1.6\r\n";
  response += "\r\n";
  this->client_->write(response.data(), response.size());
  this->handshake_done_ = true;
  ESP_LOGI(TAG, "OCPP WebSocket accepted for charge point '%s'", this->charge_point_id_.c_str());
  if (configured_charger != nullptr) {
    ESP_LOGI(TAG, "Using configured charger '%s'", configured_charger->charge_point_id.c_str());
  }
}

std::string OcppServer::websocket_accept_key_(const std::string &client_key) {
  auto digest = sha1(client_key + WS_GUID);
  return base64_encode(digest.data(), digest.size());
}

void OcppServer::handle_ws_frames_() {
  size_t frames_handled = 0;
  while (this->rx_buffer_.size() >= 2) {
    const uint8_t *data = reinterpret_cast<const uint8_t *>(this->rx_buffer_.data());
    uint8_t opcode = data[0] & 0x0F;
    bool masked = (data[1] & 0x80) != 0;
    uint64_t payload_len = data[1] & 0x7F;
    size_t pos = 2;
    if (payload_len == 126) {
      if (this->rx_buffer_.size() < 4)
        return;
      payload_len = (uint16_t(data[2]) << 8) | data[3];
      pos = 4;
    } else if (payload_len == 127) {
      ESP_LOGW(TAG, "Closing OCPP connection with unsupported large WebSocket frame");
      this->close_client_();
      return;
    }
    if (!masked || payload_len > MAX_WS_PAYLOAD) {
      ESP_LOGW(TAG, "Closing OCPP connection with invalid WebSocket frame");
      this->close_client_();
      return;
    }
    if (this->rx_buffer_.size() < pos + 4 + payload_len)
      return;

    uint8_t mask[4] = {data[pos], data[pos + 1], data[pos + 2], data[pos + 3]};
    pos += 4;
    std::string payload;
    payload.resize(payload_len);
    for (size_t i = 0; i < payload_len; i++)
      payload[i] = static_cast<char>(data[pos + i] ^ mask[i % 4]);
    this->rx_buffer_.erase(0, pos + payload_len);

    if (opcode == 0x8) {
      this->close_client_();
      return;
    }
    if (opcode == 0x9) {
      std::string pong;
      pong.push_back(static_cast<char>(0x8A));
      pong.push_back(static_cast<char>(payload.size()));
      pong += payload;
      this->client_->write(pong.data(), pong.size());
      continue;
    }
    if (opcode == 0x1)
      this->handle_ws_text_(payload);

    if (++frames_handled >= MAX_WS_FRAMES_PER_LOOP)
      return;
  }
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
  ESP_LOGD(TAG, "OCPP message: charge_point='%s' action='%s' uniqueId='%s'", this->charge_point_id_.c_str(),
           action.c_str(), unique_id.c_str());
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
      this->publish_drawn_current_if_configured_(connector);
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
    this->update_connector_allocation_(started_connector);
    this->publish_connector_allocation_if_configured_(started_connector);
    if (!started_connector->enabled) {
      ESP_LOGI(TAG, "Stopping transaction %u because connector %d is disabled", transaction_id, connector_id);
      this->remote_stop(transaction_id);
      this->pending_profile_connector_id_ = 0;
      this->pending_profile_current_limit_ = 0.0f;
      return;
    }
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
    this->send_set_charging_profile_(static_cast<uint8_t>(connector_id), transaction_id, allocated_current);
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
  uint8_t connector_phases = 3;
  if (this->has_charger_ && connector == &this->charger_.connector)
    connector_phases = this->charger_.phases;
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
    ESP_LOGD(TAG, "MeterValues: charge_point='%s' connectorId=%d transactionId=%d", this->charge_point_id_.c_str(),
             connector_id, transaction_id);
  } else {
    for (JsonObject meter_value : meter_values) {
      const char *timestamp = meter_value["timestamp"] | "";
      JsonArray sampled_values = meter_value["sampledValue"].as<JsonArray>();
      if (sampled_values.isNull()) {
        ESP_LOGD(TAG, "MeterValues: charge_point='%s' connectorId=%d transactionId=%d timestamp='%s'",
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
            if (valid_current_import(parsed_value) && connector != nullptr) {
              bool first_session_current = connector->is_charging && !connector->has_session_current_import;
              connector->latest_current_import = parsed_value;
              connector->has_latest_current_import = true;
              connector->has_session_current_import = connector->is_charging;
              if (connector->is_charging) {
                int current_phase = phase_index(phase);
                if (current_phase >= 0 && current_phase < connector_phases) {
                  if (!connector->has_phase_specific_current_import)
                    connector->latest_drawn_current = {};
                  connector->has_phase_specific_current_import = true;
                  connector->latest_drawn_current[current_phase] = parsed_value;
                } else if (phase[0] == '\0') {
                  connector->has_phase_specific_current_import = false;
                  for (uint8_t i = 0; i < connector->latest_drawn_current.size(); i++)
                    connector->latest_drawn_current[i] = i < connector_phases ? parsed_value : 0.0f;
                } else {
                  ESP_LOGD(TAG, "Ignoring Current.Import phase '%s' for drawn_current sensors", phase);
                }
              }
              if (first_session_current) {
                ESP_LOGI(TAG, "Current.Import available for active session: charge_point='%s' connectorId=%d",
                         this->charge_point_id_.c_str(), connector_id);
              }
              if (!connector->is_charging)
                ESP_LOGD(TAG, "Ignoring Current.Import as active drawn current because connectorId=%d is not charging",
                         connector_id);
            } else if (!valid_current_import(parsed_value)) {
              ESP_LOGW(TAG, "Ignoring invalid Current.Import %.3f A for connectorId=%d", parsed_value, connector_id);
            }
            current_updated = valid_current_import(parsed_value);
          } else if (std::strcmp(measurand, "Power.Active.Import") == 0 &&
                     (unit[0] == '\0' || std::strcmp(unit, "W") == 0)) {
            if (connector != nullptr) {
              connector->latest_power_active_import = parsed_value;
              connector->has_latest_power_active_import = true;
            }
            power_updated = true;
          }
        }
        ESP_LOGD(TAG,
                 "MeterValues: charge_point='%s' connectorId=%d transactionId=%d timestamp='%s' value='%s' "
                 "measurand='%s' unit='%s' phase='%s' context='%s' location='%s'",
                 this->charge_point_id_.c_str(), connector_id, transaction_id, timestamp, value, measurand, unit,
                 phase, context, location);
      }
    }
  }

  if ((current_updated || power_updated) && connector != nullptr) {
    ESP_LOGD(TAG, "Latest meter values: connectorId=%d current=%.1f A power=%.1f W", connector_id,
             connector->latest_current_import, connector->latest_power_active_import);
  }
  if (connector != nullptr && current_updated && connector->current_sensor != nullptr)
    connector->current_sensor->publish_state(connector->latest_current_import);
  if (connector != nullptr && current_updated) {
    this->publish_drawn_current_if_configured_(connector);
    const float previous_allocated_current = connector->allocated_current;
    this->update_connector_allocation_(connector);
    this->publish_connector_allocation_if_configured_(connector);
    if (this->client_ != nullptr && this->handshake_done_ && connector->has_active_transaction &&
        connector->allocated_current != previous_allocated_current) {
      connector->charging_profile_applied = false;
      this->send_preferred_current_limit_if_needed_(connector->id);
    }
  }
  if (connector != nullptr && power_updated && connector->power_sensor != nullptr)
    connector->power_sensor->publish_state(connector->latest_power_active_import);

  std::string response = "[3,\"" + json_escape(unique_id) + "\",{}]";
  this->send_ws_text_(response);
}

void OcppServer::handle_call_result_(const std::string &unique_id, JsonObject payload) {
  const char *status = payload["status"] | "";
  auto *pending = this->find_pending_call_(unique_id);
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
  this->clear_pending_call_(unique_id);
}

void OcppServer::handle_call_error_(const std::string &unique_id, const std::string &error_code,
                                    const std::string &description) {
  auto *pending = this->find_pending_call_(unique_id);
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
  this->clear_pending_call_(unique_id);
}

void OcppServer::send_set_charging_profile_(uint8_t connector_id, uint32_t transaction_id, float current_limit) {
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
  this->send_ocpp_call_("set-charging-profile", "SetChargingProfile", payload, connector_id, transaction_id,
                        current_limit);
}

std::string OcppServer::send_ocpp_call_(const char *unique_prefix, const char *action, const std::string &payload_json,
                                        uint8_t connector_id, uint32_t transaction_id, float current_limit) {
  std::string unique_id = this->next_unique_id_(unique_prefix);
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
