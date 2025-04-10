set(CMAKE_AUTOMOC ON)

qt_add_plugin(syslogviewer
    CLASS_NAME SyslogViewer
)

target_sources(syslogviewer
    PRIVATE
        syslogviewer.cpp
        syslogtablemodel.cpp
        syslogtablemodel.h
        syslogentry.h
    PUBLIC
        syslogviewer.h
)

set_target_properties(syslogviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(syslogviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(syslogviewer
    PRIVATE
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS syslogviewer
    LIBRARY DESTINATION plugins COMPONENT Runtime
    RUNTIME DESTINATION plugins COMPONENT Runtime
)
