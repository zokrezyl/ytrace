// Advanced example: custom trace handler and fine-grained control
#include <ytrace/ytrace.hpp>
#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>

// Store traces in memory for later analysis
std::vector<std::string> trace_log;

void memory_trace_handler(const char* level, const char* file, int line, const char* function, const char* msg) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    
    std::ostringstream oss;
    oss << "[" << ms << "ms] [" << level << "] " << file << ":" << line << " (" << function << "): " << msg;
    trace_log.push_back(oss.str());
}

namespace network {
    void connect(const char* host) {
        yfunc();
        ytrace("connecting to %s", host);
        yinfo("connection established");
    }
    
    void send_data(const char* data) {
        ytrace("sending: %s", data);
    }
    
    void disconnect() {
        ydebug("disconnecting");
    }
}

namespace database {
    void query(const char* sql) {
        ytrace("executing query: %s", sql);
    }
    
    void commit() {
        yinfo("committing transaction");
    }
}

void application_logic() {
    yfunc();
    ytrace("starting application logic");
    network::connect("api.example.com");
    database::query("SELECT * FROM users");
    network::send_data("{\"action\": \"fetch\"}");
    database::commit();
    network::disconnect();
    ytrace("application logic complete");
}

int main() {
    std::cout << "=== Custom handler example ===\n\n";

    // Install custom handler that stores traces in memory
    ytrace::set_trace_handler(memory_trace_handler);
    
    // First run to register all trace points (they start disabled)
    application_logic();
    
    // Now enable all traces
    yenable_all();
    
    std::cout << "1. Running application with memory trace handler:\n";
    application_logic();
    
    std::cout << "\n2. Captured traces:\n";
    for (const auto& entry : trace_log) {
        std::cout << "  " << entry << "\n";
    }
    
    // Clear log and selectively enable only database traces
    trace_log.clear();
    ydisable_all();
    yenable_func("query");
    yenable_func("commit");
    
    std::cout << "\n3. Running with only database traces enabled:\n";
    application_logic();
    
    std::cout << "\n4. Captured traces (database only):\n";
    for (const auto& entry : trace_log) {
        std::cout << "  " << entry << "\n";
    }

    // Demonstrate level filtering
    std::cout << "\n5. Enable only 'info' level traces:\n";
    trace_log.clear();
    ydisable_all();
    yenable_level("info");
    application_logic();
    
    std::cout << "\n6. Captured traces (info level only):\n";
    for (const auto& entry : trace_log) {
        std::cout << "  " << entry << "\n";
    }

    return 0;
}
