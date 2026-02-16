#pragma once

// Master switch to completely disable ytrace (default: enabled)
// Set YTRACE_ENABLED=0 to disable all tracing (useful for Emscripten/WASM builds)
#ifndef YTRACE_ENABLED
#define YTRACE_ENABLED 1
#endif

#if YTRACE_ENABLED

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
#include <chrono>
#include <unordered_map>
#include <cinttypes>

// Formatting backend selection (compile-time flag)
// - YTRACE_USE_SPDLOG: Use spdlog for logging (requires spdlog)
// - default: snprintf (C-style, no external dependencies)

#if defined(YTRACE_USE_SPDLOG)
    #include <spdlog/spdlog.h>
    #include <spdlog/sinks/stdout_color_sinks.h>
#endif

// Auto-detect Emscripten and disable control socket
#if defined(__EMSCRIPTEN__)
    #ifndef YTRACE_NO_CONTROL_SOCKET
    #define YTRACE_NO_CONTROL_SOCKET 1
    #endif
#endif

// Control socket and config persistence (disable for Emscripten/WASM)
#if !defined(YTRACE_NO_CONTROL_SOCKET)
    #ifdef _WIN32
    #include <winsock2.h>
    #include <afunix.h>
    #include <process.h>
    #include <io.h>
    #pragma comment(lib, "ws2_32.lib")
    #define getpid _getpid
    #define unlink _unlink
    #else
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <unistd.h>
    #endif
    #include <fstream>
    #include <filesystem>
#endif

namespace ytrace {

// Forward declaration
struct TracePointInfo;

#if !defined(YTRACE_NO_CONTROL_SOCKET)
// Config persistence utility (requires filesystem and socket APIs)
class ConfigPersistence {
public:
    // Stored config entry (loaded from file, used to restore state on registration)
    struct ConfigEntry {
        bool enabled;
        std::string file;
        int line;
        std::string function;
        std::string level;
        std::string message;
    };

    static std::string compute_path_hash(const std::string& path) {
        // Simple hash: sum bytes and convert to base36-like (digits + lowercase)
        unsigned long hash = 5381;
        for (char c : path) {
            hash = ((hash << 5) + hash) ^ (unsigned char)c;
        }

        // Convert to base36 (0-9, a-z) - 20 chars
        std::string result;
        for (int i = 0; i < 20; ++i) {
            int digit = hash % 36;
            hash /= 36;
            if (digit < 10)
                result += char('0' + digit);
            else
                result += char('a' + digit - 10);
        }
        return result;
    }

    static std::pair<std::string, std::string> get_exec_name_and_path() {
#ifndef _WIN32
        char path[4096] = {0};
        ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len > 0) {
            path[len] = '\0';
            std::string exec_path(path);
            // Extract just the basename for exec_name
            size_t pos = exec_path.find_last_of('/');
            std::string exec_name = (pos != std::string::npos) ?
                exec_path.substr(pos + 1) : exec_path;
            return {exec_name, exec_path};
        }
#endif
        return {"ytrace", ""};  // Fallback if readlink fails
    }

    static std::string get_config_file(const std::string& exec_name, const std::string& exec_path) {
#ifndef _WIN32
        const char* home = std::getenv("HOME");
        if (!home) home = "/tmp";

        std::string cache_dir = std::string(home) + "/.cache/ytrace";
        std::filesystem::create_directories(cache_dir);

        // Strip "ytrace_" prefix from config filename if present
        std::string config_name = exec_name;
        if (config_name.substr(0, 7) == "ytrace_") {
            config_name = config_name.substr(7);
        }

        std::string path_hash = compute_path_hash(exec_path);
        return cache_dir + "/" + config_name + "-" + path_hash + ".config";
#else
        return "";  // Not supported on Windows
#endif
    }

    static void save_state(const std::string& config_file, const std::vector<TracePointInfo>& points);

