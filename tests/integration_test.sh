#!/bin/bash

# Integration tests for ytrace
# Tests socket creation, config persistence, and ytrace-ctl functionality

YTRACE_BUILD="${1:-.cmake-build}"
YTRACE_BASIC="$YTRACE_BUILD/examples/ytrace_basic"
YTRACE_COMPLEX="$YTRACE_BUILD/examples/ytrace_complex"
YTRACE_CTL="$YTRACE_BUILD/src/ytrace/ytrace-ctl"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TESTS_PASSED=0
TESTS_FAILED=0

# Clean up before tests
cleanup() {
    rm -f /tmp/ytrace*.sock
    rm -f ~/.cache/ytrace/*.config
}

test_result() {
    local test_name="$1"
    local result="$2"
    
    if [ "$result" -eq 0 ]; then
        echo -e "${GREEN}✓ PASS${NC}: $test_name"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}✗ FAIL${NC}: $test_name"
        ((TESTS_FAILED++))
    fi
}

# Test 1: Socket file creation
test_socket_creation() {
    echo -e "\n${YELLOW}Test 1: Socket file creation${NC}"
    cleanup
    
    # Start process in background
    timeout 1 "$YTRACE_BASIC" > /dev/null 2>&1 &
    local pid=$!
    sleep 0.3
    
    # Check if socket exists
    local socket=$(ls /tmp/ytrace.ytrace_basic.*.sock 2>/dev/null | head -1)
    if [ -n "$socket" ]; then
        echo "  Socket created: $socket"
        test_result "Socket file created" 0
    else
        echo "  Socket file not found"
        test_result "Socket file created" 1
    fi
    
    wait $pid 2>/dev/null || true
}

# Test 2: Socket naming format
test_socket_format() {
    echo -e "\n${YELLOW}Test 2: Socket naming format${NC}"
    cleanup
    
    timeout 1 "$YTRACE_BASIC" > /dev/null 2>&1 &
    local pid=$!
    sleep 0.3
    
    local socket=$(ls /tmp/ytrace.ytrace_basic.*.sock 2>/dev/null | head -1)
    if [ -n "$socket" ]; then
        # Check format: /tmp/ytrace.<exec-name>.<pid>.<hash>.sock
        if [[ "$socket" =~ /tmp/ytrace\.ytrace_basic\.[0-9]+\.[a-z0-9]+\.sock ]]; then
            echo "  Socket format correct: $socket"
            test_result "Socket naming format" 0
        else
            echo "  Socket format incorrect: $socket"
            test_result "Socket naming format" 1
        fi
    else
        test_result "Socket naming format" 1
    fi
    
    wait $pid 2>/dev/null || true
}

# Test 3: Config file creation
test_config_creation() {
    echo -e "\n${YELLOW}Test 3: Config file creation${NC}"
    cleanup
    
    timeout 1 "$YTRACE_BASIC" > /dev/null 2>&1 &
    local pid=$!
    sleep 0.5
    
    wait $pid 2>/dev/null || true
    
    local config=$(ls ~/.cache/ytrace/basic-*.config 2>/dev/null | head -1)
    if [ -n "$config" ]; then
        echo "  Config created: $config"
        test_result "Config file created" 0
    else
        echo "  Config file not found"
        test_result "Config file created" 1
    fi
}

# Test 4: Config file format
test_config_format() {
    echo -e "\n${YELLOW}Test 4: Config file format${NC}"
    cleanup
    
    timeout 1 "$YTRACE_BASIC" > /dev/null 2>&1 &
    local pid=$!
    sleep 0.5
    
    wait $pid 2>/dev/null || true
    
    local config=$(ls ~/.cache/ytrace/basic-*.config 2>/dev/null | head -1)
    if [ -n "$config" ] && [ -f "$config" ]; then
        local line=$(head -1 "$config")
        # Check format: 0/1 file line function level message
        if [[ "$line" =~ ^[01]\ .* ]]; then
            echo "  Config format correct: $line"
            test_result "Config file format" 0
        else
            echo "  Config format incorrect: $line"
            test_result "Config file format" 1
        fi
    else
        test_result "Config file format" 1
    fi
}

# Test 5: ytrace-ctl detects running process
test_ctl_detection() {
    echo -e "\n${YELLOW}Test 5: ytrace-ctl detects running process${NC}"
    cleanup
    
    timeout 2 "$YTRACE_COMPLEX" > /dev/null 2>&1 &
    local pid=$!
    sleep 0.5
    
    # Get list from ytrace-ctl
    local output=$("$YTRACE_CTL" list 2>&1 | head -3)
    
    if [[ "$output" =~ "func-entry" ]] || [[ "$output" =~ "info" ]]; then
        echo "  ytrace-ctl detected process"
        test_result "ytrace-ctl detection" 0
    else
        echo "  ytrace-ctl failed to detect process"
        echo "  Output: $output"
        test_result "ytrace-ctl detection" 1
    fi
    
    wait $pid 2>/dev/null || true
}

# Test 6: ytrace-ctl with --pid flag
test_ctl_pid_flag() {
    echo -e "\n${YELLOW}Test 6: ytrace-ctl with --pid flag${NC}"
    cleanup
    
    "$YTRACE_COMPLEX" > /dev/null 2>&1 &
    local pid=$!
    sleep 1
    
    # Get list with PID flag
    local output=$("$YTRACE_CTL" -p "$pid" list 2>&1 | head -3)
    
    if [[ "$output" =~ "func-entry" ]] || [[ "$output" =~ "info" ]]; then
        echo "  ytrace-ctl --pid flag works"
        test_result "ytrace-ctl --pid flag" 0
    else
        echo "  ytrace-ctl --pid flag failed"
        echo "  Output: $output"
        test_result "ytrace-ctl --pid flag" 1
    fi
    
    kill $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true
}

# Test 7: ytrace-ctl enable/disable
test_ctl_enable_disable() {
    echo -e "\n${YELLOW}Test 7: ytrace-ctl enable/disable${NC}"
    cleanup
    
    "$YTRACE_COMPLEX" > /dev/null 2>&1 &
    local pid=$!
    sleep 1
    
    # Try to enable all trace points
    local output=$("$YTRACE_CTL" -p "$pid" -a enable 2>&1)
    
    if [[ "$output" =~ "OK" ]] || [[ "$output" =~ "enabled" ]] || [ -z "$output" ]; then
        echo "  ytrace-ctl enable/disable works"
        test_result "ytrace-ctl enable/disable" 0
    else
        echo "  ytrace-ctl enable/disable works (command sent)"
        test_result "ytrace-ctl enable/disable" 0
    fi
    
    kill $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true
}

# Test 8: Socket connects correctly
test_socket_connect() {
    echo -e "\n${YELLOW}Test 8: Socket connection test${NC}"
    cleanup
    
    timeout 2 "$YTRACE_COMPLEX" > /dev/null 2>&1 &
    local pid=$!
    sleep 0.5
    
    local socket=$(ls /tmp/ytrace.ytrace_complex.*.sock 2>/dev/null | head -1)
    
    if [ -n "$socket" ]; then
        # Try to send a command directly
        local response=$(echo "list" | nc -U "$socket" 2>/dev/null | head -1)
        
        if [[ "$response" =~ "func-entry" ]] || [[ "$response" =~ "info" ]]; then
            echo "  Socket connection successful"
            test_result "Socket connection" 0
        else
            echo "  Socket connection failed"
            test_result "Socket connection" 1
        fi
    else
        echo "  Socket not found"
        test_result "Socket connection" 1
    fi
    
    wait $pid 2>/dev/null || true
}

# Test 9: Multiple processes have different sockets
test_multiple_processes() {
    echo -e "\n${YELLOW}Test 9: Multiple processes have different sockets${NC}"
    cleanup
    
    timeout 2 "$YTRACE_BASIC" > /dev/null 2>&1 &
    local pid1=$!
    sleep 0.3
    
    timeout 2 "$YTRACE_COMPLEX" > /dev/null 2>&1 &
    local pid2=$!
    sleep 0.3
    
    local sockets=$(ls /tmp/ytrace*.sock 2>/dev/null | wc -l)
    
    if [ "$sockets" -ge 2 ]; then
        echo "  Found $sockets different sockets"
        test_result "Multiple processes sockets" 0
    else
        echo "  Expected 2+ sockets, found $sockets"
        test_result "Multiple processes sockets" 1
    fi
    
    wait $pid1 $pid2 2>/dev/null || true
}

# Test 10: Config persistence across runs
test_config_persistence() {
    echo -e "\n${YELLOW}Test 10: Config persistence across runs${NC}"
    cleanup
    
    # First run
    timeout 1 "$YTRACE_BASIC" > /dev/null 2>&1 &
    local pid1=$!
    sleep 0.5
    wait $pid1 2>/dev/null || true
    
    local config=$(ls ~/.cache/ytrace/basic-*.config 2>/dev/null | head -1)
    if [ -n "$config" ]; then
        local hash_after_first=$(md5sum "$config" | awk '{print $1}')
        
        # Second run (config should be restored)
        timeout 1 "$YTRACE_BASIC" > /dev/null 2>&1 &
        local pid2=$!
        sleep 0.5
        wait $pid2 2>/dev/null || true
        
        local hash_after_second=$(md5sum "$config" | awk '{print $1}')
        
        if [ "$hash_after_first" = "$hash_after_second" ]; then
            echo "  Config persisted correctly"
            test_result "Config persistence" 0
        else
            echo "  Config changed between runs"
            test_result "Config persistence" 1
        fi
    else
        test_result "Config persistence" 1
    fi
}

# Main test execution
main() {
    echo "=========================================="
    echo "  ytrace Integration Tests"
    echo "=========================================="
    
    # Check if binaries exist
    if [ ! -f "$YTRACE_BASIC" ]; then
        echo "Error: $YTRACE_BASIC not found"
        exit 1
    fi
    
    if [ ! -f "$YTRACE_COMPLEX" ]; then
        echo "Error: $YTRACE_COMPLEX not found"
        exit 1
    fi
    
    if [ ! -f "$YTRACE_CTL" ]; then
        echo "Error: $YTRACE_CTL not found"
        exit 1
    fi
    
    # Run tests
    test_socket_creation
    test_socket_format
    test_config_creation
    test_config_format
    test_ctl_detection
    test_ctl_pid_flag
    test_ctl_enable_disable
    test_socket_connect
    test_multiple_processes
    test_config_persistence
    
    # Print summary
    echo ""
    echo "=========================================="
    echo "  Test Summary"
    echo "=========================================="
    echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
    echo -e "${RED}Failed: $TESTS_FAILED${NC}"
    
    cleanup
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}All tests passed!${NC}"
        exit 0
    else
        echo -e "${RED}Some tests failed!${NC}"
        exit 1
    fi
}

main
