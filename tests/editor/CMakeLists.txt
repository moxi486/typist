qt_add_executable(editor_spike roundtrip_spike.cpp)
target_link_libraries(editor_spike PRIVATE typist::core typist::editor typist::platform Qt6::Test Qt6::Widgets)
qt_add_test(editor_spike)
set_tests_properties(editor_spike PROPERTIES
  ENVIRONMENT "TYPIST_CORPUS_DIR=${CMAKE_SOURCE_DIR}/tests/corpus")
