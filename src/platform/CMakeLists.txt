add_library(typist_platform STATIC FileWriter.h FileWriter.cpp PdfWriter.h PdfWriter.cpp Settings.h Settings.cpp FileSearch.h FileSearch.cpp FileOps.h FileOps.cpp DocxWriter.h DocxWriter.cpp)
add_library(typist::platform ALIAS typist_platform)
target_include_directories(typist_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# QPdfWriter lives in Qt6::Gui, so PDF export needs no PrintSupport module.
target_link_libraries(typist_platform PUBLIC typist::core Qt6::Core Qt6::Gui)

if(Qt6PrintSupport_FOUND)
  target_link_libraries(typist_platform PUBLIC Qt6::PrintSupport)
  target_compile_definitions(typist_platform PUBLIC TYPIST_HAS_PRINTSUPPORT=1)
endif()
target_compile_options(typist_platform PRIVATE -Wall -Wextra)
