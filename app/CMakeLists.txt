cmake_minimum_required(VERSION 3.16)

project(Logdor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent)

# Create plugin interface library
add_library(logdor_interface SHARED
    src/plugininterface.h
    src/plugininterface.cpp
    src/logtablemodel.h
    src/logtablemodel.cpp
    src/logviewerwidget.h
    src/logviewerwidget.cpp
    src/formatcatalog.h
    src/formatcatalog.cpp
    src/annotationhub.h
    src/annotationhub.cpp
    src/annotationdialog.h
    src/annotationdialog.cpp
    src/annotationexporter.h
    src/annotationexporter.cpp
    src/recentitems.h
    src/recentitems.cpp
    src/folderview.h
    src/folderview.cpp
)

target_link_libraries(logdor_interface PUBLIC
    Logdor::Core
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
)

if(BUILD_TESTING)
    foreach(t logtablemodel annotationhub logviewerwidget recentitems folderview)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp)
        target_link_libraries(tst_${t} PRIVATE
            logdor_interface Logdor::Core Qt6::Test Qt6::Widgets)
        add_test(NAME app.${t} COMMAND tst_${t})
        set_tests_properties(app.${t} PROPERTIES LABELS "unit"
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()

# Main executable
set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/pluginmanager.cpp
    src/pluginmanager.h
    resources/resources.qrc
)

qt_add_executable(logdor
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_link_libraries(logdor PRIVATE
    logdor_interface
    Logdor::Core
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
)

set_target_properties(logdor PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS logdor
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(logdor)
