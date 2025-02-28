set(CMAKE_AUTOMOC ON)

qt_add_plugin(regexviewer
    CLASS_NAME RegexViewer
)

target_sources(regexviewer
    PRIVATE
        regexviewer.cpp
        regextablemodel.cpp
        regextablemodel.h
        regexsettingsdialog.cpp
        regexsettingsdialog.h
        valuecolordialog.cpp
        valuecolordialog.h
    PUBLIC
        regexviewer.h
)

set_target_properties(regexviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(regexviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(regexviewer
    PRIVATE
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS regexviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
