#include <args/args.hxx>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <regex>
#include <vector>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <dirent.h>

struct TracePoint {
    std::string file;
    int line;
    std::string function;
    std::string level;
    std::string message;
    bool enabled;
};

std::string find_socket_by_pid(int pid) {
    return "/tmp/ytrace." + std::to_string(pid) + ".sock";
}

std::vector<std::string> find_all_sockets() {
    std::vector<std::string> sockets;
    DIR* dir = opendir("/tmp");
    if (!dir) return sockets;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name.rfind("ytrace.", 0) == 0 && name.find(".sock") != std::string::npos) {
            sockets.push_back("/tmp/" + name);
        }
    }
    closedir(dir);
    return sockets;
}

// Extract PID from socket path "/tmp/ytrace.12345.sock"
int extract_pid_from_socket(const std::string& socket_path) {
    size_t start = socket_path.find("ytrace.") + 7;
    size_t end = socket_path.find(".sock");
    if (start == std::string::npos || end == std::string::npos) return -1;
    try {
        return std::stoi(socket_path.substr(start, end - start));
    } catch (...) {
        return -1;
    }
}

// Check if a process is alive
bool is_process_alive(int pid) {
    std::string proc_path = "/proc/" + std::to_string(pid);
    return access(proc_path.c_str(), F_OK) == 0;
}

// Get process command line
std::string get_process_cmdline(int pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream file(path);
    if (!file) return "";
    
    std::string cmdline;
    std::getline(file, cmdline, '\0');
    return cmdline;
}

// Find live ytrace processes
struct YtraceProcess {
    int pid;
    std::string socket_path;
    std::string cmdline;
};

std::vector<YtraceProcess> find_live_processes() {
    std::vector<YtraceProcess> result;
    auto sockets = find_all_sockets();
    
    for (const auto& sock : sockets) {
        int pid = extract_pid_from_socket(sock);
        if (pid > 0 && is_process_alive(pid)) {
            YtraceProcess proc;
            proc.pid = pid;
            proc.socket_path = sock;
            proc.cmdline = get_process_cmdline(pid);
            result.push_back(proc);
        }
    }
    return result;
}

std::string send_command(const std::string& socket_path, const std::string& command) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return "ERROR: Failed to create socket";
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return "ERROR: Failed to connect to " + socket_path;
    }

    // Send command
    std::string cmd_with_newline = command + "\n";
    ssize_t n = write(fd, cmd_with_newline.c_str(), cmd_with_newline.size());
    (void)n;  // suppress unused result warning

    // Read response
    std::string response;
    char buffer[4096];
    while ((n = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';
        response += buffer;
    }

    close(fd);
    return response;
}

