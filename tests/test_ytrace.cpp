#include <boost/ut.hpp>
#include <ytrace/ytrace.hpp>
#include <string>
#include <vector>

using namespace boost::ut;

suite ytrace_tests = [] {
    "format_duration_ns"_test = [] {
        auto s = ytrace::format_duration(500.0);
        expect(s.find("ns") != std::string::npos) << s;
    };

    "format_duration_us"_test = [] {
        auto s = ytrace::format_duration(5000.0);
        expect(s.find("us") != std::string::npos) << s;
    };

    "format_duration_ms"_test = [] {
        auto s = ytrace::format_duration(5000000.0);
        expect(s.find("ms") != std::string::npos) << s;
    };

    "format_duration_s"_test = [] {
        auto s = ytrace::format_duration(5000000000.0);
        expect(s.find(" s") != std::string::npos) << s;
    };

    "timer_manager_record"_test = [] {
        ytrace::TimerManager::instance().record("test_label", 1000.0);
        auto summary = ytrace::TimerManager::instance().summary();
        expect(summary.find("test_label") != std::string::npos);
    };

    "timer_manager_stats"_test = [] {
        ytrace::TimerManager::instance().record("stats_test", 100.0);
        ytrace::TimerManager::instance().record("stats_test", 200.0);
        ytrace::TimerManager::instance().record("stats_test", 300.0);
        auto summary = ytrace::TimerManager::instance().summary();
        expect(summary.find("stats_test") != std::string::npos);
        expect(summary.find("count=3") != std::string::npos) << summary;
    };

    "trace_manager_singleton"_test = [] {
        auto& mgr1 = ytrace::TraceManager::instance();
        auto& mgr2 = ytrace::TraceManager::instance();
        expect(&mgr1 == &mgr2);
    };

    "trace_handler_default"_test = [] {
        auto& handler = ytrace::trace_handler();
        expect(handler != nullptr);
    };

    "trace_handler_custom"_test = [] {
        std::vector<std::string> captured;
        ytrace::set_trace_handler([&](const char* level, const char* file, int line,
                                       const char* func, const char* msg) {
            captured.push_back(std::string(level) + ":" + msg);
            (void)file; (void)line; (void)func;
        });

        ytrace::trace_handler()("info", "test.cpp", 1, "test_func", "hello");
        expect(captured.size() == 1_u);
        expect(captured[0] == "info:hello");

        ytrace::set_trace_handler(ytrace::default_trace_handler);
    };
};

int main() {
    return 0;
}
