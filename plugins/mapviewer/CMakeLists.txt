set(CMAKE_AUTOMOC ON)

set(QT_RESOURCES_DIR "${QT_ARCH_DIR}/resources")
message(STATUS "Determined Qt Resources Directory: ${QT_RESOURCES_DIR}") # Optional: for debugging

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
    LIBRARY DESTINATION plugins COMPONENT Runtime
    RUNTIME DESTINATION plugins COMPONENT Runtime
)

# Install Qt's own resources directory (needed for WebEngine, etc.)
if(EXISTS ${QT_RESOURCES_DIR})
    install(DIRECTORY ${QT_RESOURCES_DIR}/
        DESTINATION resources
        COMPONENT Runtime
    )
else()
    message(WARNING "Qt resources directory not found at ${QT_RESOURCES_DIR}. Skipping installation.")
endif()

if(WIN32)
    # TODO: figure this out
elseif(APPLE)
    # TODO: figure this out
elseif(UNIX)
    # Assume Linux if not Apple
    # For some reason I have to manually install these because they're not being detected
    set(EXTRA_DEPENDENCIES
        ${QT_ARCH_DIR}/lib/libQt6WebEngineWidgets.so.6
        ${QT_ARCH_DIR}/lib/libQt6WebEngineCore.so.6
        ${QT_ARCH_DIR}/lib/libQt6Positioning.so.6
        ${QT_ARCH_DIR}/lib/libQt6QuickWidgets.so.6
        ${QT_ARCH_DIR}/lib/libQt6PrintSupport.so.6
        ${QT_ARCH_DIR}/lib/libQt6WebChannel.so.6
        ${QT_ARCH_DIR}/lib/libQt6Quick.so.6
        ${QT_ARCH_DIR}/lib/libQt6QmlMeta.so.6
        ${QT_ARCH_DIR}/lib/libQt6QmlModels.so.6
        ${QT_ARCH_DIR}/lib/libQt6QmlWorkerScript.so.6
        ${QT_ARCH_DIR}/lib/libQt6Qml.so.6
        ${QT_ARCH_DIR}/lib/libQt6Network.so.6
    )

    foreach(FILE ${EXTRA_DEPENDENCIES})
        install(FILES ${FILE} DESTINATION lib)
        get_filename_component(REAL_PATH "${FILE}" REALPATH)
        install(FILES ${REAL_PATH} DESTINATION lib)
    endforeach()

    # also the web engine needs this program
    install(PROGRAMS ${QT_ARCH_DIR}/libexec/QtWebEngineProcess DESTINATION bin)
endif()