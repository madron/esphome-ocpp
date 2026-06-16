#pragma once

#include <string>

namespace esphome::ocpp {

class ChargePoint {
  public:
    void set_charge_point_id(std::string charge_point_id);
    const std::string &get_charge_point_id() const;

  protected:
    std::string charge_point_id_;
};

}  // namespace esphome::ocpp