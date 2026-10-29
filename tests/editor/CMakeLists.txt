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

# QtTest otherwise sends Windows assertion messages only to the debugger.
set_property(TEST editor_roundtrip APPEND PROPERTY ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")

qt_add_executable(window_tests window_tests.cpp ../../src/ui/MainWindow.cpp ../../src/ui/MainWindow.h)
set_target_properties(window_tests PROPERTIES AUTOMOC ON)
target_compile_definitions(window_tests PRIVATE TYPIST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(window_tests PRIVATE typist::core typist::editor typist::platform
  typist::ui_theme typist::ui_autosave typist::ui_viewswitch typist::ui_search
  typist::ui_findbar typist::ui_filetree typist::ui_locale typist::ui_recovery
  typist::ui_conflict typist::ui_toc typist::ui_update Qt6::Test Qt6::Widgets)
if(TARGET Qt6::Network)
  target_compile_definitions(window_tests PRIVATE TYPIST_HAS_NETWORK=1 TYPIST_VERSION="${PROJECT_VERSION}")
  target_link_libraries(window_tests PRIVATE Qt6::Network)
endif()
qt_add_resources(window_tests "test_assets" PREFIX "/" BASE "${CMAKE_SOURCE_DIR}"
  FILES "${CMAKE_SOURCE_DIR}/packaging/linux/typist.png" "${CMAKE_SOURCE_DIR}/translations/typist_zh.ts")
add_test(NAME window_regressions COMMAND window_tests)
get_test_property(editor_roundtrip ENVIRONMENT editor_test_environment)
set_tests_properties(window_regressions PROPERTIES ENVIRONMENT "${editor_test_environment}")

qt_add_executable(editing_tests editing_tests.cpp)
set_target_properties(editing_tests PROPERTIES AUTOMOC ON)
target_link_libraries(editing_tests PRIVATE typist::ui_viewswitch typist::ui_search typist::platform Qt6::Test Qt6::Widgets)
add_test(NAME editing_behaviors COMMAND editing_tests)
set_tests_properties(editing_behaviors PROPERTIES ENVIRONMENT "${editor_test_environment}" TIMEOUT 45)

qt_add_executable(advanced_content_tests advanced_content_tests.cpp)
set_target_properties(advanced_content_tests PROPERTIES AUTOMOC ON)
target_link_libraries(advanced_content_tests PRIVATE typist::ui_viewswitch typist::platform Qt6::Test Qt6::Widgets)
add_test(NAME advanced_markdown COMMAND advanced_content_tests)
set_tests_properties(advanced_markdown PROPERTIES ENVIRONMENT "${editor_test_environment}" TIMEOUT 45)

qt_add_executable(syntax_paragraph_tests syntax_paragraph_tests.cpp)
set_target_properties(syntax_paragraph_tests PROPERTIES AUTOMOC ON)
target_link_libraries(syntax_paragraph_tests PRIVATE typist::ui_viewswitch typist::platform Qt6::Test Qt6::Widgets)
add_test(NAME syntax_and_paragraph COMMAND syntax_paragraph_tests)
set_tests_properties(syntax_and_paragraph PROPERTIES ENVIRONMENT "${editor_test_environment}" TIMEOUT 45)

qt_add_executable(code_writing_tests code_writing_tests.cpp)
set_target_properties(code_writing_tests PROPERTIES AUTOMOC ON)
target_link_libraries(code_writing_tests PRIVATE typist::ui_viewswitch Qt6::Test Qt6::Widgets)
add_test(NAME code_writing COMMAND code_writing_tests)
set_tests_properties(code_writing PROPERTIES ENVIRONMENT "${editor_test_environment}" TIMEOUT 45)

qt_add_executable(image_assets_tests image_assets_tests.cpp ../../src/ui/MainWindow.cpp ../../src/ui/MainWindow.h)
set_target_properties(image_assets_tests PROPERTIES AUTOMOC ON)
target_link_libraries(image_assets_tests PRIVATE typist::core typist::editor typist::platform
  typist::ui_theme typist::ui_autosave typist::ui_viewswitch typist::ui_search
  typist::ui_findbar typist::ui_filetree typist::ui_locale typist::ui_recovery
  typist::ui_conflict typist::ui_toc typist::ui_update Qt6::Test Qt6::Widgets)
if(TARGET Qt6::Network)
  target_compile_definitions(image_assets_tests PRIVATE TYPIST_HAS_NETWORK=1 TYPIST_VERSION="${PROJECT_VERSION}")
  target_link_libraries(image_assets_tests PRIVATE Qt6::Network)
endif()
qt_add_resources(image_assets_tests "image_test_assets" PREFIX "/" BASE "${CMAKE_SOURCE_DIR}"
  FILES "${CMAKE_SOURCE_DIR}/translations/typist_zh.ts")
add_test(NAME image_assets COMMAND image_assets_tests)
set_tests_properties(image_assets PROPERTIES ENVIRONMENT "${editor_test_environment}" TIMEOUT 45)

qt_add_executable(table_workflow_tests table_workflow_tests.cpp)
set_target_properties(table_workflow_tests PROPERTIES AUTOMOC ON)
target_link_libraries(table_workflow_tests PRIVATE typist::ui_viewswitch typist::platform Qt6::Test Qt6::Widgets)
add_test(NAME table_workflow COMMAND table_workflow_tests)
set_tests_properties(table_workflow PROPERTIES ENVIRONMENT "${editor_test_environment}" TIMEOUT 45)

qt_add_executable(list_workflow_tests list_workflow_tests.cpp)
set_target_properties(list_workflow_tests PROPERTIES AUTOMOC ON)
target_link_libraries(list_workflow_tests PRIVATE typist::ui_viewswitch typist::platform Qt6::Test Qt6::Widgets)
add_test(NAME list_workflow COMMAND list_workflow_tests)
set_tests_properties(list_workflow PROPERTIES ENVIRONMENT "${editor_test_environment}" TIMEOUT 45)

qt_add_executable(link_workflow_tests link_workflow_tests.cpp)
set_target_properties(link_workflow_tests PROPERTIES AUTOMOC ON)
target_link_libraries(link_workflow_tests PRIVATE typist::ui_viewswitch typist::platform Qt6::Test Qt6::Widgets)
add_test(NAME link_workflow COMMAND link_workflow_tests)
set_tests_properties(link_workflow PROPERTIES ENVIRONMENT "${editor_test_environment}" TIMEOUT 45)

qt_add_executable(epub_export_tests epub_export_tests.cpp)
set_target_properties(epub_export_tests PROPERTIES AUTOMOC ON)
target_link_libraries(epub_export_tests PRIVATE typist::platform Qt6::Test Qt6::Widgets)
add_test(NAME epub_export COMMAND epub_export_tests)
set_tests_properties(epub_export PROPERTIES ENVIRONMENT "${editor_test_environment}" TIMEOUT 45)
