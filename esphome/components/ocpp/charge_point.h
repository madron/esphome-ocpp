#pragma once

#include <string>

namespace esphome::ocpp {

class ChargePoint {
  public:
    void set_charge_point_id(std::string charge_point_id);
    const std::string &get_charge_point_id() const;
    void set_debug_ocpp_messages(bool debug_ocpp_messages);
    bool get_debug_ocpp_messages() const;

  protected:
    std::string charge_point_id_;
    bool debug_ocpp_messages_{false};
};

}  // namespace esphome::ocpp
