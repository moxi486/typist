add_executable(typist_core_tests core_tests.cpp)
target_link_libraries(typist_core_tests PRIVATE typist::core)
target_compile_options(typist_core_tests PRIVATE ${TYPIST_WARNING_OPTIONS})
add_test(NAME core_fidelity COMMAND typist_core_tests ${CMAKE_SOURCE_DIR}/tests/corpus)
if(WIN32)
  # The runtime DLLs live next to the import libraries: cmark in ../bin, Qt and the
  # GCC runtime under the Qt prefix's bin. Derive both instead of inheriting shell PATH.
  get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
  file(TO_NATIVE_PATH "${qt_prefix}/bin" qt_bin_native)
  file(TO_NATIVE_PATH "${cmark-gfm_BIN_DIR}" cmark_bin_native)
  # ENVIRONMENT splits entries on ';' at CMake parse time, so separators need \;.
  set_tests_properties(core_fidelity PROPERTIES ENVIRONMENT "PATH=${cmark_bin_native}\;${qt_bin_native}")
endif()
