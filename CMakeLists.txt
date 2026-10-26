cmake_minimum_required(VERSION 3.21)
project(Typist VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TYPIST_BUILD_APP "Build the Qt Widgets application" ON)
option(TYPIST_BUILD_TESTS "Build tests" ON)

# enable_testing must run before any add_test in a subdirectory, otherwise that
# test never lands in CTestTestfile.cmake.
enable_testing()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(CmarkGfm REQUIRED)
add_subdirectory(src/core)

# Widgets and Test are required; PrintSupport/PrintTest stay optional because
# some distro packages (MSYS2) ship PrintSupport without the PrintTest module.
find_package(Qt6 COMPONENTS Widgets Test OPTIONAL_COMPONENTS PrintSupport PrintTest QUIET)
if(TYPIST_BUILD_APP AND Qt6_FOUND)
  add_subdirectory(src/platform)
  add_subdirectory(src/editor)
  add_subdirectory(src/ui)
  add_subdirectory(tests/editor)
  add_subdirectory(tests/perf)
endif()

if(TYPIST_BUILD_TESTS)
  add_subdirectory(tests/core)
endif()
