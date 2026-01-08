CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -I include

# Formatting backend selection (optional)
# YTRACE_FORMAT can be: snprintf (default), fmtlib, or spdlog
YTRACE_FORMAT ?= snprintf

ifeq ($(YTRACE_FORMAT),fmtlib)
    CXXFLAGS += -DYTRACE_USE_FMTLIB
else ifeq ($(YTRACE_FORMAT),spdlog)
    CXXFLAGS += -DYTRACE_USE_SPDLOG
endif

BUILD_DIR := build
EXAMPLES_DIR := examples
SRC_DIR := src

EXAMPLES := basic custom_handler socket_control
EXAMPLE_TARGETS := $(addprefix $(BUILD_DIR)/,$(EXAMPLES))

# Complex example sources
COMPLEX_SRCS := $(EXAMPLES_DIR)/complex/main.cpp \
                $(EXAMPLES_DIR)/complex/math_ops.cpp \
                $(EXAMPLES_DIR)/complex/data_processor.cpp \
                $(EXAMPLES_DIR)/complex/network_sim.cpp

.PHONY: all clean examples ytrace-ctl

all: examples ytrace-ctl

examples: $(EXAMPLE_TARGETS) $(BUILD_DIR)/complex

$(BUILD_DIR)/%: $(EXAMPLES_DIR)/%.cpp include/ytrace/ytrace.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILD_DIR)/complex: $(COMPLEX_SRCS) include/ytrace/ytrace.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I $(EXAMPLES_DIR)/complex -o $@ $(COMPLEX_SRCS)

ytrace-ctl: $(BUILD_DIR)/ytrace-ctl

$(BUILD_DIR)/ytrace-ctl: $(SRC_DIR)/ytrace/ytrace-ctl.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

# Run examples
.PHONY: run-basic run-custom_handler run-all

run-basic: $(BUILD_DIR)/basic
	@echo "=== Running basic example ==="
	@./$(BUILD_DIR)/basic

run-custom_handler: $(BUILD_DIR)/custom_handler
	@echo "=== Running custom_handler example ==="
	@./$(BUILD_DIR)/custom_handler

run-all: $(EXAMPLE_TARGETS)
	@echo "=== Running basic example ==="
	@./$(BUILD_DIR)/basic
	@echo ""
	@echo "=== Running custom_handler example ==="
	@./$(BUILD_DIR)/custom_handler
