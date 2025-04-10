set(CMAKE_AUTOMOC ON)

qt_add_plugin(selectedlineviewer
    CLASS_NAME SelectedLineViewer
)

target_sources(selectedlineviewer PRIVATE
    selectedlineviewer.cpp
    selectedlineviewer.h
)

set_target_properties(selectedlineviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(selectedlineviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(selectedlineviewer PRIVATE
    logdor_interface
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS selectedlineviewer
    LIBRARY DESTINATION plugins COMPONENT Runtime
    RUNTIME DESTINATION plugins COMPONENT Runtime
)
