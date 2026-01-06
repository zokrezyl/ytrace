// Advanced example: custom trace handler and fine-grained control
#include <ytrace/ytrace.hpp>
#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>

// Store traces in memory for later analysis
std::vector<std::string> trace_log;

void memory_trace_handler(const char* file, int line, const char* function, const char* msg) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    
    std::ostringstream oss;
    oss << "[" << ms << "ms] " << file << ":" << line << " (" << function << "): " << msg;
    trace_log.push_back(oss.str());
}

namespace network {
    void connect(const char* host) {
        YTRACE("connecting to %s", host);
        YTRACE("connection established");
    }
    
    void send_data(const char* data) {
        YTRACE("sending: %s", data);
    }
    
    void disconnect() {
        YTRACE("disconnecting");
    }
}

namespace database {
    void query(const char* sql) {
        YTRACE("executing query: %s", sql);
    }
    
    void commit() {
        YTRACE("committing transaction");
    }
}

void application_logic() {
    YTRACE("starting application logic");
    network::connect("api.example.com");
    database::query("SELECT * FROM users");
    network::send_data("{\"action\": \"fetch\"}");
    database::commit();
    network::disconnect();
    YTRACE("application logic complete");
}

int main() {
    std::cout << "=== Custom handler example ===\n\n";

    // Install custom handler that stores traces in memory
    ytrace::set_trace_handler(memory_trace_handler);
    
    // First run to register all trace points (they start disabled)
    application_logic();
    
    // Now enable all traces
    YTRACE_ENABLE_ALL();
    
    std::cout << "1. Running application with memory trace handler:\n";
    application_logic();
    
    std::cout << "\n2. Captured traces:\n";
    for (const auto& entry : trace_log) {
        std::cout << "  " << entry << "\n";
    }
    
    // Clear log and selectively enable only database traces
    trace_log.clear();
    YTRACE_DISABLE_ALL();
    YTRACE_ENABLE_FUNCTION("query");
    YTRACE_ENABLE_FUNCTION("commit");
    
    std::cout << "\n3. Running with only database traces enabled:\n";
    application_logic();
    
    std::cout << "\n4. Captured traces (database only):\n";
    for (const auto& entry : trace_log) {
        std::cout << "  " << entry << "\n";
    }

    // Demonstrate conditional tracing with YTRACE_POINT
    std::cout << "\n5. Using YTRACE_POINT for conditional blocks:\n";
    YTRACE_ENABLE_ALL();
    
    {
        YTRACE_POINT();  // register trace point
        if (YTRACE_IS_ENABLED()) {
            std::cout << "  Trace point is enabled - doing expensive debug work\n";
        }
    }

    return 0;
}
