add_library(typist_editor STATIC editor_document.h editor_document.cpp)
add_library(typist::editor ALIAS typist_editor)
target_link_libraries(typist_editor PUBLIC typist::core Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_options(typist_editor PRIVATE -Wall -Wextra)
