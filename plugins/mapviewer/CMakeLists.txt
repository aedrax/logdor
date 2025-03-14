set(CMAKE_AUTOMOC ON)

qt_add_plugin(mapviewer
    CLASS_NAME MapViewer
)

find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Gui 
    Widgets 
    WebEngineWidgets 
    Positioning
    WebChannel
    WebEngineCore
)

target_sources(mapviewer
    PRIVATE
        mapviewer.cpp
        mapviewer.h
        maptablemodel.cpp
        maptablemodel.h
)

target_link_libraries(mapviewer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::Positioning
    Qt6::WebChannel
    Qt6::WebEngineCore
)

set_target_properties(mapviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(mapviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(mapviewer
    PRIVATE
        logdor_interface
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS mapviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
