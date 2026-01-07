// Complex example for ytrace - demonstrates multiple modules with many trace points
// Use this to test filtering capabilities of ytrace-ctl
//
// Example filtering commands:
//   ytrace-ctl list                        # List all trace points
//   ytrace-ctl enable --all                # Enable all traces
//   ytrace-ctl disable --all               # Disable all traces
//   ytrace-ctl enable -F compute_factorial # Enable traces in specific function
//   ytrace-ctl disable -f data_processor   # Disable traces in specific file
//   ytrace-ctl enable -L func-entry        # Enable all function entry traces
//   ytrace-ctl enable -L "info|warn"       # Enable info and warn levels

#include <ytrace/ytrace.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

#include "math_ops.hpp"
#include "data_processor.hpp"
#include "network_sim.hpp"

void run_math_tests() {
    yfunc();
    yinfo("starting math tests");
    
    math_ops::compute_factorial(5);
    math_ops::compute_fibonacci(10);
    math_ops::compute_prime_check(17);
    math_ops::compute_prime_check(18);
    
    yinfo("math tests complete");
}

void run_data_tests() {
    yfunc();
    yinfo("starting data processing tests");
    
    std::vector<int> data = {64, 34, 25, 12, 22, 11, 90, 5, 77, 30};
    
    auto sorted = data_processor::sort_data(data);
    auto evens = data_processor::filter_even(sorted);
    int sum = data_processor::aggregate_sum(evens);
    std::string str = data_processor::transform_to_string(evens);
    
    yinfo("data tests complete: sum=%d, result=%s", sum, str.c_str());
}

void run_network_tests() {
    yfunc();
    yinfo("starting network simulation tests");
    
    network_sim::simulate_full_session("api.example.com", 443);
    
    yinfo("network tests complete");
}

int main(int argc, char* argv[]) {
    std::cout << "=== Complex ytrace Example ===\n\n";
    std::cout << "This example has many trace points across multiple modules.\n";
    std::cout << "Use ytrace-ctl to filter and control trace output.\n\n";
    
    std::cout << "Socket: " << ytrace::TraceManager::instance().get_socket_path() << "\n\n";
    
    bool loop_mode = (argc > 1 && std::string(argv[1]) == "--loop");
    
    if (loop_mode) {
        std::cout << "Running in loop mode. Press Ctrl+C to stop.\n";
        std::cout << "Try enabling/disabling traces while running:\n";
        std::cout << "  ytrace-ctl enable --all\n";
        std::cout << "  ytrace-ctl disable -f data_processor\n";
        std::cout << "  ytrace-ctl enable -L func-entry\n\n";
    }
    
    do {
        yinfo("=== Starting test cycle ===");
        
        run_math_tests();
        run_data_tests();
        run_network_tests();
        
        yinfo("=== Test cycle complete ===");
        
        if (loop_mode) {
            std::cout << "--- Cycle complete, waiting 2 seconds ---\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    } while (loop_mode);
    
    // List all trace points at the end
    std::cout << "\n=== Registered Trace Points ===\n";
    ytrace::TraceManager::instance().for_each([](const ytrace::TracePointInfo& info) {
        std::printf("  %s:%d [%s] [%s] \"%s\" -> %s\n", 
            info.file, info.line, info.level, info.function, info.message,
            *info.enabled ? "ENABLED" : "disabled");
    });
    
    return 0;
}
