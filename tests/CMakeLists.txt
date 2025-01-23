# BEGIN googletest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.11.0
)

include(GoogleTest)
FetchContent_MakeAvailable(googletest)
enable_testing()
# END googletest


file(GLOB_RECURSE PIAPIAC_TEST_SRC ${PROJECT_SOURCE_DIR}/tests/testcases*.cpp)
set(piapiactest_flags -DSPDLOG_DEBUG_ON=1 -DSPDLOG_TRACE_ON=1)
add_executable(piapiactest ${PIAPIAC_TEST_SRC} ${PIAPIAC_SRC} )
target_include_directories(piapiactest PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_include_directories(piapiactest PRIVATE ${libechidna_SOURCE_DIR}/include)
target_include_directories(piapiactest PRIVATE ${rapidjson_SOURCE_DIR}/include)

target_link_libraries(piapiactest gtest_main duckdb)

# Load tests...
gtest_discover_tests(piapiactest)