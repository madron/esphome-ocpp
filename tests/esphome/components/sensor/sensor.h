#pragma once

#include <cmath>

namespace esphome::sensor {

class Sensor {
    public:
        void publish_state(float state) { this->state = state; }

        float state{NAN};
};

}  // namespace esphome::sensor
