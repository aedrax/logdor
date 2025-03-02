set(CMAKE_AUTOMOC ON)

qt_add_plugin(commonlogformatviewer
    CLASS_NAME CLFViewer
)

target_sources(commonlogformatviewer
    PRIVATE
        clfentry.h
        clftablemodel.cpp
        clftablemodel.h
        clfviewer.cpp
        clfviewer.h
)

set_target_properties(commonlogformatviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(commonlogformatviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(commonlogformatviewer
    PRIVATE
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS commonlogformatviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
