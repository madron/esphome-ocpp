#pragma once

#include "protocol.h"

#if __has_include("esphome/components/binary_sensor/binary_sensor.h")
#include "esphome/components/binary_sensor/binary_sensor.h"
#else
namespace esphome::binary_sensor {
class BinarySensor {
 public:
  void publish_state(bool state) { this->state = state; }
  bool state{false};
};
}  // namespace esphome::binary_sensor
#endif

#include <string>
#include <cstdint>

namespace esphome::ocpp {

class OcppMessageSink {
  public:
    virtual ~OcppMessageSink() = default;
    virtual void send_ocpp_text(const std::string &connection_id, const std::string &message) = 0;
};

class ChargePoint {
  public:
    void set_charge_point_id(std::string charge_point_id);
    const std::string &get_charge_point_id() const;
    void set_connection_id(std::string connection_id);
    const std::string &get_connection_id() const;
    void set_message_sink(OcppMessageSink *message_sink) { this->message_sink_ = message_sink; }
    void set_online_binary_sensor(binary_sensor::BinarySensor *online_binary_sensor) {
      this->online_binary_sensor_ = online_binary_sensor;
    }
    void set_debug_ocpp_messages(bool debug_ocpp_messages);
    bool get_debug_ocpp_messages() const;
    void set_force_boot_notification(bool force_boot_notification);
    bool get_force_boot_notification() const;
    bool is_online() const;

    void on_connected(std::string connection_id, uint32_t now_millis = 0);
    void on_disconnected();
    void handle_ocpp_text(const std::string &message);
    void loop(uint32_t now_millis);

  protected:
    void send_message_(const std::string &message);
    void apply_protocol_result_(const OcppProtocolResult &result);
    void send_forced_boot_notification_trigger_();
    void set_online_(bool online);

    std::string charge_point_id_;
    std::string connection_id_;
    OcppProtocol protocol_;
    OcppMessageSink *message_sink_{nullptr};
    binary_sensor::BinarySensor *online_binary_sensor_{nullptr};
    bool debug_ocpp_messages_{false};
    bool force_boot_notification_{false};
    bool force_boot_notification_pending_{true};
    bool force_boot_notification_scheduled_{false};
    bool connected_{false};
    bool online_{false};
    uint32_t connected_at_millis_{0};
};

}  // namespace esphome::ocpp
