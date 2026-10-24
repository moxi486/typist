add_executable(typist_core_tests core_tests.cpp)
target_link_libraries(typist_core_tests PRIVATE typist::core)
target_compile_options(typist_core_tests PRIVATE -Wall -Wextra)
add_test(NAME core_fidelity COMMAND typist_core_tests ${CMAKE_SOURCE_DIR}/tests/corpus)
