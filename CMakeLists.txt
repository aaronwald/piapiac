cmake_minimum_required (VERSION 3.14)
project(piapiac)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

include(FetchContent)

# set(CMAKE_INSTALL_RPATH "/usr/local/lib")
set(CMAKE_BUILD_RPATH "/usr/local/lib")

# BEGIN piapiac
add_executable(piapiac "src/main.cpp")
set(piapiac_debug_flags -Wpedantic -Wno-deprecated-declarations -fno-omit-frame-pointer -g -O0 -DSPDLOG_DEBUG_ON=1 -DSPDLOG_TRACE_ON=1 )
set(piapiac_release_flags  -Wpedantic -Wno-deprecated-declarations -fno-omit-frame-pointer -g -O3 -DSPDLOG_DEBUG_ON=0 -DSPDLOG_TRACE_ON=0 )
target_compile_options(piapiac PRIVATE "$<IF:$<CONFIG:Debug>,${piapiac_debug_flags},${piapiac_release_flags}>")
target_compile_options(piapiac PRIVATE -Werror -Wall -Wextra)
target_link_options(piapiac PRIVATE -fsanitize=address -fsanitize=undefined -v)
target_link_libraries(piapiac duckdb)
# END piapiac

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  add_subdirectory(tests)
  # enable testing in parent
  enable_testing()
endif()