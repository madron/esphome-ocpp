#pragma once

#include "esphome/core/component.h"

#include <string>
#include <utility>
#include <vector>

namespace esphome::ocpp {

class OcppComponent : public Component {
    public:
        void setup() override;
        void loop() override;
        void dump_config() override;
};

}  // namespace esphome::ocpp
