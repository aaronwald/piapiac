cmake_minimum_required (VERSION 3.14)
project(piapiac)

include(FetchContent)

find_package(libwebsockets CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(yaml CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)
link_directories(${NGHTTP2_LIBRARY_DIRS})
find_path(QUILL_INCLUDE_DIRS "quill/Backend.h")


if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()


# BEGIN rapidjson
FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_SHALLOW    ON
#  GIT_TAG        v1.1.0
)
FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
endif()
# END rapidjson

# BEGIN echidna
FetchContent_Declare(
  libechidna
  GIT_REPOSITORY https://github.com/aaronwald/libechidna.git
  GIT_TAG        main
  GIT_SHALLOW    ON
  )
FetchContent_MakeAvailable(libechidna)
# END echidna


# set(CMAKE_INSTALL_RPATH "/usr/local/lib")
set(CMAKE_BUILD_RPATH "/usr/local/lib")

file(GLOB_RECURSE PIAPIAC_SRC ${PROJECT_SOURCE_DIR}/src/*.cpp)
# BEGIN piapiac
add_executable(piapiac ${PIAPIAC_SRC})
target_include_directories(piapiac PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(piapiac PRIVATE ${libechidna_SOURCE_DIR}/include)
target_include_directories(piapiac PRIVATE ${rapidjson_SOURCE_DIR}/include)

set(piapiac_debug_flags -Wpedantic -Wno-deprecated-declarations -fno-omit-frame-pointer -g -O0 -DSPDLOG_DEBUG_ON=1 -DSPDLOG_TRACE_ON=1 )
set(piapiac_release_flags  -Wpedantic -Wno-deprecated-declarations -fno-omit-frame-pointer -g -O3 -DSPDLOG_DEBUG_ON=0 -DSPDLOG_TRACE_ON=0 )

target_compile_options(piapiac PRIVATE "$<IF:$<CONFIG:Debug>,${piapiac_debug_flags},${piapiac_release_flags}>")
target_compile_options(piapiac PRIVATE -Werror -Wall -Wextra ) # -v for verbose
target_link_options(piapiac PRIVATE -fsanitize=address -fsanitize=undefined -v) # -v for verbose
target_link_libraries(piapiac PRIVATE pthread duckdb  OpenSSL::SSL OpenSSL::Crypto numa yaml nghttp2 echidna)


# END piapiac

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  add_subdirectory(tests)
  # enable testing in parent
  enable_testing()
endif()