# ytrace

A header-only C++20 tracing library with runtime control via Unix sockets.

## Intent

ytrace provides lightweight, dynamically controllable trace points for C++ applications. Trace points are disabled by default and can be enabled/disabled at runtime without restarting the application—ideal for debugging production systems.

## Mechanism

1. **Registration**: Each trace macro registers itself with a singleton `TraceManager` on first execution
2. **Control Socket**: The manager spawns a background thread listening on `/tmp/ytrace.<pid>.sock`
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
```

### Filter Flags

| Flag | Description |
|------|-------------|
| `-a, --all` | Match all trace points |
| `-f, --file PATTERN` | Filter by file path (regex) |
| `-F, --function PATTERN` | Filter by function name (regex) |
| `-l, --line LINE` | Filter by line number |
| `-L, --level LEVEL` | Filter by level (regex): trace, debug, info, warn, func-entry, func-exit |
| `-m, --message PATTERN` | Filter by message/format string (regex) |
| `-p, --pid PID` | Target specific process |
| `-s, --socket PATH` | Use socket path directly |

## Building

```bash
make           # Build examples and ytrace-ctl
make clean     # Clean build artifacts
```

## Requirements

- C++20 compiler
- POSIX system (Linux/macOS) for socket control
- [args](https://github.com/Taywee/args) header for ytrace-ctl
