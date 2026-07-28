set(CMAKE_AUTOMOC ON)

qt_add_plugin(logviewer
    CLASS_NAME LogViewer
)

target_sources(logviewer
    PRIVATE
        logviewer.cpp
    PUBLIC
        logviewer.h
)

set_target_properties(logviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(logviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(logviewer
    PRIVATE
        logdor_interface
        Logdor::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)


# Install plugin to plugins directory
install(TARGETS logviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
