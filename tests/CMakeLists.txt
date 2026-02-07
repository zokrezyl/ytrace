CPMAddPackage(
    NAME ut
    GITHUB_REPOSITORY boost-ext/ut
    GIT_TAG v2.1.0
)

add_executable(ytrace_tests test_ytrace.cpp)
target_link_libraries(ytrace_tests PRIVATE ytrace::ytrace Boost::ut)
target_compile_features(ytrace_tests PRIVATE cxx_std_20)

# Register with CTest
include(CTest)
add_test(NAME ytrace_tests COMMAND ytrace_tests)
