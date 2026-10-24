cmake_minimum_required(VERSION 3.21)
project(Typist VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TYPIST_BUILD_APP "Build the Qt Widgets application" ON)
option(TYPIST_BUILD_TESTS "Build tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(cmark-gfm REQUIRED)
add_subdirectory(src/core)

find_package(Qt6 COMPONENTS Widgets Test PrintTest QUIET)
if(TYPIST_BUILD_APP AND Qt6_FOUND)
  add_subdirectory(src/ui)
  add_subdirectory(tests/editor)
endif()

if(TYPIST_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/core)
endif()
