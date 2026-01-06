#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <cstdio>
#include <utility>
#include <thread>
#include <atomic>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace ytrace {

// Info stored for each trace point
struct TracePointInfo {
    bool* enabled;
    const char* file;
    int line;
    const char* function;
};

// Singleton manager for all trace points
class TraceManager {
public:
    static TraceManager& instance() {
        static TraceManager mgr;
        return mgr;
    }

    ~TraceManager() {
        stop_control_thread();
    }

    // Register a trace point - stores pointer to the caller's static bool
    void register_trace_point(bool* enabled, const char* file, int line, const char* function) {
        std::lock_guard<std::mutex> lock(mutex_);
        trace_points_.push_back(TracePointInfo{enabled, file, line, function});
        
        // Start control thread on first registration
        if (!control_thread_started_) {
            control_thread_started_ = true;
            start_control_thread();
        }
    }

    // Enable/disable a specific trace point
    bool set_enabled(const char* file, int line, const char* function, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& info : trace_points_) {
            if (info.line == line &&
                std::string_view(info.file) == std::string_view(file) &&
                std::string_view(info.function) == std::string_view(function)) {
                *info.enabled = state;
                return true;
            }
        }
        return false;
    }

    // Enable/disable by index
    bool set_enabled_by_index(size_t index, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index < trace_points_.size()) {
            *trace_points_[index].enabled = state;
            return true;
        }
        return false;
    }

    // Enable/disable all trace points in a file
    void set_file_enabled(const char* file, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string_view file_view(file);
        for (auto& info : trace_points_) {
            if (std::string_view(info.file) == file_view) {
                *info.enabled = state;
            }
        }
    }

    // Enable/disable all trace points in a function
    void set_function_enabled(const char* function, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string_view func_view(function);
        for (auto& info : trace_points_) {
            if (std::string_view(info.function) == func_view) {
                *info.enabled = state;
            }
        }
    }

    // Enable/disable all trace points
    void set_all_enabled(bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& info : trace_points_) {
            *info.enabled = state;
        }
    }

    // Iterate over all trace points
    template<typename Func>
    void for_each(Func&& func) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& info : trace_points_) {
            func(info);
        }
    }

    // Get list of trace points as string
    std::string list_trace_points() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        size_t idx = 0;
        for (const auto& info : trace_points_) {
            oss << idx++ << " " << (*info.enabled ? "[ON] " : "[OFF]") 
                << " " << info.file << ":" << info.line 
                << " (" << info.function << ")\n";
        }
        return oss.str();
    }

    std::string get_socket_path() const { return socket_path_; }

private:
    TraceManager() : control_thread_started_(false), running_(false), server_fd_(-1) {
        // Generate socket path based on PID
        std::ostringstream oss;
        oss << "/tmp/ytrace." << getpid() << ".sock";
        socket_path_ = oss.str();
    }

    void start_control_thread() {
        running_ = true;
        control_thread_ = std::thread(&TraceManager::control_loop, this);
    }

    void stop_control_thread() {
        running_ = false;
        if (server_fd_ >= 0) {
#ifdef _WIN32
            closesocket(server_fd_);
#else
            close(server_fd_);
#endif
            server_fd_ = -1;
        }
        if (control_thread_.joinable()) {
            control_thread_.join();
        }
        unlink(socket_path_.c_str());
    }

    void control_loop() {
#ifndef _WIN32
        server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::fprintf(stderr, "[ytrace] Failed to create socket\n");
            return;
        }

        // Remove existing socket file
        unlink(socket_path_.c_str());

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::fprintf(stderr, "[ytrace] Failed to bind socket: %s\n", socket_path_.c_str());
            close(server_fd_);
            server_fd_ = -1;
            return;
        }

        if (listen(server_fd_, 5) < 0) {
            std::fprintf(stderr, "[ytrace] Failed to listen on socket\n");
            close(server_fd_);
            server_fd_ = -1;
            return;
        }

        std::fprintf(stderr, "[ytrace] Control socket: %s\n", socket_path_.c_str());

        while (running_) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(server_fd_, &readfds);

            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int ret = select(server_fd_ + 1, &readfds, nullptr, nullptr, &tv);
            if (ret <= 0) continue;

            int client_fd = accept(server_fd_, nullptr, nullptr);
            if (client_fd < 0) continue;

            handle_client(client_fd);
            close(client_fd);
        }
