CPMAddPackage(
    NAME ut
    GITHUB_REPOSITORY boost-ext/ut
    GIT_TAG v2.1.0
    OPTIONS "BOOST_UT_DISABLE_MODULE ON"
)

add_executable(ytrace_tests test_ytrace.cpp)
target_link_libraries(ytrace_tests PRIVATE ytrace::ytrace Boost::ut)
target_compile_features(ytrace_tests PRIVATE cxx_std_20)

# Disable C++20 modules for boost.ut on MSVC
target_compile_definitions(ytrace_tests PRIVATE BOOST_UT_DISABLE_MODULE)

# Register with CTest
include(CTest)
add_test(NAME ytrace_tests COMMAND ytrace_tests)
