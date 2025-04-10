set(CMAKE_AUTOMOC ON)

qt_add_plugin(plaintextviewer
    CLASS_NAME PlainTextViewer
)

target_sources(plaintextviewer
    PRIVATE
        plaintextviewer.cpp
        plaintexttablemodel.cpp
        plaintexttablemodel.h
    PUBLIC
        plaintextviewer.h
)

set_target_properties(plaintextviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(plaintextviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(plaintextviewer
    PRIVATE
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)


# Install plugin to plugins directory
install(TARGETS plaintextviewer
    LIBRARY DESTINATION plugins COMPONENT Runtime
    RUNTIME DESTINATION plugins COMPONENT Runtime
)
