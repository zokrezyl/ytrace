# ytrace

A header-only C++20 tracing library with runtime control via Unix sockets.

## Intent

ytrace provides lightweight, dynamically controllable trace points for C++ applications. Trace points are disabled by default and can be enabled/disabled at runtime without restarting the application—ideal for debugging production systems.

**Zero-overhead tracing:** When disabled, trace points compile to a single boolean check with zero runtime cost. This means you can ship production binaries with comprehensive tracing built-in, enabling detailed diagnostics on-demand without performance penalty.

## Mechanism

1. **Registration**: Each trace macro registers itself with a singleton `TraceManager` on first execution
2. **Control Socket**: The manager spawns a background thread listening on `/tmp/ytrace.<exec>.<pid>.<hash>.sock`
3. **Runtime Control**: External tools (like `ytrace-ctl`) connect to the socket to list/enable/disable trace points
4. **Zero Overhead**: Disabled trace points cost only a boolean check

## Usage

```cpp
#include <ytrace/ytrace.hpp>

void process_data(int value) {
    yfunc();  // traces function entry/exit
    ytrace("processing value: %d", value);
    
    if (value > threshold) {
        ywarn("value exceeds threshold");
    }
    
    yinfo("processing complete");
}
```

## Macros

### Logging Macros

| Macro | Level | Description |
|-------|-------|-------------|
| `ylog(level, fmt, ...)` | custom | Base macro with explicit level |
| `ytrace(fmt, ...)` | trace | Low-level tracing |
| `ydebug(fmt, ...)` | debug | Debug information |
| `yinfo(fmt, ...)` | info | Informational messages |
| `ywarn(fmt, ...)` | warn | Warnings |

### Function Tracing

| Macro | Description |
|-------|-------------|
| `yfunc()` | RAII scope guard - traces function entry and exit |

The `yfunc()` macro registers two trace points (`func-entry` and `func-exit`) that can be controlled independently.

### Scope Timing

| Macro | Description |
|-------|-------------|
| `ytimeit()` | RAII scope timer - measures elapsed time using `__func__` as label |
| `ytimeit("label")` | RAII scope timer with custom label |

The `ytimeit()` macro measures elapsed time for a scope and prints entry/exit messages with adaptive time units (ns/us/ms/s). It also records statistics (count, avg, min, max) that are printed at program exit and can be queried at runtime.

```cpp
void process_request() {
    ytimeit("request");  // or ytimeit() to use function name
    // ... work ...
}  // prints: "request elapsed: 1.5 ms"

// At program exit:
// [ytrace] Timer summary:
//   file.cpp:10 request    count=42  avg=1.5 ms  min=0.8 ms  max=3.2 ms
```

**Programmatic access:**
```cpp
// Get timer summary as string
std::string summary = ytrace::TimerManager::instance().summary();
```

### Programmatic Control

| Macro | Description |
|-------|-------------|
| `yenable_all()` | Enable all trace points |
| `ydisable_all()` | Disable all trace points |
| `yenable_file(file)` | Enable all trace points in a file |
| `ydisable_file(file)` | Disable all trace points in a file |
| `yenable_func(func)` | Enable all trace points in a function |
| `ydisable_func(func)` | Disable all trace points in a function |
| `yenable_level(level)` | Enable all trace points with a level |
| `ydisable_level(level)` | Disable all trace points with a level |

## Custom Handler

```cpp
ytrace::set_trace_handler([](const char* level, const char* file, int line, 
                             const char* func, const char* msg) {
    syslog(LOG_DEBUG, "[%s] %s:%d %s", level, file, line, msg);
});
```

## ytrace-ctl

Command-line tool to control trace points in running processes.

```bash
# List live ytrace processes
ytrace-ctl ps

# List all trace points (auto-discovers single process)
ytrace-ctl list

# List trace points for specific PID
ytrace-ctl -p 12345 list

# Enable/disable with filters
ytrace-ctl enable --all                     # Enable all
ytrace-ctl disable --all                    # Disable all
ytrace-ctl enable --file "math_ops"         # Enable by file regex
ytrace-ctl enable --function "compute_.*"   # Enable by function regex
ytrace-ctl enable --level "info|warn"       # Enable info and warn levels
ytrace-ctl enable --level func-entry        # Enable function entry tracing
ytrace-ctl enable --message "connection"    # Enable by message content
ytrace-ctl disable --file "network" --function "debug_.*"  # Combined filters

# Query timer statistics
ytrace-ctl timers
```

### Filter Flags

| Flag | Description |
|------|-------------|
| `-a, --all` | Match all trace points |
| `-f, --file PATTERN` | Filter by file path (regex) |
| `-F, --function PATTERN` | Filter by function name (regex) |
| `-l, --line LINE` | Filter by line number |
| `-L, --level LEVEL` | Filter by level (regex): trace, debug, info, warn, func-entry, func-exit, timer-entry, timer-exit |
| `-m, --message PATTERN` | Filter by message/format string (regex) |
| `-p, --pid PID` | Target specific process |
| `-s, --socket PATH` | Use socket path directly |