    // Load config entries from file (call once at startup)
    static std::vector<ConfigEntry> load_config_entries(const std::string& config_file);

    // Apply saved state to a trace point (call on each registration)
    static bool apply_saved_state(const std::vector<ConfigEntry>& entries, TracePointInfo& point);
};
#endif // !YTRACE_NO_CONTROL_SOCKET

// Info stored for each trace point
struct TracePointInfo {
    bool* enabled;
    const char* file;
    int line;
    const char* function;
    const char* level;      // "trace", "debug", "info", "warn", "func-entry", "func-exit"
    const char* message;    // format string
};

// Adaptive time unit formatting
inline std::string format_duration(double ns) {
    char buf[64];
    if (ns < 1000.0) {
        std::snprintf(buf, sizeof(buf), "%.1f ns", ns);
    } else if (ns < 1000000.0) {
        std::snprintf(buf, sizeof(buf), "%.1f us", ns / 1000.0);
    } else if (ns < 1000000000.0) {
        std::snprintf(buf, sizeof(buf), "%.1f ms", ns / 1000000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.3f s", ns / 1000000000.0);
    }
    return buf;
}

// Per-label timer statistics
struct TimerStats {
    uint64_t count = 0;
    double avg = 0.0;
    double min = 0.0;
    double max = 0.0;
};

// Singleton manager that collects timer statistics and prints summary on exit
class TimerManager {
public:
    static TimerManager& instance() {
        static TimerManager mgr;
        return mgr;
    }

    void record(const std::string& label, double duration_ns) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& s = stats_[label];
        s.count++;
        if (s.count == 1) {
            s.avg = duration_ns;
            s.min = duration_ns;
            s.max = duration_ns;
        } else {
            s.avg += (duration_ns - s.avg) / static_cast<double>(s.count);
            if (duration_ns < s.min) s.min = duration_ns;
            if (duration_ns > s.max) s.max = duration_ns;
        }
    }

    std::string summary() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stats_.empty()) return "";
        std::ostringstream oss;
        for (const auto& [label, s] : stats_) {
            char line[256];
            std::snprintf(line, sizeof(line), "  %-40s  count=%" PRIu64 "  avg=%s  min=%s  max=%s\n",
                label.c_str(), s.count,
                format_duration(s.avg).c_str(),
                format_duration(s.min).c_str(),
                format_duration(s.max).c_str());
            oss << line;
        }
        return oss.str();
    }

    ~TimerManager() {
        auto s = summary();
        if (!s.empty()) {
            std::fprintf(stderr, "\n[ytrace] Timer summary:\n%s", s.c_str());
        }
    }

private:
    TimerManager() = default;
    std::mutex mutex_;
    std::unordered_map<std::string, TimerStats> stats_;
};

#if defined(YTRACE_NO_CONTROL_SOCKET)
// Simplified TraceManager for Emscripten/WASM (no control socket, no config persistence)
class TraceManager {
public:
    static TraceManager& instance() {
        static TraceManager mgr;
        return mgr;
    }

    ~TraceManager() = default;

    void register_trace_point(bool* enabled, const char* file, int line, const char* function,
                              const char* level, const char* message) {
        std::lock_guard<std::mutex> lock(mutex_);
        trace_points_.push_back(TracePointInfo{enabled, file, line, function, level, message});
    }

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

    bool set_enabled_by_index(size_t index, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index < trace_points_.size()) {
            *trace_points_[index].enabled = state;
            return true;
        }
        return false;
    }

    void set_level_enabled(const char* level, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string_view level_view(level);
        for (auto& info : trace_points_) {
            if (std::string_view(info.level) == level_view) {
                *info.enabled = state;
            }
        }
    }

    void set_file_enabled(const char* file, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string_view file_view(file);
        for (auto& info : trace_points_) {
            if (std::string_view(info.file) == file_view) {
                *info.enabled = state;
            }
        }
    }

    void set_function_enabled(const char* function, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string_view func_view(function);
        for (auto& info : trace_points_) {
            if (std::string_view(info.function) == func_view) {
                *info.enabled = state;
            }
        }
    }

    void set_all_enabled(bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& info : trace_points_) {
            *info.enabled = state;
        }
    }

    size_t count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return trace_points_.size();
    }

    void for_each(std::function<void(const TracePointInfo&)> func) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& info : trace_points_) {
            func(info);
        }
    }

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

    std::string get_socket_path() const { return ""; }

