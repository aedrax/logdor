cmake_minimum_required(VERSION 3.16)

project(Logdor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent Sql)

# Create plugin interface library
add_library(logdor_interface SHARED
    src/plugininterface.h
    src/plugininterface.cpp
    src/regexpluginbase.h
    src/regexpluginbase.cpp
    src/regextablemodel.h
    src/regextablemodel.cpp
    src/databasemanager.h
    src/databasemanager.cpp
    src/plugindatabasemanager.h
    src/plugindatabasemanager.cpp
    src/parallellogparser.h
    src/parallellogparser.cpp
    src/filechunker.h
    src/filechunker.cpp
    src/databaseconnectionpool.h
    src/databaseconnectionpool.cpp
    src/searchtokenizer.h
    src/searchtokenizer.cpp
    src/logtablemodel.h
    src/logtablemodel.cpp
    src/logviewerwidget.h
    src/logviewerwidget.cpp
    src/formatcatalog.h
    src/formatcatalog.cpp
)

target_link_libraries(logdor_interface PUBLIC
    Logdor::Core
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Sql
)

if(BUILD_TESTING)
    qt_add_executable(tst_logtablemodel tests/tst_logtablemodel.cpp)
    target_link_libraries(tst_logtablemodel PRIVATE
        logdor_interface Logdor::Core Qt6::Test Qt6::Widgets)
    add_test(NAME app.logtablemodel COMMAND tst_logtablemodel)
    set_tests_properties(app.logtablemodel PROPERTIES LABELS "unit"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Main executable
set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    src/pluginmanager.cpp
    src/pluginmanager.h
    src/backgroundtaskmanager.cpp
    src/backgroundtaskmanager.h
    src/progresstracker.cpp
    src/progresstracker.h
    src/progressdialog.cpp
    src/progressdialog.h
    src/pluginprocessingtask.cpp
    src/pluginprocessingtask.h
    src/legacybridge.cpp
    src/legacybridge.h
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
