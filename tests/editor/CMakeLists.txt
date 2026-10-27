qt_add_executable(editor_spike roundtrip_spike.cpp)
set_target_properties(editor_spike PROPERTIES AUTOMOC ON)
target_link_libraries(editor_spike PRIVATE typist::core typist::editor typist::platform typist::ui_theme typist::ui_autosave typist::ui_viewswitch typist::ui_search typist::ui_filetree typist::ui_locale typist::ui_recovery typist::ui_conflict typist::ui_toc typist::ui_update Qt6::Test Qt6::Widgets)
add_test(NAME editor_roundtrip COMMAND editor_spike)

if(WIN32)
  get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
  file(TO_NATIVE_PATH "${qt_prefix}/bin" qt_bin_native)
  file(TO_NATIVE_PATH "${cmark-gfm_BIN_DIR}" cmark_bin_native)
  set_tests_properties(editor_roundtrip PROPERTIES
    ENVIRONMENT "PATH=${cmark_bin_native}\;${qt_bin_native};TYPIST_CORPUS_DIR=${CMAKE_SOURCE_DIR}/tests/corpus;TYPIST_TS_FILE=${CMAKE_SOURCE_DIR}/translations/typist_zh.ts;TYPIST_UI_SOURCE=${CMAKE_SOURCE_DIR}/src/ui/MainWindow.cpp;QT_QPA_PLATFORM=offscreen")
else()
  set_tests_properties(editor_roundtrip PROPERTIES
    ENVIRONMENT "TYPIST_CORPUS_DIR=${CMAKE_SOURCE_DIR}/tests/corpus;TYPIST_TS_FILE=${CMAKE_SOURCE_DIR}/translations/typist_zh.ts;TYPIST_UI_SOURCE=${CMAKE_SOURCE_DIR}/src/ui/MainWindow.cpp;QT_QPA_PLATFORM=offscreen")
endif()
