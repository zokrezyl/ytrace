#pragma once

#include <string>

namespace network_sim {

bool connect_to_server(const std::string& host, int port);
bool send_request(const std::string& endpoint, const std::string& payload);
std::string receive_response();
void disconnect();

void simulate_full_session(const std::string& host, int port);

} // namespace network_sim
