add_library(typist_platform STATIC FileWriter.h FileWriter.cpp PdfWriter.h PdfWriter.cpp Settings.h Settings.cpp FileSearch.h FileSearch.cpp FileOps.h FileOps.cpp DocxWriter.h DocxWriter.cpp SystemTheme.h SystemTheme.cpp FolderSearchRunner.h FolderSearchRunner.cpp ImageAssets.h ImageAssets.cpp)
target_sources(typist_platform PRIVATE ZipArchive.h ZipArchive.cpp EpubWriter.h EpubWriter.cpp)
add_library(typist::platform ALIAS typist_platform)
# FolderSearchRunner is a QObject with signals, so its moc output must be generated.
set_target_properties(typist_platform PROPERTIES AUTOMOC ON)
target_include_directories(typist_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# QPdfWriter lives in Qt6::Gui, so PDF export needs no PrintSupport module.
target_link_libraries(typist_platform PUBLIC typist::core typist_render Qt6::Core Qt6::Gui)

if(Qt6PrintSupport_FOUND)
  target_link_libraries(typist_platform PUBLIC Qt6::PrintSupport)
  target_compile_definitions(typist_platform PUBLIC TYPIST_HAS_PRINTSUPPORT=1)
endif()
target_compile_options(typist_platform PRIVATE ${TYPIST_WARNING_OPTIONS})
