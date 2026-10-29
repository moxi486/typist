add_library(typist_editor STATIC editor_document.h editor_document.cpp Editing.h Editing.cpp
  CodeEditing.h CodeEditing.cpp TableEditing.h TableEditing.cpp ListEditing.h ListEditing.cpp
  LinkEditing.h LinkEditing.cpp)
add_library(typist::editor ALIAS typist_editor)
target_sources(typist_editor PRIVATE HtmlImporter.h HtmlImporter.cpp)
target_include_directories(typist_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_editor PUBLIC typist::core typist_render Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_options(typist_editor PRIVATE ${TYPIST_WARNING_OPTIONS})