private:
    TraceManager() = default;
    std::mutex mutex_;
    std::vector<TracePointInfo> trace_points_;
};

#else // Full TraceManager with control socket

// Singleton manager for all trace points
class TraceManager {
public:
    static TraceManager& instance() {
        static TraceManager mgr;
        return mgr;
    }

    ~TraceManager() {
        stop_control_thread();
        // NOTE: Do NOT save_config() here! Static bools in trace points are already
        // destroyed at this point, so reading *info.enabled would give garbage (0).
        // Config is saved immediately on each change, so this is not needed.
    }

    // Register a trace point - stores pointer to the caller's static bool
    void register_trace_point(bool* enabled, const char* file, int line, const char* function,
                              const char* level, const char* message) {
        std::lock_guard<std::mutex> lock(mutex_);
        trace_points_.push_back(TracePointInfo{enabled, file, line, function, level, message});

        // Apply saved state to this newly registered trace point
        ConfigPersistence::apply_saved_state(saved_config_, trace_points_.back());

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
                save_config();
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
            save_config();
            return true;
        }
        return false;
    }

    // Enable/disable all trace points with a specific level
    void set_level_enabled(const char* level, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string_view level_view(level);
        bool changed = false;
        for (auto& info : trace_points_) {
            if (std::string_view(info.level) == level_view) {
                *info.enabled = state;
                changed = true;
            }
        }
        if (changed) save_config();
    }

    // Enable/disable all trace points in a file
    void set_file_enabled(const char* file, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string_view file_view(file);
        bool changed = false;
        for (auto& info : trace_points_) {
            if (std::string_view(info.file) == file_view) {
                *info.enabled = state;
                changed = true;
            }
        }
        if (changed) save_config();
    }

    // Enable/disable all trace points in a function
    void set_function_enabled(const char* function, bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string_view func_view(function);
        bool changed = false;
        for (auto& info : trace_points_) {
            if (std::string_view(info.function) == func_view) {
                *info.enabled = state;
                changed = true;
            }
        }
        if (changed) save_config();
    }

    // Enable/disable all trace points
    void set_all_enabled(bool state) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool changed = false;
        for (auto& info : trace_points_) {
            if (*info.enabled != state) {
                *info.enabled = state;
                changed = true;
            }
        }
        if (changed) save_config();
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
        // Auto-detect executable name and path from /proc/self/exe
        auto [exec_name, exec_path] = ConfigPersistence::get_exec_name_and_path();
        exec_name_ = exec_name;
        exec_path_ = exec_path;

        // Init config file path and load saved config entries
        config_file_ = ConfigPersistence::get_config_file(exec_name_, exec_path_);
        saved_config_ = ConfigPersistence::load_config_entries(config_file_);

        // Generate socket path with actual exec info
        generate_socket_path();
    }
    
    void generate_socket_path() {
        std::ostringstream oss;
#ifdef _WIN32
        const char* temp = std::getenv("TEMP");
        if (!temp) temp = std::getenv("TMP");
        if (!temp) temp = "C:\\Windows\\Temp";
        oss << temp << "\\ytrace." << exec_name_ << "." << getpid();
#else
        oss << "/tmp/ytrace." << exec_name_ << "." << getpid();
#endif
        if (!exec_path_.empty()) {
            std::string path_hash = ConfigPersistence::compute_path_hash(exec_path_);
            oss << "." << path_hash;
        }
        oss << ".sock";
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
        if (!socket_path_.empty()) {
            unlink(socket_path_.c_str());
        }
    }

    void control_loop() {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::fprintf(stderr, "[ytrace] WSAStartup failed\n");
            return;
        }
