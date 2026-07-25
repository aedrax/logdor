set(CMAKE_AUTOMOC ON)

qt_add_plugin(timelineviewer
    CLASS_NAME TimelineViewer
)

target_sources(timelineviewer
    PRIVATE
        timelineviewer.cpp
    PUBLIC
        timelineviewer.h
        timelinefile.h
)

set_target_properties(timelineviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(timelineviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(timelineviewer
    PRIVATE
        logdor_interface
        Logdor::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS timelineviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