// Parse list response into TracePoint structs
// Format: "0 [ON]  [level] /path/file.cpp:123 (function_name) "message""
std::vector<TracePoint> parse_trace_points(const std::string& response) {
    std::vector<TracePoint> points;
    std::istringstream iss(response);
    std::string line;
    
    // Regex to parse: "0 [ON]  [level] /path/file.cpp:123 (function_name) "message""
    std::regex line_re(R"regex(^\d+\s+\[(ON|OFF)\]\s+\[([^\]]+)\]\s+(.+):(\d+)\s+\(([^)]+)\)\s+"([^"]*)")regex");
    
    while (std::getline(iss, line)) {
        std::smatch match;
        if (std::regex_search(line, match, line_re)) {
            TracePoint tp;
            tp.enabled = (match[1] == "ON");
            tp.level = match[2];
            tp.file = match[3];
            tp.line = std::stoi(match[4]);
            tp.function = match[5];
            tp.message = match[6];
            points.push_back(tp);
        }
    }
    return points;
}

// Filter trace points based on --all, --file, --function, --line, --level, --message flags
std::vector<TracePoint> filter_trace_points(
    const std::vector<TracePoint>& points,
    bool all_flag,
    const std::vector<std::string>& file_patterns,
    const std::vector<std::string>& function_patterns,
    const std::vector<int>& lines,
    const std::vector<std::string>& level_patterns,
    const std::vector<std::string>& message_patterns)
{
    // No filter specified = no matches (safe default)
    if (!all_flag && file_patterns.empty() && function_patterns.empty() && 
        lines.empty() && level_patterns.empty() && message_patterns.empty()) {
        return {};
    }
    
    // --all returns everything
    if (all_flag) {
        return points;
    }
    
    std::vector<TracePoint> result;
    
    // Compile regexes for file and function patterns
    std::vector<std::regex> file_regexes;
    for (const auto& pat : file_patterns) {
        try {
            file_regexes.emplace_back(pat);
        } catch (const std::regex_error&) {
            std::cerr << "Warning: Invalid regex for --file: " << pat << "\n";
        }
    }
    
    std::vector<std::regex> func_regexes;
    for (const auto& pat : function_patterns) {
        try {
            func_regexes.emplace_back(pat);
        } catch (const std::regex_error&) {
            std::cerr << "Warning: Invalid regex for --function: " << pat << "\n";
        }
    }
    
    std::vector<std::regex> level_regexes;
    for (const auto& pat : level_patterns) {
        try {
            level_regexes.emplace_back(pat);
        } catch (const std::regex_error&) {
            std::cerr << "Warning: Invalid regex for --level: " << pat << "\n";
        }
    }
    
    std::vector<std::regex> msg_regexes;
    for (const auto& pat : message_patterns) {
        try {
            msg_regexes.emplace_back(pat);
        } catch (const std::regex_error&) {
            std::cerr << "Warning: Invalid regex for --message: " << pat << "\n";
        }
    }
    
    for (const auto& tp : points) {
        bool match = false;
        
        // Check file patterns (OR)
        for (const auto& re : file_regexes) {
            if (std::regex_search(tp.file, re)) {
                match = true;
                break;
            }
        }
        
        // Check function patterns (OR)
        if (!match) {
            for (const auto& re : func_regexes) {
                if (std::regex_search(tp.function, re)) {
                    match = true;
                    break;
                }
            }
        }
        
        // Check line numbers (OR)
        if (!match) {
            for (int l : lines) {
                if (tp.line == l) {
                    match = true;
                    break;
                }
            }
        }
        
        // Check level patterns (OR)
        if (!match) {
            for (const auto& re : level_regexes) {
                if (std::regex_search(tp.level, re)) {
                    match = true;
                    break;
                }
            }
        }
        
        // Check message patterns (OR)
        if (!match) {
            for (const auto& re : msg_regexes) {
                if (std::regex_search(tp.message, re)) {
                    match = true;
                    break;
                }
            }
        }
        
        if (match) {
            result.push_back(tp);
        }
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    args::ArgumentParser parser("ytrace-ctl - Control ytrace trace points at runtime");
    parser.Prog("ytrace-ctl");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"}, args::Options::Global);
    
    args::ValueFlag<int> pid_flag(parser, "PID", "Target process PID", {'p', "pid"}, args::Options::Global);
    args::ValueFlag<std::string> socket_flag(parser, "SOCKET", "Socket path directly", {'s', "socket"}, args::Options::Global);
    
    // Filter flags (global, work with enable/disable/list)
    args::Flag all_flag(parser, "all", "Match all trace points", {'a', "all"}, args::Options::Global);
    args::ValueFlagList<std::string> file_flag(parser, "PATTERN", "Filter by file (regex)", {'f', "file"}, {}, args::Options::Global);
    args::ValueFlagList<std::string> func_flag(parser, "PATTERN", "Filter by function (regex)", {'F', "function"}, {}, args::Options::Global);
    args::ValueFlagList<int> line_flag(parser, "LINE", "Filter by line number", {'l', "line"}, {}, args::Options::Global);
    args::ValueFlagList<std::string> level_flag(parser, "LEVEL", "Filter by level (regex)", {'L', "level"}, {}, args::Options::Global);
    args::ValueFlagList<std::string> msg_flag(parser, "PATTERN", "Filter by message (regex)", {'m', "message"}, {}, args::Options::Global);
    
    args::Group commands(parser, "Commands:");
    args::Command list_cmd(commands, "list", "List trace points (with optional filters)");
    args::Command enable_cmd(commands, "enable", "Enable trace points matching filters");
    args::Command disable_cmd(commands, "disable", "Disable trace points matching filters");
    args::Command ps_cmd(commands, "ps", "List live ytrace processes");
    args::Command discover_cmd(commands, "discover", "Discover ytrace sockets (including stale)");
    
    parser.RequireCommand(false);

    try {
        parser.ParseCLI(argc, argv);
    } catch (const args::Help&) {
        std::cout << parser;
        return 0;
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    // Discover command (shows all sockets including stale)
    if (discover_cmd) {
        auto sockets = find_all_sockets();
        if (sockets.empty()) {
            std::cout << "No ytrace sockets found.\n";
        } else {
            std::cout << "Found ytrace sockets:\n";
            for (const auto& s : sockets) {
                std::cout << "  " << s << "\n";
            }
        }
        return 0;
    }

    // ps command - list live processes only
    if (ps_cmd) {
        auto procs = find_live_processes();
        if (procs.empty()) {
            std::cout << "No live ytrace processes found.\n";
        } else {
            std::cout << "PID\tCOMMAND\n";
            for (const auto& p : procs) {
                std::cout << p.pid << "\t" << p.cmdline << "\n";
            }
        }
        return 0;
    }

    // No command specified - show help
    if (!list_cmd && !enable_cmd && !disable_cmd) {
        std::cout << parser;
        return 0;
    }

    // Determine socket path
    std::string socket_path;
    if (socket_flag) {
        socket_path = args::get(socket_flag);
    } else if (pid_flag) {
        socket_path = find_socket_by_pid(args::get(pid_flag));
    } else {
        auto sockets = find_all_sockets();
        if (sockets.empty()) {
            std::cerr << "No ytrace processes found. Specify --pid or --socket.\n";
            return 1;
        } else if (sockets.size() > 1) {
            std::cerr << "Multiple ytrace processes found. Specify --pid or --socket:\n";
            for (const auto& s : sockets) {
                std::cerr << "  " << s << "\n";
            }
            return 1;
        }
        socket_path = sockets[0];
    }

    // Get filter parameters
    bool use_all = all_flag;
    std::vector<std::string> file_patterns = args::get(file_flag);
    std::vector<std::string> func_patterns = args::get(func_flag);
    std::vector<int> line_nums = args::get(line_flag);
    std::vector<std::string> level_patterns = args::get(level_flag);
    std::vector<std::string> msg_patterns = args::get(msg_flag);

    // List command - fetch and optionally filter
    if (list_cmd) {
        std::string response = send_command(socket_path, "list");
        if (response.rfind("ERROR", 0) == 0) {
            std::cerr << response;
            return 1;
        }
        
        // If no filters, show all
        if (!use_all && file_patterns.empty() && func_patterns.empty() && 
            line_nums.empty() && level_patterns.empty() && msg_patterns.empty()) {
            std::cout << response;
            return 0;
        }
        
        // Apply filters
        auto points = parse_trace_points(response);
        auto filtered = filter_trace_points(points, use_all, file_patterns, func_patterns, 
                                            line_nums, level_patterns, msg_patterns);
        
        for (const auto& tp : filtered) {
            std::cout << (tp.enabled ? "[ON] " : "[OFF]") << " [" << tp.level << "] "
                      << tp.file << ":" << tp.line << " (" << tp.function << ") \"" << tp.message << "\"\n";
        }
        return 0;
    }

    // Enable/Disable commands - fetch list, filter, send batch
    if (enable_cmd || disable_cmd) {
        // Must have at least one filter
        if (!use_all && file_patterns.empty() && func_patterns.empty() && 
            line_nums.empty() && level_patterns.empty() && msg_patterns.empty()) {
            std::cerr << "Error: No filter specified. Use --all, --file, --function, --line, --level, or --message.\n";
            return 1;
        }
        
        // Fetch current trace points
        std::string response = send_command(socket_path, "list");
        if (response.rfind("ERROR", 0) == 0) {
            std::cerr << response;
            return 1;
        }
        
        auto points = parse_trace_points(response);
        auto filtered = filter_trace_points(points, use_all, file_patterns, func_patterns, 
                                            line_nums, level_patterns, msg_patterns);
        
        if (filtered.empty()) {
            std::cout << "No trace points matched the filter.\n";
            return 0;
        }
        
        // Build batch command: "enable file:line:function:level:message ..."
        // URL-encode message to handle spaces and special chars
        auto url_encode = [](const std::string& str) {
            std::ostringstream oss;
            for (char c : str) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    oss << c;
                } else {
                    oss << '%' << std::uppercase << std::hex << ((int)(unsigned char)c);
                }
            }
            return oss.str();
        };
        
        std::string cmd = enable_cmd ? "enable" : "disable";
        for (const auto& tp : filtered) {
            cmd += " " + tp.file + ":" + std::to_string(tp.line) + ":" + tp.function 
                 + ":" + tp.level + ":" + url_encode(tp.message);
        }
        
        response = send_command(socket_path, cmd);
        std::cout << response;
        
        return response.rfind("ERROR", 0) == 0 ? 1 : 0;
    }

    return 0;
}
