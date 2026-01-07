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
#include <optional>

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
    const char* level;      // "trace", "debug", "info", "warn", "func-entry", "func-exit"
    const char* message;    // format string
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
    void register_trace_point(bool* enabled, const char* file, int line, const char* function,
                              const char* level, const char* message) {
        std::lock_guard<std::mutex> lock(mutex_);
        trace_points_.push_back(TracePointInfo{enabled, file, line, function, level, message});
        
        // Start control thread on first registration
        if (!control_thread_started_) {
            control_thread_started_ = true;
            start_control_thread();
        }
    }

    // Enable/disable a specific trace point (full key match)
    bool set_enabled(const char* file, int line, const char* function,
                     const char* level, const char* message, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& info : trace_points_) {
            if (info.line == line &&
                std::string_view(info.file) == std::string_view(file) &&
                std::string_view(info.function) == std::string_view(function) &&
                std::string_view(info.level) == std::string_view(level) &&
                std::string_view(info.message) == std::string_view(message)) {
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

    // Enable/disable all trace points with a specific level
    void set_level_enabled(const char* level, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string_view level_view(level);
        for (auto& info : trace_points_) {
            if (std::string_view(info.level) == level_view) {
                *info.enabled = state;
            }
        }
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
                << " [" << info.level << "] "
                << info.file << ":" << info.line 
                << " (" << info.function << ") \"" << info.message << "\"\n";
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
                   "  enable <specs>     - Enable trace points (file:line:func:level:msg ...)\n"
                   "  disable <specs>    - Disable trace points (file:line:func:level:msg ...)\n"
                   "  help (h, ?)        - Show this help\n";
        }
        
        return "ERROR: Unknown command. Type 'help' for usage.\n";
    }

    // URL-decode a string (for message field which may contain encoded chars)
    static std::string url_decode(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '%' && i + 2 < str.size()) {
                int val = 0;
                std::istringstream iss(str.substr(i + 1, 2));
                if (iss >> std::hex >> val) {
                    result += static_cast<char>(val);
                    i += 2;
                    continue;
                }
            }
            result += str[i];
        }
        return result;
    }

    // Process batch enable/disable: "enable file:line:func:level:msg file:line:func:level:msg ..."
    std::string process_batch_command(const std::string& command, bool enable) {
        std::istringstream iss(command);
        std::string verb;
        iss >> verb; // skip "enable" or "disable"
        
        int count = 0;
        std::string spec;
        while (iss >> spec) {
            // Parse file:line:function:level:message (message is URL-encoded)
            // Find last 4 colons from right
            size_t pos_msg = spec.rfind(':');
            if (pos_msg == std::string::npos) continue;
            std::string message = url_decode(spec.substr(pos_msg + 1));
            
            std::string rest = spec.substr(0, pos_msg);
            size_t pos_level = rest.rfind(':');
            if (pos_level == std::string::npos) continue;
            std::string level = rest.substr(pos_level + 1);
            
            rest = rest.substr(0, pos_level);
            size_t pos_func = rest.rfind(':');
            if (pos_func == std::string::npos) continue;
            std::string function = rest.substr(pos_func + 1);
            
            rest = rest.substr(0, pos_func);
            size_t pos_line = rest.rfind(':');
            if (pos_line == std::string::npos) continue;
            
            std::string file = rest.substr(0, pos_line);
            int line = 0;
            try {
                line = std::stoi(rest.substr(pos_line + 1));
            } catch (...) { continue; }
            
            if (set_enabled(file.c_str(), line, function.c_str(), level.c_str(), message.c_str(), enable)) {
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
    inline bool register_trace_point(bool* enabled, const char* file, int line, const char* function,
                                     const char* level, const char* message) {
        TraceManager::instance().register_trace_point(enabled, file, line, function, level, message);
        return false;  // default: disabled
    }
    
    inline bool register_trace_point_enabled(bool* enabled, const char* file, int line, const char* function,
                                             const char* level, const char* message) {
        TraceManager::instance().register_trace_point(enabled, file, line, function, level, message);
        return true;  // enabled by default
    }
}

// Default output handler (now includes level)
inline void default_trace_handler(const char* level, const char* file, int line, const char* function, const char* msg) {
    std::fprintf(stderr, "[%s] %s:%d (%s): %s\n", level, file, line, function, msg);
}

// Configurable trace output
inline std::function<void(const char*, const char*, int, const char*, const char*)>& trace_handler() {
    static std::function<void(const char*, const char*, int, const char*, const char*)> handler = default_trace_handler;
    return handler;
}

inline void set_trace_handler(std::function<void(const char*, const char*, int, const char*, const char*)> handler) {
    trace_handler() = std::move(handler);
}

namespace detail {
    template<typename... Args>
    void trace_impl(const char* level, const char* file, int line, const char* function, const char* fmt, Args&&... args) {
        char buffer[1024];
        if constexpr (sizeof...(args) == 0) {
            std::snprintf(buffer, sizeof(buffer), "%s", fmt);
        } else {
            std::snprintf(buffer, sizeof(buffer), fmt, std::forward<Args>(args)...);
        }
        trace_handler()(level, file, line, function, buffer);
    }
}

// RAII scope tracer for function entry/exit
class ScopeTracer {
public:
    ScopeTracer(bool* exit_enabled, const char* file, int line, const char* function)
        : exit_enabled_(exit_enabled), file_(file), line_(line), function_(function) {
        trace_handler()("func-entry", file_, line_, function_, "");
    }
    
    ~ScopeTracer() {
        if (*exit_enabled_) {
            trace_handler()("func-exit", file_, line_, function_, "");
        }
    }
    
private:
    bool* exit_enabled_;
    const char* file_;
    int line_;
    const char* function_;
};

} // namespace ytrace

// Base log macro with level
#define ylog(lvl, fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__, lvl, fmt); \
        if (_ytrace_enabled_) { \
            ytrace::detail::trace_impl(lvl, __FILE__, __LINE__, __func__, fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)

// Level-specific macros
#define ytrace(fmt, ...) ylog("trace", fmt __VA_OPT__(,) __VA_ARGS__)
#define ydebug(fmt, ...) ylog("debug", fmt __VA_OPT__(,) __VA_ARGS__)
#define yinfo(fmt, ...)  ylog("info", fmt __VA_OPT__(,) __VA_ARGS__)
#define ywarn(fmt, ...)  ylog("warn", fmt __VA_OPT__(,) __VA_ARGS__)

// Function entry/exit tracer (RAII)
#define yfunc() \
    static bool _ytrace_entry_enabled_ = ytrace::detail::register_trace_point(&_ytrace_entry_enabled_, __FILE__, __LINE__, __func__, "func-entry", ""); \
    static bool _ytrace_exit_enabled_ = ytrace::detail::register_trace_point(&_ytrace_exit_enabled_, __FILE__, __LINE__, __func__, "func-exit", ""); \
    std::optional<ytrace::ScopeTracer> _ytrace_scope_guard_; \
    if (_ytrace_entry_enabled_) _ytrace_scope_guard_.emplace(&_ytrace_exit_enabled_, __FILE__, __LINE__, __func__)

// Convenience macros for manager access
#define yenable_all()          ytrace::TraceManager::instance().set_all_enabled(true)
#define ydisable_all()         ytrace::TraceManager::instance().set_all_enabled(false)
#define yenable_file(file)     ytrace::TraceManager::instance().set_file_enabled(file, true)
#define ydisable_file(file)    ytrace::TraceManager::instance().set_file_enabled(file, false)
#define yenable_func(func)     ytrace::TraceManager::instance().set_function_enabled(func, true)
#define ydisable_func(func)    ytrace::TraceManager::instance().set_function_enabled(func, false)
#define yenable_level(lvl)     ytrace::TraceManager::instance().set_level_enabled(lvl, true)
#define ydisable_level(lvl)    ytrace::TraceManager::instance().set_level_enabled(lvl, false)