## Building

### Using CMake (Recommended)

```bash
# Configure and build
mkdir build && cd build
cmake ..
cmake --build .
```

**By default, CMake will automatically download and use spdlog.**

To override:
```bash
# Use snprintf backend instead (no external dependencies)
cmake .. -DYTRACE_WITH_SPDLOG=OFF

# Use fmtlib instead (when spdlog disabled)
cmake .. -DYTRACE_WITH_SPDLOG=OFF -DYTRACE_FORMAT=fmtlib

# Disable specific macros at compile-time
cmake .. -DYTRACE_ENABLE_YFUNC=OFF      # Disable yfunc macro
cmake .. -DYTRACE_ENABLE_YDEBUG=OFF     # Disable ydebug macro

# Build with examples and disable tools
cmake .. -DYTRACE_BUILD_EXAMPLES=ON -DYTRACE_BUILD_TOOLS=OFF
```

**CMake Options:**
- `YTRACE_WITH_SPDLOG` (default ON) - Auto-download and use spdlog
- `YTRACE_FORMAT` - Backend when spdlog disabled: `snprintf` (default) or `fmtlib`
- `YTRACE_ENABLE_*` - Compile-time macro switches (all default to ON)
- `YTRACE_BUILD_EXAMPLES` (default ON if top-level) - Build examples
- `YTRACE_BUILD_TOOLS` (default ON if top-level) - Build ytrace-ctl
- `YTRACE_BUILD_TESTS` (default ON if top-level) - Build unit tests

**Compile-time macro switches** (all default to ON):
- `YTRACE_ENABLE_YLOG` - Enable ylog macro
- `YTRACE_ENABLE_YTRACE` - Enable ytrace macro
- `YTRACE_ENABLE_YDEBUG` - Enable ydebug macro
- `YTRACE_ENABLE_YINFO` - Enable yinfo macro
- `YTRACE_ENABLE_YWARN` - Enable ywarn macro
- `YTRACE_ENABLE_YFUNC` - Enable yfunc macro
- `YTRACE_ENABLE_YTIMEIT` - Enable ytimeit macro

When disabled, macros compile to empty `do {} while(0)` with zero overhead.

## Runtime Configuration

### Environment Variables

| Variable | Description |
|----------|-------------|
| `YTRACE_DEFAULT_ON` | Set to `1`, `yes`, or `true` to enable all trace points by default |

By default, trace points are disabled until explicitly enabled via `yenable_*()` macros or `ytrace-ctl`. Set `YTRACE_DEFAULT_ON=1` to start with all trace points enabled.

### Using CPM (C++ Package Manager)

In your `CMakeLists.txt`:

```cmake
include(cmake/CPM.cmake)

CPMAddPackage("gh:user/ytrace@main")

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE ytrace::ytrace)
```

Override options:
```cmake
CPMAddPackage(
    NAME ytrace
    GIT_REPOSITORY "https://github.com/user/ytrace.git"
    GIT_TAG main
    OPTIONS 
        "YTRACE_WITH_SPDLOG OFF"
        "YTRACE_ENABLE_YFUNC OFF"
)
```

### Using Make (Legacy)

```bash
make                           # Build with default snprintf
make YTRACE_FORMAT=fmtlib      # Use fmtlib
make YTRACE_FORMAT=spdlog      # Use spdlog
make clean                     # Clean build artifacts
```

### Formatting Backends

ytrace automatically selects the best available:

- **spdlog** (default) - High-performance async logging with fmt backend (auto-downloaded)
- **fmtlib** - Modern, safe formatting (can be auto-downloaded if spdlog disabled)
- **snprintf** (fallback) - C-style formatting, no external dependencies

## Socket Protocol

For direct socket communication (without `ytrace-ctl`), connect to the Unix socket and send text commands:

| Command | Description |
|---------|-------------|
| `list` or `l` | List all trace points with status |
| `enable all` or `ea` | Enable all trace points |
| `disable all` or `da` | Disable all trace points |
| `enable <specs>` | Enable specific trace points |
| `disable <specs>` | Disable specific trace points |
| `timers` or `t` | Get timer statistics |
| `help` or `h` | Show help |

Example using `socat`:
```bash
echo "list" | socat - UNIX-CONNECT:/tmp/ytrace.myapp.12345.abc123.sock
echo "timers" | socat - UNIX-CONNECT:/tmp/ytrace.myapp.12345.abc123.sock
```

## Requirements

- C++20 compiler
- POSIX system (Linux/macOS) for socket control
- [args](https://github.com/Taywee/args) header for ytrace-ctl
