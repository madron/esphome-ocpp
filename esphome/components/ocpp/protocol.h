#pragma once

#include <string>
#include <vector>

namespace esphome::ocpp {

enum class OcppProtocolEventType {
  BOOT_NOTIFICATION_ACCEPTED,
};

struct OcppProtocolEvent {
  OcppProtocolEventType type;
};

struct OcppProtocolResult {
  std::vector<OcppProtocolEvent> events;
  std::vector<std::string> outbound_messages;
};

class OcppProtocol {
  public:
    OcppProtocolResult handle_text(const std::string &charge_point_id, const std::string &message);

  protected:
    void handle_boot_notification_(const std::string &unique_id, OcppProtocolResult *result);
    std::string make_ocpp_error_(const std::string &unique_id, const char *code, const char *description) const;
};

}  // namespace esphome::ocpp
