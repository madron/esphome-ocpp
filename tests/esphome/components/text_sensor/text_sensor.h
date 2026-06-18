#pragma once

#include <string>

namespace esphome::text_sensor {

class TextSensor {
 public:
  void publish_state(const std::string &state) { this->state = state; }

  std::string state;
};

}  // namespace esphome::text_sensor