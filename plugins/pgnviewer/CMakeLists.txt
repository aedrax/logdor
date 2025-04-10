set(CMAKE_AUTOMOC ON)

qt_add_plugin(pgnviewer
    CLASS_NAME PgnViewer
)

target_sources(pgnviewer
    PRIVATE
        pgnviewer.cpp
        pgntablemodel.cpp
        pgntablemodel.h
        chessboardwidget.cpp
        chessboardwidget.h
        chessgame.cpp
        chessgame.h
    PUBLIC
        pgnviewer.h
)

set_target_properties(pgnviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(pgnviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(pgnviewer
    PRIVATE
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS pgnviewer
    LIBRARY DESTINATION plugins COMPONENT Runtime
    RUNTIME DESTINATION plugins COMPONENT Runtime
)
