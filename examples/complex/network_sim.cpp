#include "network_sim.hpp"
#include <ytrace/ytrace.hpp>
#include <thread>
#include <chrono>

namespace network_sim {

static bool connected_ = false;
static std::string current_host_;
static int current_port_ = 0;

bool connect_to_server(const std::string& host, int port) {
    yfunc();
    yinfo("attempting connection to %s:%d", host.c_str(), port);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    
    ytrace("resolving DNS for %s", host.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    ytrace("establishing TCP connection");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    connected_ = true;
    current_host_ = host;
    current_port_ = port;
    
    yinfo("connection established to %s:%d", host.c_str(), port);
    return true;
}

bool send_request(const std::string& endpoint, const std::string& payload) {
    yfunc();
    yinfo("sending request to endpoint: %s", endpoint.c_str());
    
    if (!connected_) {
        ywarn("error: not connected to server");
        return false;
    }
    
    ytrace("serializing payload (%zu bytes)", payload.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    ytrace("writing to socket");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    ydebug("request sent successfully");
    return true;
}

std::string receive_response() {
    yfunc();
    ytrace("waiting for response");
    
    if (!connected_) {
        ywarn("error: not connected");
        return "";
    }
    
    ytrace("reading from socket");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    
    std::string response = "{\"status\":\"ok\",\"data\":[1,2,3]}";
    ydebug("received response: %zu bytes", response.size());
    
    ytrace("deserializing response");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    return response;
}

void disconnect() {
    yfunc();
    yinfo("disconnecting from %s:%d", current_host_.c_str(), current_port_);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    ytrace("closing socket");
    connected_ = false;
    current_host_.clear();
    current_port_ = 0;
    
    ydebug("disconnected");
}

void simulate_full_session(const std::string& host, int port) {
    yfunc();
    yinfo("starting full network session simulation");
    
    if (connect_to_server(host, port)) {
        send_request("/api/data", "{\"query\":\"test\"}");
        std::string resp = receive_response();
        ydebug("session response: %s", resp.c_str());
        
        send_request("/api/status", "{}");
        resp = receive_response();
        ydebug("status response: %s", resp.c_str());
        
        disconnect();
    }
    
    yinfo("full session complete");
}

} // namespace network_sim
