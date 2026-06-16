#include "charge_point.h"

#include <utility>

namespace esphome::ocpp {

void ChargePoint::set_charge_point_id(std::string charge_point_id) { this->charge_point_id_ = std::move(charge_point_id); }
const std::string &ChargePoint::get_charge_point_id() const { return this->charge_point_id_; }

void ChargePoint::set_debug_ocpp_messages(bool debug_ocpp_messages) { this->debug_ocpp_messages_ = debug_ocpp_messages; }
bool ChargePoint::get_debug_ocpp_messages() const { return this->debug_ocpp_messages_; }

}  // namespace esphome::ocpp
