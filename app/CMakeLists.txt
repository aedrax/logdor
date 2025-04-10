cmake_minimum_required(VERSION 3.16)

project(Logdor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Concurrent)

set(QT_RESOURCES_DIR "${QT_ARCH_DIR}/resources")
message(STATUS "Determined Qt Resources Directory: ${QT_RESOURCES_DIR}") # Optional: for debugging

# Create plugin interface library
add_library(logdor_interface SHARED
    src/plugininterface.h
)

target_link_libraries(logdor_interface PUBLIC
    Qt6::Core
    Qt6::Widgets
    Qt6::Concurrent
)

# Install the interface library
include(GNUInstallDirs)
install(TARGETS logdor_interface
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} # For .so/.dylib
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} # For .dll
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} # For .lib (Windows)
)

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
    COMPONENT Runtime # Assign to a component for finer control
)

# Install Qt's platform themes
set(QT_THEMES_DIR "${QT_ARCH_DIR}/plugins/platformthemes")
if(EXISTS ${QT_THEMES_DIR})
    install(DIRECTORY ${QT_THEMES_DIR}
        DESTINATION plugins/
        COMPONENT Runtime
    )
else()
    message(WARNING "Qt themes directory not found at ${QT_THEMES_DIR}. Skipping installation.")
endif()

# Generate a deployment script that CPack will execute
qt_generate_deploy_app_script(
    TARGET logdor
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR # Don't error if run on unsupported platform
)

# Install the deployment script
install(SCRIPT ${deploy_script})

qt_finalize_executable(logdor)
