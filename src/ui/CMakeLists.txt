add_library(typist_ui_theme STATIC Theme.h Theme.cpp)
add_library(typist::ui_theme ALIAS typist_ui_theme)
target_include_directories(typist_ui_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_ui_theme PUBLIC Qt6::Core)
target_compile_options(typist_ui_theme PRIVATE ${TYPIST_WARNING_OPTIONS})

add_library(typist_ui_autosave STATIC AutoSave.h AutoSave.cpp)
add_library(typist::ui_autosave ALIAS typist_ui_autosave)
target_include_directories(typist_ui_autosave PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(typist_ui_autosave PROPERTIES AUTOMOC ON)
target_link_libraries(typist_ui_autosave PUBLIC Qt6::Core)
target_compile_options(typist_ui_autosave PRIVATE ${TYPIST_WARNING_OPTIONS})

add_library(typist_ui_viewswitch STATIC ViewSwitch.h ViewSwitch.cpp Editor.h Editor.cpp
  ParagraphSourcePanel.h ParagraphSourcePanel.cpp CodeBlockBar.h CodeBlockBar.cpp TableBar.h TableBar.cpp
  LinkNavigation.h LinkNavigation.cpp LinkDialog.h LinkDialog.cpp EpubDialog.h EpubDialog.cpp
  HtmlImportDialog.h HtmlImportDialog.cpp)
target_sources(typist_ui_viewswitch PRIVATE PdfExportDialog.h PdfExportDialog.cpp)
target_sources(typist_ui_viewswitch PRIVATE QuickOpenDialog.h QuickOpenDialog.cpp)
target_sources(typist_ui_viewswitch PRIVATE WorkspaceSearchPanel.h WorkspaceSearchPanel.cpp)
target_sources(typist_ui_viewswitch PRIVATE FolderLocationsMenu.h FolderLocationsMenu.cpp)
add_library(typist::ui_viewswitch ALIAS typist_ui_viewswitch)
target_include_directories(typist_ui_viewswitch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(typist_ui_viewswitch PROPERTIES AUTOMOC ON)
target_link_libraries(typist_ui_viewswitch PUBLIC typist::core typist::editor typist::platform Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_options(typist_ui_viewswitch PRIVATE ${TYPIST_WARNING_OPTIONS})

add_library(typist_ui_search STATIC Search.h Search.cpp)
add_library(typist::ui_search ALIAS typist_ui_search)
target_include_directories(typist_ui_search PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_ui_search PUBLIC Qt6::Core Qt6::Gui)
target_compile_options(typist_ui_search PRIVATE ${TYPIST_WARNING_OPTIONS})

add_library(typist_ui_findbar STATIC FindBar.h FindBar.cpp)
add_library(typist::ui_findbar ALIAS typist_ui_findbar)
set_target_properties(typist_ui_findbar PROPERTIES AUTOMOC ON)
target_include_directories(typist_ui_findbar PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_ui_findbar PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_options(typist_ui_findbar PRIVATE ${TYPIST_WARNING_OPTIONS})

add_library(typist_ui_filetree STATIC FileTree.h FileTree.cpp)
set_target_properties(typist_ui_filetree PROPERTIES AUTOMOC ON)
add_library(typist::ui_filetree ALIAS typist_ui_filetree)
target_include_directories(typist_ui_filetree PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_ui_filetree PUBLIC typist::platform Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_options(typist_ui_filetree PRIVATE ${TYPIST_WARNING_OPTIONS})


add_library(typist_ui_locale STATIC Localization.h Localization.cpp)
add_library(typist::ui_locale ALIAS typist_ui_locale)
target_include_directories(typist_ui_locale PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_ui_locale PUBLIC Qt6::Core)
target_compile_options(typist_ui_locale PRIVATE ${TYPIST_WARNING_OPTIONS})


add_library(typist_ui_recovery STATIC RecoveryStore.h RecoveryStore.cpp)
add_library(typist::ui_recovery ALIAS typist_ui_recovery)
target_include_directories(typist_ui_recovery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_ui_recovery PUBLIC Qt6::Core)
target_compile_options(typist_ui_recovery PRIVATE ${TYPIST_WARNING_OPTIONS})

add_library(typist_ui_conflict STATIC Conflict.h Conflict.cpp)
add_library(typist::ui_conflict ALIAS typist_ui_conflict)
target_include_directories(typist_ui_conflict PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_ui_conflict PUBLIC Qt6::Core)
target_compile_options(typist_ui_conflict PRIVATE ${TYPIST_WARNING_OPTIONS})


add_library(typist_ui_toc STATIC Toc.h Toc.cpp)
add_library(typist::ui_toc ALIAS typist_ui_toc)
target_include_directories(typist_ui_toc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_ui_toc PUBLIC Qt6::Gui)
target_compile_options(typist_ui_toc PRIVATE ${TYPIST_WARNING_OPTIONS})

qt_add_executable(typist main.cpp MainWindow.h MainWindow.cpp)
qt_add_resources(typist "app_assets" PREFIX "/" BASE "${CMAKE_SOURCE_DIR}"
  FILES "${CMAKE_SOURCE_DIR}/packaging/linux/typist.png"
        "${CMAKE_SOURCE_DIR}/translations/typist_zh.ts")

if(WIN32)
  set_target_properties(typist PROPERTIES WIN32_EXECUTABLE TRUE)
  target_sources(typist PRIVATE "${CMAKE_SOURCE_DIR}/packaging/windows/typist.rc")
endif()
target_link_libraries(typist PRIVATE typist::core typist::editor typist::platform typist::ui_theme typist::ui_autosave typist::ui_viewswitch typist::ui_search typist::ui_findbar typist::ui_filetree typist::ui_locale typist::ui_recovery typist::ui_conflict typist::ui_toc typist::ui_update Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(typist PROPERTIES AUTOMOC ON)

if(APPLE)
  set_target_properties(typist PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME Typist
    MACOSX_BUNDLE_BUNDLE_NAME Typist
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.moxi486.typist
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
endif()

install(TARGETS typist RUNTIME DESTINATION bin BUNDLE DESTINATION .)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../themes/default.qss DESTINATION share/typist/themes)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../NOTICE ${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE DESTINATION share/typist OPTIONAL)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../docs DESTINATION share/typist OPTIONAL)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/advanced.md
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/syntax.md
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/code-writing.md
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/table-writing.md
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/list-writing.md
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/link-writing.md
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/epub-writing.md
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/epub-chapters.md
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/import-writing.html
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/pdf-writing.md
  ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/corpus/linked-note.md
  DESTINATION share/typist/examples)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../translations DESTINATION share/typist OPTIONAL)
add_library(typist_ui_update STATIC UpdateChecker.h UpdateChecker.cpp)
add_library(typist::ui_update ALIAS typist_ui_update)
target_include_directories(typist_ui_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_ui_update PUBLIC Qt6::Core)
target_compile_options(typist_ui_update PRIVATE ${TYPIST_WARNING_OPTIONS})

if(TARGET Qt6::Network)
  target_compile_definitions(typist PRIVATE TYPIST_HAS_NETWORK=1 TYPIST_VERSION="${PROJECT_VERSION}")
  target_link_libraries(typist PRIVATE Qt6::Network)
endif()
