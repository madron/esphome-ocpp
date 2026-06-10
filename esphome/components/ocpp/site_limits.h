#pragma once

#include <cstdint>
#include <optional>

namespace esphome::ocpp {

struct SiteLimitConfig {
  uint8_t phases{1};
  float voltage{230.0f};
  std::optional<float> storage_capacity_kwh{};
};

inline uint8_t site_active_phases(const SiteLimitConfig &config) { return config.phases == 3 ? 3 : 1; }

}  // namespace esphome::ocpp