#endif
    }

    void handle_client(int client_fd) {
        // Read full command (may be large for batch operations)
        std::string command;
        char buffer[4096];
        ssize_t n;
        while ((n = read(client_fd, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[n] = '\0';
            command += buffer;
            // Stop at newline (end of command)
            if (command.find('\n') != std::string::npos) break;
        }
        if (command.empty()) return;

        // Trim newline
        if (!command.empty() && command.back() == '\n') command.pop_back();

        std::string response = process_command(command.c_str());
        write(client_fd, response.c_str(), response.size());
    }

    std::string process_command(const char* cmd) {
        std::string command(cmd);
        
        if (command == "list" || command == "l") {
            return list_trace_points();
        }
        else if (command == "enable all" || command == "ea") {
            set_all_enabled(true);
            return "OK: All trace points enabled\n";
        }
        else if (command == "disable all" || command == "da") {
            set_all_enabled(false);
            return "OK: All trace points disabled\n";
        }
        else if (command.rfind("enable ", 0) == 0 || command.rfind("e ", 0) == 0) {
            return process_batch_command(command, true);
        }
        else if (command.rfind("disable ", 0) == 0 || command.rfind("d ", 0) == 0) {
            return process_batch_command(command, false);
        }
        else if (command == "help" || command == "h" || command == "?") {
            return "Commands:\n"
                   "  list (l)           - List all trace points\n"
                   "  enable all (ea)    - Enable all trace points\n"
                   "  disable all (da)   - Disable all trace points\n"
                   "  enable <specs>     - Enable trace points (file:line:func ...)\n"
                   "  disable <specs>    - Disable trace points (file:line:func ...)\n"
                   "  help (h, ?)        - Show this help\n";
        }
        
        return "ERROR: Unknown command. Type 'help' for usage.\n";
    }

    // Process batch enable/disable: "enable file:line:func file:line:func ..."
    std::string process_batch_command(const std::string& command, bool enable) {
        std::istringstream iss(command);
        std::string verb;
        iss >> verb; // skip "enable" or "disable"
        
        int count = 0;
        std::string spec;
        while (iss >> spec) {
            // Parse file:line:function
            size_t pos1 = spec.rfind(':');
            if (pos1 == std::string::npos) continue;
            std::string function = spec.substr(pos1 + 1);
            
            std::string rest = spec.substr(0, pos1);
            size_t pos2 = rest.rfind(':');
            if (pos2 == std::string::npos) continue;
            
            std::string file = rest.substr(0, pos2);
            int line = 0;
            try {
                line = std::stoi(rest.substr(pos2 + 1));
            } catch (...) { continue; }
            
            if (set_enabled(file.c_str(), line, function.c_str(), enable)) {
                ++count;
            }
        }
        
        std::ostringstream oss;
        oss << "OK: " << (enable ? "Enabled" : "Disabled") << " " << count << " trace point(s)\n";
        return oss.str();
    }

    std::mutex mutex_;
    std::vector<TracePointInfo> trace_points_;
    bool control_thread_started_;
    std::atomic<bool> running_;
    std::thread control_thread_;
    int server_fd_;
    std::string socket_path_;
};

namespace detail {
    // Helper to register and return initial value
    inline bool register_trace_point(bool* enabled, const char* file, int line, const char* function) {
        TraceManager::instance().register_trace_point(enabled, file, line, function);
        return false;  // default: disabled
    }
    
    inline bool register_trace_point_enabled(bool* enabled, const char* file, int line, const char* function) {
        TraceManager::instance().register_trace_point(enabled, file, line, function);
        return true;  // enabled by default
    }
}

// Default output handler
inline void default_trace_handler(const char* file, int line, const char* function, const char* msg) {
    std::fprintf(stderr, "[TRACE] %s:%d (%s): %s\n", file, line, function, msg);
}

// Configurable trace output
inline std::function<void(const char*, int, const char*, const char*)>& trace_handler() {
    static std::function<void(const char*, int, const char*, const char*)> handler = default_trace_handler;
    return handler;
}

inline void set_trace_handler(std::function<void(const char*, int, const char*, const char*)> handler) {
    trace_handler() = std::move(handler);
}

namespace detail {
    template<typename... Args>
    void trace_impl(const char* file, int line, const char* function, const char* fmt, Args&&... args) {
        char buffer[1024];
        if constexpr (sizeof...(args) == 0) {
            std::snprintf(buffer, sizeof(buffer), "%s", fmt);
        } else {
            std::snprintf(buffer, sizeof(buffer), fmt, std::forward<Args>(args)...);
        }
        trace_handler()(file, line, function, buffer);
    }
}

} // namespace ytrace

// Main trace macro - static bool registered with manager
#define YTRACE(fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__); \
        if (_ytrace_enabled_) { \
            ytrace::detail::trace_impl(__FILE__, __LINE__, __func__, fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)

// Trace macro with initial enabled state
#define YTRACE_ENABLED(fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point_enabled(&_ytrace_enabled_, __FILE__, __LINE__, __func__); \
        if (_ytrace_enabled_) { \
            ytrace::detail::trace_impl(__FILE__, __LINE__, __func__, fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)

// Just register a trace point without tracing (useful for conditional blocks)
#define YTRACE_POINT() \
    static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__)

#define YTRACE_POINT_ENABLED() \
    static bool _ytrace_enabled_ = ytrace::detail::register_trace_point_enabled(&_ytrace_enabled_, __FILE__, __LINE__, __func__)

// Check if trace point is enabled (must be after YTRACE_POINT)
#define YTRACE_IS_ENABLED() (_ytrace_enabled_)

// Convenience macros for manager access
#define YTRACE_ENABLE_ALL() ytrace::TraceManager::instance().set_all_enabled(true)
#define YTRACE_DISABLE_ALL() ytrace::TraceManager::instance().set_all_enabled(false)
#define YTRACE_ENABLE_FILE(file) ytrace::TraceManager::instance().set_file_enabled(file, true)
#define YTRACE_DISABLE_FILE(file) ytrace::TraceManager::instance().set_file_enabled(file, false)
#define YTRACE_ENABLE_FUNCTION(func) ytrace::TraceManager::instance().set_function_enabled(func, true)
#define YTRACE_DISABLE_FUNCTION(func) ytrace::TraceManager::instance().set_function_enabled(func, false)
