set(CMAKE_AUTOMOC ON)

qt_add_plugin(hexdumpviewer
    CLASS_NAME HexDumpViewer
)

target_sources(hexdumpviewer
    PRIVATE
        hexdumpviewer.cpp
    PUBLIC
        hexdumpviewer.h
)

set_target_properties(hexdumpviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(hexdumpviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(hexdumpviewer
    PRIVATE
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS hexdumpviewer
    LIBRARY DESTINATION plugins COMPONENT Runtime
    RUNTIME DESTINATION plugins COMPONENT Runtime
)
