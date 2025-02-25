set(CMAKE_AUTOMOC ON)

qt_add_plugin(logcatviewer
    CLASS_NAME LogcatViewer
)

target_sources(logcatviewer
    PRIVATE
        logcatviewer.cpp
        logcattablemodel.cpp
        logcattablemodel.h
        taglabel.cpp
        taglabel.h
        logcatentry.h
    PUBLIC
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
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS logcatviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
