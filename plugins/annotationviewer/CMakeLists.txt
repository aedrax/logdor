set(CMAKE_AUTOMOC ON)

qt_add_plugin(annotationviewer
    CLASS_NAME AnnotationViewer
)

target_sources(annotationviewer
    PRIVATE
        annotationviewer.cpp
    PUBLIC
        annotationviewer.h
)

set_target_properties(annotationviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(annotationviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(annotationviewer
    PRIVATE
        logdor_interface
        Logdor::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

install(TARGETS annotationviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)
