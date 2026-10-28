cmake_minimum_required(VERSION 3.21)
project(Typist VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  set(TYPIST_WARNING_OPTIONS /W4 /utf-8)
else()
  set(TYPIST_WARNING_OPTIONS -Wall -Wextra)
endif()

# Packaging: CPack builds the distributable bundles. Platform generators are chosen
# here; CI adds DEB/RPM explicitly where the tools exist on the runner.
set(CPACK_PACKAGE_NAME "Typist")
set(CPACK_PACKAGE_VENDOR "Typist project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WYSIWYG Markdown editor built on Qt 6")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Typist")
set(CPACK_PACKAGE_CONTACT "typist-project@example.org")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
  set(CPACK_GENERATOR "TGZ;DragNDrop")
else()
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "typist-project@example.org")
  set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
  set(CPACK_RPM_PACKAGE_LICENSE "MIT")
endif()
include(CPack)

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
find_package(Qt6 COMPONENTS Widgets Test OPTIONAL_COMPONENTS PrintSupport PrintTest Network QUIET)
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
