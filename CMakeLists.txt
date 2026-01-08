cmake_minimum_required(VERSION 3.21)
project(ytrace
    VERSION 1.0.0
    DESCRIPTION "Header-only C++20 runtime-controllable tracing library"
    LANGUAGES CXX
)

# Header-only library
add_library(ytrace INTERFACE)
add_library(ytrace::ytrace ALIAS ytrace)

# Set C++ standard
target_compile_features(ytrace INTERFACE cxx_std_20)

# Include directories
target_include_directories(ytrace INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Formatting backend option
set(YTRACE_FORMAT "snprintf" CACHE STRING "Formatting backend: snprintf, fmtlib, or spdlog")
set_property(CACHE YTRACE_FORMAT PROPERTY STRINGS "snprintf" "fmtlib" "spdlog")

if(YTRACE_FORMAT STREQUAL "fmtlib")
    target_compile_definitions(ytrace INTERFACE YTRACE_USE_FMTLIB)
    # fmtlib dependency (optional - users may provide it)
    if(NOT TARGET fmt::fmt)
        message(STATUS "ytrace: fmtlib backend selected, ensure fmt::fmt is available")
    endif()
elseif(YTRACE_FORMAT STREQUAL "spdlog")
    target_compile_definitions(ytrace INTERFACE YTRACE_USE_SPDLOG)
    # spdlog dependency (optional - users may provide it)
    if(NOT TARGET spdlog::spdlog)
        message(STATUS "ytrace: spdlog backend selected, ensure spdlog::spdlog is available")
    endif()
endif()

message(STATUS "ytrace: Using ${YTRACE_FORMAT} formatting backend")

# Optional: Build examples and tools
option(YTRACE_BUILD_EXAMPLES "Build ytrace examples" OFF)
option(YTRACE_BUILD_TOOLS "Build ytrace-ctl tool" OFF)

if(YTRACE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(YTRACE_BUILD_TOOLS)
    add_subdirectory(src/ytrace)
endif()

# Install configuration for CPM
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include
)

install(
    TARGETS ytrace
    EXPORT ytraceTargets
)

install(
    EXPORT ytraceTargets
    NAMESPACE ytrace::
    DESTINATION lib/cmake/ytrace
)
