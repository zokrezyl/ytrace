add_executable(ytrace_basic basic.cpp)
target_link_libraries(ytrace_basic PRIVATE ytrace::ytrace)

add_executable(ytrace_custom_handler custom_handler.cpp)
target_link_libraries(ytrace_custom_handler PRIVATE ytrace::ytrace)

add_executable(ytrace_socket_control socket_control.cpp)
target_link_libraries(ytrace_socket_control PRIVATE ytrace::ytrace)

add_executable(ytrace_complex
    complex/main.cpp
    complex/math_ops.cpp
    complex/data_processor.cpp
    complex/network_sim.cpp
)
target_include_directories(ytrace_complex PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/complex)
target_link_libraries(ytrace_complex PRIVATE ytrace::ytrace)

add_executable(ytrace_timers timers.cpp)
target_link_libraries(ytrace_timers PRIVATE ytrace::ytrace)
