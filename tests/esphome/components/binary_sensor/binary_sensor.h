#pragma once

namespace esphome::binary_sensor {

class BinarySensor {
 public:
    void publish_state(bool state) {
        this->state = state;
        this->has_state = true;
    }

    void publish_initial_state(bool state) { this->publish_state(state); }

    bool state{false};
    bool has_state{false};
};

}  // namespace esphome::binary_sensor
