set(CMAKE_AUTOMOC ON)

qt_add_plugin(pythonlogviewer
    CLASS_NAME PythonLogViewer
)

target_sources(pythonlogviewer
    PRIVATE
        pythonlogviewer.cpp
        pythonlogtablemodel.cpp
        pythonlogtablemodel.h
        pythonlogentry.h
    PUBLIC
        pythonlogviewer.h
)

set_target_properties(pythonlogviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(pythonlogviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(pythonlogviewer
    PRIVATE
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS pythonlogviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
