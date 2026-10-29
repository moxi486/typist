# MicroTeX 0e3707f6dafebb121d98b53c64364d16fefe481d (MIT).
# Only the native Qt renderer and math fonts are built; no sample applications.
file(GLOB_RECURSE microtex_sources CONFIGURE_DEPENDS
  src/atom/*.cpp src/box/*.cpp src/core/*.cpp src/fonts/*.cpp
  src/utils/*.cpp src/res/*.cpp)
file(GLOB_RECURSE microtex_fonts RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" res/fonts/*.ttf)
set(math_qrc "<RCC><qresource prefix=\"/microtex\">")
foreach(font IN LISTS microtex_fonts)
  string(APPEND math_qrc "<file alias=\"${font}\">${CMAKE_CURRENT_SOURCE_DIR}/${font}</file>")
endforeach()
string(APPEND math_qrc "</qresource></RCC>")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/microtex_fonts.qrc" CONTENT "${math_qrc}")
add_library(typist_microtex STATIC ${microtex_sources}
  src/latex.cpp src/render.cpp src/platform/qt/graphic_qt.cpp
  ../tinyxml2/tinyxml2.cpp "${CMAKE_CURRENT_BINARY_DIR}/microtex_fonts.qrc")
set_target_properties(typist_microtex PROPERTIES AUTORCC ON)
target_compile_definitions(typist_microtex PUBLIC BUILD_QT)
target_include_directories(typist_microtex PUBLIC src PRIVATE ../tinyxml2)
target_link_libraries(typist_microtex PUBLIC Qt6::Gui)
install(FILES LICENSE DESTINATION share/typist/licenses/microtex)
install(DIRECTORY res/fonts/licences/ DESTINATION share/typist/licenses/math-fonts)
install(FILES ../tinyxml2/LICENSE.txt DESTINATION share/typist/licenses/tinyxml2)
