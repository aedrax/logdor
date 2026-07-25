set(CMAKE_AUTOMOC ON)

qt_add_plugin(netlogviewer
    CLASS_NAME NetLogViewer
)

target_sources(netlogviewer
    PRIVATE
        netlogviewer.cpp
    PUBLIC
        netlogviewer.h
)

set_target_properties(netlogviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(netlogviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(netlogviewer
    PRIVATE
        logdor_interface
        Logdor::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS netlogviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
