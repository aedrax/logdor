set(CMAKE_AUTOMOC ON)

qt_add_plugin(log4jviewer
    CLASS_NAME Log4jViewer
)

target_sources(log4jviewer
    PRIVATE
    log4jentry.h
    log4jtablemodel.cpp
    log4jtablemodel.h
    log4jviewer.cpp
    log4jviewer.h
)

set_target_properties(log4jviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(log4jviewer
    PRIVATE
    ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(log4jviewer
    PRIVATE
    logdor_interface
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS log4jviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
