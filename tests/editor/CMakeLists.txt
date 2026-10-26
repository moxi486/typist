qt_add_executable(editor_spike roundtrip_spike.cpp)
set_target_properties(editor_spike PROPERTIES AUTOMOC ON)
target_link_libraries(editor_spike PRIVATE typist::core typist::editor typist::platform Qt6::Test Qt6::Widgets)
add_test(NAME editor_roundtrip COMMAND editor_spike)

if(WIN32)
  set_tests_properties(editor_roundtrip PROPERTIES
    ENVIRONMENT "PATH=${cmark-gfm_BIN_DIR}\;$ENV{PATH};TYPIST_CORPUS_DIR=${CMAKE_SOURCE_DIR}/tests/corpus;QT_QPA_PLATFORM=offscreen")
else()
  set_tests_properties(editor_roundtrip PROPERTIES
    ENVIRONMENT "TYPIST_CORPUS_DIR=${CMAKE_SOURCE_DIR}/tests/corpus;QT_QPA_PLATFORM=offscreen")
endif()
