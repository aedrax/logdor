set(CMAKE_AUTOMOC ON)

qt_add_plugin(timelineviewer
    CLASS_NAME TimelineViewer
)

target_sources(timelineviewer
    PRIVATE
        timelineviewer.cpp
        timelinemodel.cpp
    PUBLIC
        timelineviewer.h
        timelinemodel.h
        timelinefile.h
)

set_target_properties(timelineviewer PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX ""
    AUTOMOC ON
)

target_include_directories(timelineviewer
    PRIVATE
        ${CMAKE_SOURCE_DIR}/app/src
)

target_link_libraries(timelineviewer
    PRIVATE
        logdor_interface
        Logdor::Core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
)

# Install plugin to plugins directory
install(TARGETS timelineviewer
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

if(BUILD_TESTING)
    qt_add_executable(tst_timelineviewer tst_timelineviewer.cpp)
    target_link_libraries(tst_timelineviewer PRIVATE
        logdor_interface Logdor::Core Qt6::Test Qt6::Widgets)
    target_compile_definitions(tst_timelineviewer PRIVATE
        TIMELINE_PLUGIN_PATH="$<TARGET_FILE:timelineviewer>")
    add_dependencies(tst_timelineviewer timelineviewer)
    add_test(NAME plugins.timelineviewer COMMAND tst_timelineviewer)
    set_tests_properties(plugins.timelineviewer PROPERTIES LABELS "unit"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
