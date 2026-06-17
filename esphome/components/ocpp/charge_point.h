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
    bool is_online() const;

    void on_connected(std::string connection_id);
    void on_disconnected();
    void handle_ocpp_text(const std::string &message);

  protected:
    void apply_protocol_result_(const OcppProtocolResult &result);
    void set_online_(bool online);

    std::string charge_point_id_;
    std::string connection_id_;
    OcppProtocol protocol_;
    OcppMessageSink *message_sink_{nullptr};
    binary_sensor::BinarySensor *online_binary_sensor_{nullptr};
    bool debug_ocpp_messages_{false};
    bool online_{false};
};

}  // namespace esphome::ocpp
