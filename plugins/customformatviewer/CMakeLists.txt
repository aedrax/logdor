set(CMAKE_AUTOMOC ON)

qt_add_plugin(customformatviewer
    CLASS_NAME CustomFormatViewer
)

target_sources(customformatviewer
    PRIVATE
        customformatviewer.cpp
    PUBLIC
        customformatviewer.h
)

set_target_properties(customformatviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(customformatviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(customformatviewer
    PRIVATE
        logdor_interface
        Logdor::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

install(TARGETS customformatviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
