#pragma once

#include "esphome/components/json/json_util.h"

#include <string>

namespace esphome::ocpp {

class OcppProtocolTransport {
  public:
    virtual ~OcppProtocolTransport() = default;

    virtual void send_ocpp_text(const std::string &message) = 0;
};

class OcppProtocol {
  public:
    void set_transport(OcppProtocolTransport *transport) { this->transport_ = transport; }
    void on_connected(const std::string &charge_point_id);
    void on_disconnected();
    void handle_text(const std::string &message);

  protected:
    void handle_boot_notification_(const std::string &unique_id, JsonObject payload);
    void send_text_(const std::string &message);
    void send_ocpp_error_(const std::string &unique_id, const char *code, const char *description);

    OcppProtocolTransport *transport_{nullptr};
    std::string charge_point_id_;
};

}  // namespace esphome::ocpp
