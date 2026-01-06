// Basic usage example for ytrace
#include <ytrace/ytrace.hpp>
#include <iostream>

void process_data(int value) {
    YTRACE("processing value: %d", value);
    
    if (value > 10) {
        YTRACE("value exceeds threshold");
    }
}

void initialize() {
    YTRACE_ENABLED("initialization started");  // enabled by default
    YTRACE("loading config");
    YTRACE("config loaded successfully");
}

int main() {
    std::cout << "=== Basic ytrace example ===\n\n";

    std::cout << "1. Running with default state (most traces disabled):\n";
    initialize();
    process_data(5);
    process_data(15);

    std::cout << "\n2. Enabling all trace points:\n";
    YTRACE_ENABLE_ALL();
    initialize();
    process_data(5);
    process_data(15);

    std::cout << "\n3. Disabling only process_data traces:\n";
    YTRACE_DISABLE_FUNCTION("process_data");
    initialize();
    process_data(20);

    std::cout << "\n4. Listing all registered trace points:\n";
    ytrace::TraceManager::instance().for_each([](const ytrace::TracePointInfo& info) {
        std::printf("  %s:%d [%s] -> %s\n", 
            info.file, info.line, info.function,
            *info.enabled ? "ENABLED" : "disabled");
    });

    return 0;
}
