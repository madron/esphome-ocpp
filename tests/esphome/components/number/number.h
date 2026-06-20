#pragma once

#include <cmath>

namespace esphome::number {

class Number {
 public:
    virtual ~Number() = default;
    void publish_state(float state) { this->state = state; }

    float state{NAN};

 protected:
    virtual void control(float value) = 0;
};

}  // namespace esphome::number