add_library(typist_core STATIC markdown.h markdown.cpp)
add_library(typist::core ALIAS typist_core)
target_link_libraries(typist_core PUBLIC cmark-gfm::cmark-gfm cmark-gfm::extensions)
target_compile_options(typist_core PRIVATE -Wall -Wextra)
