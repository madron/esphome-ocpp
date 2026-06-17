#pragma once

namespace esphome::binary_sensor {

class BinarySensor {
 public:
  void publish_state(bool state) { this->state = state; }

  bool state{false};
};

}  // namespace esphome::binary_sensor
