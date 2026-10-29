add_library(typist_render STATIC SpecialContent.h SpecialContent.cpp Mermaid.cpp
  SyntaxHighlighter.h SyntaxHighlighter.cpp ImageResources.h ImageResources.cpp)
set_target_properties(typist_render PROPERTIES AUTOMOC ON)
target_include_directories(typist_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(typist_render PUBLIC typist::core Qt6::Gui PRIVATE typist_microtex)
target_compile_options(typist_render PRIVATE ${TYPIST_WARNING_OPTIONS})
