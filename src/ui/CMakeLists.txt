qt_add_executable(typist main.cpp MainWindow.h MainWindow.cpp)
target_link_libraries(typist PRIVATE typist::core typist::editor Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_options(typist PRIVATE -Wall -Wextra)
