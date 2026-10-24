add_library(typist_platform STATIC FileWriter.h FileWriter.cpp)
add_library(typist::platform ALIAS typist_platform)
target_link_libraries(typist_platform PUBLIC Qt6::Core)
target_compile_options(typist_platform PRIVATE -Wall -Wextra)
