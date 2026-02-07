cmake_minimum_required(VERSION 3.21)

# Suppress FetchContent deprecation warnings (CPM.cmake uses older API)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

project(ytrace
    VERSION 1.0.0
    DESCRIPTION "Header-only C++20 runtime-controllable tracing library"
    LANGUAGES CXX
)

# Include CPM for dependency management (only if not already included by parent)
if(NOT DEFINED CPM_VERSION)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)
endif()

# Header-only library
add_library(ytrace INTERFACE)
add_library(ytrace::ytrace ALIAS ytrace)

# Set C++ standard
target_compile_features(ytrace INTERFACE cxx_std_20)

# MSVC requires /Zc:preprocessor for __VA_OPT__ support (C++20 conformant preprocessor)
target_compile_options(ytrace INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
)

# Include directories
target_include_directories(ytrace INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Compile-time macro switches (default: all enabled)
option(YTRACE_ENABLE_YLOG "Enable ylog macro" ON)
option(YTRACE_ENABLE_YTRACE "Enable ytrace macro" ON)
option(YTRACE_ENABLE_YDEBUG "Enable ydebug macro" ON)
option(YTRACE_ENABLE_YINFO "Enable yinfo macro" ON)
option(YTRACE_ENABLE_YWARN "Enable ywarn macro" ON)
option(YTRACE_ENABLE_YERROR "Enable yerror macro" ON)
option(YTRACE_ENABLE_YFUNC "Enable yfunc macro" ON)

# Apply compile definitions
if(YTRACE_ENABLE_YLOG)
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YLOG=1)
else()
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YLOG=0)
endif()

if(YTRACE_ENABLE_YTRACE)
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YTRACE=1)
else()
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YTRACE=0)
endif()

if(YTRACE_ENABLE_YDEBUG)
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YDEBUG=1)
else()
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YDEBUG=0)
endif()

if(YTRACE_ENABLE_YINFO)
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YINFO=1)
else()
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YINFO=0)
endif()

if(YTRACE_ENABLE_YWARN)
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YWARN=1)
else()
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YWARN=0)
endif()

if(YTRACE_ENABLE_YERROR)
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YERROR=1)
else()
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YERROR=0)
endif()

if(YTRACE_ENABLE_YFUNC)
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YFUNC=1)
else()
    target_compile_definitions(ytrace INTERFACE YTRACE_ENABLE_YFUNC=0)
endif()

# Optional: Try to pull in spdlog (skip if already available from parent)
option(YTRACE_WITH_SPDLOG "Use spdlog for logging" ON)

if(YTRACE_WITH_SPDLOG AND NOT TARGET spdlog::spdlog)
    # spdlog requires fmt as dependency
    if(NOT TARGET fmt::fmt)
        CPMAddPackage(
            NAME fmt
            GITHUB_REPOSITORY fmtlib/fmt
            GIT_TAG 10.2.1
        )
    endif()
    
    CPMAddPackage(
        NAME spdlog
        GITHUB_REPOSITORY gabime/spdlog
        GIT_TAG v1.14.1
        OPTIONS "SPDLOG_FMT_EXTERNAL ON"
    )
    message(STATUS "ytrace: Downloaded and using spdlog")
endif()

if(YTRACE_WITH_SPDLOG AND TARGET spdlog::spdlog)
    message(STATUS "ytrace: Using spdlog formatting backend")
    target_compile_definitions(ytrace INTERFACE YTRACE_USE_SPDLOG)
    target_link_libraries(ytrace INTERFACE spdlog::spdlog)
else()
    # Explicit formatting backend override
    set(YTRACE_FORMAT "snprintf" CACHE STRING "Formatting backend: snprintf or fmtlib")
    set_property(CACHE YTRACE_FORMAT PROPERTY STRINGS "snprintf" "fmtlib")

    if(YTRACE_FORMAT STREQUAL "fmtlib")
        CPMAddPackage(
            NAME fmt
            GITHUB_REPOSITORY fmtlib/fmt
            GIT_TAG 10.2.1
        )
        target_compile_definitions(ytrace INTERFACE YTRACE_USE_FMTLIB)
        target_link_libraries(ytrace INTERFACE fmt::fmt)
        message(STATUS "ytrace: Using fmtlib formatting backend")
    else()
        message(STATUS "ytrace: Using snprintf formatting backend")
    endif()
endif()

# Optional: Build examples and tools
# Default ON only if ytrace is the top-level project (not a subdirectory)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(YTRACE_DEFAULT_BUILD ON)
else()
    set(YTRACE_DEFAULT_BUILD OFF)
endif()

option(YTRACE_BUILD_EXAMPLES "Build ytrace examples" ${YTRACE_DEFAULT_BUILD})
option(YTRACE_BUILD_TOOLS "Build ytrace-ctl tool" ${YTRACE_DEFAULT_BUILD})
option(YTRACE_BUILD_TESTS "Build ytrace tests" ${YTRACE_DEFAULT_BUILD})

if(YTRACE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(YTRACE_BUILD_TOOLS)
    add_subdirectory(src/ytrace)
endif()

if(YTRACE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install configuration (minimal for header-only lib)
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include
)