#endif
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
#ifdef _WIN32
            closesocket(server_fd_);
#else
            close(server_fd_);
#endif
            server_fd_ = -1;
            return;
        }

        if (listen(server_fd_, 5) < 0) {
            std::fprintf(stderr, "[ytrace] Failed to listen on socket\n");
#ifdef _WIN32
            closesocket(server_fd_);
#else
            close(server_fd_);
#endif
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

            int client_fd = static_cast<int>(accept(server_fd_, nullptr, nullptr));
            if (client_fd < 0) continue;

            handle_client(client_fd);
#ifdef _WIN32
            closesocket(client_fd);
#else
            close(client_fd);
#endif
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void handle_client(int client_fd) {
        // Read full command (may be large for batch operations)
        std::string command;
        char buffer[4096];
        int n;
#ifdef _WIN32
        while ((n = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
#else
        while ((n = static_cast<int>(read(client_fd, buffer, sizeof(buffer) - 1))) > 0) {
#endif
            buffer[n] = '\0';
            command += buffer;
            // Stop at newline (end of command)
            if (command.find('\n') != std::string::npos) break;
        }
        if (command.empty()) return;

        // Trim newline
        if (!command.empty() && command.back() == '\n') command.pop_back();

        std::string response = process_command(command.c_str());
#ifdef _WIN32
        send(client_fd, response.c_str(), static_cast<int>(response.size()), 0);
#else
        n = static_cast<int>(write(client_fd, response.c_str(), response.size()));
        (void)n;
#endif
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
        else if (command == "timers" || command == "t") {
            auto s = TimerManager::instance().summary();
            if (s.empty()) return "No timer data recorded.\n";
            return "Timer summary:\n" + s;
        }
        else if (command == "help" || command == "h" || command == "?") {
            return "Commands:\n"
                   "  list (l)           - List all trace points\n"
                   "  enable all (ea)    - Enable all trace points\n"
                   "  disable all (da)   - Disable all trace points\n"
                   "  enable <specs>     - Enable trace points (file:line:func:level:msg ...)\n"
                   "  disable <specs>    - Disable trace points (file:line:func:level:msg ...)\n"
                   "  timers (t)         - Show timer statistics\n"
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
    std::vector<ConfigPersistence::ConfigEntry> saved_config_;  // Loaded at startup
    bool control_thread_started_;
    std::atomic<bool> running_;
    std::thread control_thread_;
    int server_fd_;
    std::string socket_path_;
    std::string config_file_;
    std::string exec_name_;
    std::string exec_path_;

    void save_config() {
#ifndef _WIN32
        if (!config_file_.empty()) {
            ConfigPersistence::save_state(config_file_, trace_points_);
        }
#endif
    }

public:
};
#endif // !YTRACE_NO_CONTROL_SOCKET

namespace detail {
    // Check YTRACE_DEFAULT_ON env var: if not set or not "1"/"yes", default is off
    inline bool get_default_enabled() {
        static const bool default_on = []() {
            const char* val = std::getenv("YTRACE_DEFAULT_ON");
            if (!val) return false;
            return std::string_view(val) == "1" || std::string_view(val) == "yes" || std::string_view(val) == "true";
        }();
        return default_on;
    }

    // Helper to register and return initial value (from saved config or default)
    inline bool register_trace_point(bool* enabled, const char* file, int line, const char* function,
                                     const char* level, const char* message) {
        *enabled = get_default_enabled();
        TraceManager::instance().register_trace_point(enabled, file, line, function, level, message);
        return *enabled;  // return the value (possibly modified by saved config)
    }

    inline bool register_trace_point_enabled(bool* enabled, const char* file, int line, const char* function,
                                             const char* level, const char* message) {
        *enabled = get_default_enabled();
        TraceManager::instance().register_trace_point(enabled, file, line, function, level, message);
        return *enabled;  // return the value (possibly modified by saved config)
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
#if defined(YTRACE_USE_SPDLOG)
    inline spdlog::level::level_enum to_spdlog_level(const char* level) {
        std::string_view lv(level);
        if (lv == "trace") return spdlog::level::trace;
        else if (lv == "debug" || lv == "func-entry" || lv == "func-exit") return spdlog::level::debug;
        else if (lv == "info") return spdlog::level::info;
        else if (lv == "warn") return spdlog::level::warn;
        return spdlog::level::debug;
    }
#endif
    
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

// RAII scope timer for measuring elapsed time
class ScopeTimer {
public:
    ScopeTimer(const char* label, const char* file, int line, const char* function)
        : label_(label), file_(file), line_(line), function_(function),
          start_(std::chrono::steady_clock::now()) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s started", label_);
        trace_handler()("timer-entry", file_, line_, function_, buf);
    }

    ~ScopeTimer() {
        auto end = std::chrono::steady_clock::now();
        double elapsed_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count());
        std::string dur = format_duration(elapsed_ns);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s elapsed: %s", label_, dur.c_str());
        trace_handler()("timer-exit", file_, line_, function_, buf);

        // Build key: file:line:function or just label
        std::string key = std::string(file_) + ":" + std::to_string(line_) + " " + label_;
        TimerManager::instance().record(key, elapsed_ns);
    }

private:
    const char* label_;
    const char* file_;
    int line_;
    const char* function_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace ytrace

// ConfigPersistence implementation (after TracePointInfo is defined)
#if !defined(YTRACE_NO_CONTROL_SOCKET)
namespace ytrace {
    inline void ConfigPersistence::save_state(const std::string& config_file, const std::vector<TracePointInfo>& points) {
#ifndef _WIN32
        std::ofstream file(config_file);
        if (!file) return;
        
        for (const auto& info : points) {
            bool enabled = *info.enabled;
            file << (enabled ? "1" : "0") << " "
                 << info.file << " "
                 << info.line << " "
                 << info.function << " "
                 << info.level << " "
                 << info.message << "\n";
        }
#endif
    }
    
    inline std::vector<ConfigPersistence::ConfigEntry> ConfigPersistence::load_config_entries(const std::string& config_file) {
        std::vector<ConfigEntry> entries;
#ifndef _WIN32
        std::ifstream file(config_file);
        if (!file) return entries;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;

            // Parse: "0/1 file line function level message"
            std::istringstream iss(line);
            int enabled_int = 0;
            int line_num = 0;
            std::string file_path, func, level, msg;

            if (iss >> enabled_int >> file_path >> line_num >> func >> level) {
                // Read rest of line as message
                std::string rest;
                if (std::getline(iss, rest)) {
                    msg = rest;
                    // Trim leading space
                    if (!msg.empty() && msg[0] == ' ') {
                        msg = msg.substr(1);
                    }
                }

                entries.push_back(ConfigEntry{
                    enabled_int != 0,
                    file_path,
                    line_num,
                    func,
                    level,
                    msg
                });
            }
        }
#endif
        return entries;
    }

    inline bool ConfigPersistence::apply_saved_state(const std::vector<ConfigEntry>& entries, TracePointInfo& point) {
        for (const auto& entry : entries) {
            if (entry.file == point.file &&
                entry.line == point.line &&
                entry.function == point.function &&
                entry.level == point.level &&
                entry.message == point.message) {
                *point.enabled = entry.enabled;
                return true;
            }
        }
        return false;
    }
}
#endif // !YTRACE_NO_CONTROL_SOCKET

#endif // YTRACE_ENABLED

// Compile-time switches for each macro (default: enabled, but only if YTRACE_ENABLED)
#if YTRACE_ENABLED
#ifndef YTRACE_ENABLE_YLOG
#define YTRACE_ENABLE_YLOG 1
#endif

#ifndef YTRACE_ENABLE_YTRACE
#define YTRACE_ENABLE_YTRACE 1
#endif

#ifndef YTRACE_ENABLE_YDEBUG
#define YTRACE_ENABLE_YDEBUG 1
#endif

#ifndef YTRACE_ENABLE_YINFO
#define YTRACE_ENABLE_YINFO 1
#endif

#ifndef YTRACE_ENABLE_YWARN
#define YTRACE_ENABLE_YWARN 1
#endif

#ifndef YTRACE_ENABLE_YFUNC
#define YTRACE_ENABLE_YFUNC 1
#endif

#ifndef YTRACE_ENABLE_YTIMEIT
#define YTRACE_ENABLE_YTIMEIT 1
#endif
#else
// YTRACE_ENABLED=0: disable all macros
#define YTRACE_ENABLE_YLOG 0
#define YTRACE_ENABLE_YTRACE 0
#define YTRACE_ENABLE_YDEBUG 0
#define YTRACE_ENABLE_YINFO 0
#define YTRACE_ENABLE_YWARN 0
#define YTRACE_ENABLE_YFUNC 0
#define YTRACE_ENABLE_YTIMEIT 0
#endif

// Macros with compile-time format strings for spdlog
#if YTRACE_ENABLE_YLOG
#if defined(YTRACE_USE_SPDLOG)
#define ylog(lvl, fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__, lvl, fmt); \
        if (_ytrace_enabled_) { \
            spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, ytrace::detail::to_spdlog_level(lvl), fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)
#else
#define ylog(lvl, fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__, lvl, fmt); \
        if (_ytrace_enabled_) { \
            ytrace::detail::trace_impl(lvl, __FILE__, __LINE__, __func__, fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)
#endif
#else
#define ylog(lvl, fmt, ...) do {} while(0)
#endif

// Level-specific macros with compile-time format strings
#if YTRACE_ENABLE_YTRACE
#if defined(YTRACE_USE_SPDLOG)
#define ytrace(fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "trace", fmt); \
        if (_ytrace_enabled_) { \
            spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::trace, fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)
#else
#define ytrace(fmt, ...) ylog("trace", fmt __VA_OPT__(,) __VA_ARGS__)
#endif
#else
#define ytrace(fmt, ...) do {} while(0)
#endif

#if YTRACE_ENABLE_YDEBUG
#if defined(YTRACE_USE_SPDLOG)
#define ydebug(fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "debug", fmt); \
        if (_ytrace_enabled_) { \
            spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::debug, fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)
#else
#define ydebug(fmt, ...) ylog("debug", fmt __VA_OPT__(,) __VA_ARGS__)
#endif
#else
#define ydebug(fmt, ...) do {} while(0)
#endif

#if YTRACE_ENABLE_YINFO
#if defined(YTRACE_USE_SPDLOG)
#define yinfo(fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "info", fmt); \
        if (_ytrace_enabled_) { \
            spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::info, fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)
#else
#define yinfo(fmt, ...) ylog("info", fmt __VA_OPT__(,) __VA_ARGS__)
#endif
#else
#define yinfo(fmt, ...) do {} while(0)
#endif

#if YTRACE_ENABLE_YWARN
#if defined(YTRACE_USE_SPDLOG)
#define ywarn(fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "warn", fmt); \
        if (_ytrace_enabled_) { \
            spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::warn, fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)
#else
#define ywarn(fmt, ...) ylog("warn", fmt __VA_OPT__(,) __VA_ARGS__)
#endif
#else
#define ywarn(fmt, ...) do {} while(0)
#endif

// Error level tracing
#if YTRACE_ENABLE_YERROR
#if defined(YTRACE_USE_SPDLOG)
#define yerror(fmt, ...) \
    do { \
        static bool _ytrace_enabled_ = ytrace::detail::register_trace_point(&_ytrace_enabled_, __FILE__, __LINE__, __func__, "error", fmt); \
        if (_ytrace_enabled_) { \
            spdlog::log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::err, fmt __VA_OPT__(,) __VA_ARGS__); \
        } \
    } while(0)
#else
#define yerror(fmt, ...) ylog("error", fmt __VA_OPT__(,) __VA_ARGS__)
#endif
#else
#define yerror(fmt, ...) do {} while(0)
#endif
#if YTRACE_ENABLE_YFUNC
#define yfunc() \
    static bool _ytrace_entry_enabled_ = ytrace::detail::register_trace_point(&_ytrace_entry_enabled_, __FILE__, __LINE__, __func__, "func-entry", ""); \
    static bool _ytrace_exit_enabled_ = ytrace::detail::register_trace_point(&_ytrace_exit_enabled_, __FILE__, __LINE__, __func__, "func-exit", ""); \
    std::optional<ytrace::ScopeTracer> _ytrace_scope_guard_; \
    if (_ytrace_entry_enabled_) _ytrace_scope_guard_.emplace(&_ytrace_exit_enabled_, __FILE__, __LINE__, __func__)
#else
#define yfunc() do {} while(0)
#endif

// ytime() - scope timer macro (optional label argument)
#if YTRACE_ENABLE_YTIMEIT
#define YTIMEIT_IMPL(label) \
    static bool _ytrace_timer_entry_enabled_ = ytrace::detail::register_trace_point(&_ytrace_timer_entry_enabled_, __FILE__, __LINE__, __func__, "timer-entry", label); \
    static bool _ytrace_timer_exit_enabled_ = ytrace::detail::register_trace_point(&_ytrace_timer_exit_enabled_, __FILE__, __LINE__, __func__, "timer-exit", label); \
    std::optional<ytrace::ScopeTimer> _ytrace_timer_guard_; \
    if (_ytrace_timer_entry_enabled_) _ytrace_timer_guard_.emplace(label, __FILE__, __LINE__, __func__)

// Dispatch: ytime() uses __func__, ytime("label") uses the given label
#define YTIMEIT_NOLABEL() YTIMEIT_IMPL(__func__)
#define YTIMEIT_GET_MACRO(_0, _1, NAME, ...) NAME
#define ytimeit(...) YTIMEIT_GET_MACRO(_0 __VA_OPT__(,) __VA_ARGS__, YTIMEIT_IMPL, YTIMEIT_NOLABEL)(__VA_ARGS__)
#else
#define ytimeit(...) do {} while(0)
#endif

// Convenience macros for manager access
#if YTRACE_ENABLED
#define yenable_all()          ytrace::TraceManager::instance().set_all_enabled(true)
#define ydisable_all()         ytrace::TraceManager::instance().set_all_enabled(false)
#define yenable_file(file)     ytrace::TraceManager::instance().set_file_enabled(file, true)
#define ydisable_file(file)    ytrace::TraceManager::instance().set_file_enabled(file, false)
#define yenable_func(func)     ytrace::TraceManager::instance().set_function_enabled(func, true)
#define ydisable_func(func)    ytrace::TraceManager::instance().set_function_enabled(func, false)
#define yenable_level(lvl)     ytrace::TraceManager::instance().set_level_enabled(lvl, true)
#define ydisable_level(lvl)    ytrace::TraceManager::instance().set_level_enabled(lvl, false)
#else
#define yenable_all()          do {} while(0)
#define ydisable_all()         do {} while(0)
#define yenable_file(file)     do {} while(0)
#define ydisable_file(file)    do {} while(0)
#define yenable_func(func)     do {} while(0)
#define ydisable_func(func)    do {} while(0)
#define yenable_level(lvl)     do {} while(0)
#define ydisable_level(lvl)    do {} while(0)
#endif
