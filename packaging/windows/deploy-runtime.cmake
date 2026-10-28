# windeployqt from MSYS2 does not copy the MinGW and third-party Qt runtimes.
# Inspect both the executable and deployed plugins so their transitive DLLs are
# included, while Windows system libraries remain supplied by the OS.
cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

if(NOT DEFINED STAGE_DIR OR NOT DEFINED MINGW_BIN)
  message(FATAL_ERROR "STAGE_DIR and MINGW_BIN are required")
endif()
get_filename_component(STAGE_DIR "${STAGE_DIR}" ABSOLUTE)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${MINGW_BIN}/objdump.exe")
set(ENV{PATH} "${STAGE_DIR}/bin;${MINGW_BIN};$ENV{PATH}")
file(GLOB_RECURSE plugins "${STAGE_DIR}/bin/*.dll")
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${STAGE_DIR}/bin/typist.exe"
  LIBRARIES ${plugins}
  DIRECTORIES "${STAGE_DIR}/bin" "${MINGW_BIN}"
  RESOLVED_DEPENDENCIES_VAR runtimes
  UNRESOLVED_DEPENDENCIES_VAR unresolved
  PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-win-.*"
  POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*")
if(unresolved)
  message(FATAL_ERROR "Unresolved runtime libraries: ${unresolved}")
endif()
foreach(runtime IN LISTS runtimes)
  get_filename_component(runtime_dir "${runtime}" DIRECTORY)
  if(NOT runtime_dir STREQUAL "${STAGE_DIR}/bin")
    file(COPY "${runtime}" DESTINATION "${STAGE_DIR}/bin")
  endif()
endforeach()
