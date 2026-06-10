#pragma once

// OcppComponent protocol member declarations. This file is included from
// OcppComponent's protected section in ocpp.h so these remain class members.

void handle_ws_text_(const std::string &message);
void handle_boot_notification_(const std::string &unique_id, JsonObject payload);
void send_ws_text_(const std::string &message);
void send_ocpp_error_(const std::string &unique_id, const char *code, const char *description);

std::unique_ptr<socket::Socket> client_;
std::string rx_buffer_;
std::string charge_point_id_;
bool handshake_done_{false};
