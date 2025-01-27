set(CMAKE_AUTOMOC ON)

qt_add_plugin(logcatviewer
    CLASS_NAME LogcatViewer
)

target_sources(logcatviewer
    PRIVATE
        logcatviewer.cpp
        logcatviewer.h
)

set_target_properties(logcatviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(logcatviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(logcatviewer
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

# Install plugin to plugins directory
install(TARGETS logcatviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
