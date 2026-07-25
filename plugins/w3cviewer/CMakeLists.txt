set(CMAKE_AUTOMOC ON)

qt_add_plugin(w3cviewer
    CLASS_NAME W3CViewer
)

target_sources(w3cviewer
    PRIVATE
        w3cviewer.cpp
    PUBLIC
        w3cviewer.h
)

set_target_properties(w3cviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(w3cviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(w3cviewer
    PRIVATE
        logdor_interface
        Logdor::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS w3cviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
