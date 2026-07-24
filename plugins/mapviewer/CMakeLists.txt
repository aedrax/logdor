set(CMAKE_AUTOMOC ON)

qt_add_plugin(mapviewer
    CLASS_NAME MapViewer
)

target_sources(mapviewer
    PRIVATE
        mapviewer.cpp
        maptablemodel.cpp
        maptablemodel.h
    PUBLIC
        mapviewer.h
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
        Logdor::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::WebEngineWidgets
)

install(TARGETS mapviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
