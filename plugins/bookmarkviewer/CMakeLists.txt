set(CMAKE_AUTOMOC ON)

qt_add_plugin(bookmarkviewer
    CLASS_NAME BookmarkViewer
)

target_sources(bookmarkviewer PRIVATE
    bookmarkviewer.cpp
    bookmarkviewer.h
)

set_target_properties(bookmarkviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(bookmarkviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(bookmarkviewer PRIVATE
    logdor_interface
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS bookmarkviewer
    LIBRARY DESTINATION plugins COMPONENT Runtime
    RUNTIME DESTINATION plugins COMPONENT Runtime
)
