set(CMAKE_AUTOMOC ON)

qt_add_plugin(csvviewer
    CLASS_NAME CSVViewer
)

target_sources(csvviewer
    PRIVATE
        csvviewer.cpp
        csvtablemodel.cpp
        csvtablemodel.h
        csventry.h
    PUBLIC
        csvviewer.h
)

set_target_properties(csvviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(csvviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(csvviewer
    PRIVATE
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS csvviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
