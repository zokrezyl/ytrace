// Basic usage example for ytrace
#include <ytrace/ytrace.hpp>
#include <iostream>

void process_data(int value) {
    ytimeit("process_data"); // scope timer with explicit label
    yfunc();
    ytrace("processing value: %d", value);
    
    if (value > 10) {
        ydebug("value exceeds threshold");
    }
}

void initialize() {
    ytimeit(); // scope timer using __func__ as label
    yfunc();
    yinfo("initialization started");
    ytrace("loading config");
    ytrace("config loaded successfully");
}

int main() {
    std::cout << "=== Basic ytrace example ===\n\n";

    std::cout << "1. Running with default state (most traces disabled):\n";
    initialize();
    process_data(5);
    process_data(15);

    std::cout << "\n2. Enabling all trace points:\n";
    yenable_all();
    initialize();
    process_data(5);
    process_data(15);

    std::cout << "\n3. Disabling only process_data traces:\n";
    ydisable_func("process_data");
    initialize();
    process_data(20);

    std::cout << "\n4. Listing all registered trace points:\n";
    ytrace::TraceManager::instance().for_each([](const ytrace::TracePointInfo& info) {
        std::printf("  %s:%d [%s] [%s] -> %s\n", 
            info.file, info.line, info.level, info.function,
            *info.enabled ? "ENABLED" : "disabled");
    });

    return 0;
}
