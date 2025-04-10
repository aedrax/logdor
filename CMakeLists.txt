cmake_minimum_required(VERSION 3.16)
project(Logdor LANGUAGES CXX)
string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWER)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

qt_standard_project_setup(REQUIRES 6.8)

# Determine Qt installation prefix to find resources
get_filename_component(QT_CMAKE_DIR ${Qt6_DIR} DIRECTORY)
get_filename_component(QT_LIB_DIR ${QT_CMAKE_DIR} DIRECTORY)
get_filename_component(QT_ARCH_DIR ${QT_LIB_DIR} DIRECTORY) # This should be the correct prefix like /path/to/Qt/6.x.y/gcc_64

set(ICON_PATH ${CMAKE_CURRENT_SOURCE_DIR}/app/resources/icons/${PROJECT_NAME_LOWER}.png)

add_subdirectory(app)
add_subdirectory(plugins)

# --- CPack configuration ---
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION "0.1.0") # TODO: Set a proper version
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Logdor - A log viewing application")
set(CPACK_PACKAGE_VENDOR "Logdor Developers")
set(CPACK_PACKAGE_CONTACT "paulsorensen5@gmail.com")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
set(CPACK_PACKAGE_ICON ${ICON_PATH})



# --- Bundle Runtime Dependencies ---
# Include the module that helps bundle runtime dependencies
include(InstallRequiredSystemLibraries)
# Enable the bundling for CPack. This requires targets to be installed with a COMPONENT.
set(CPACK_INSTALL_REQUIRED_SYSTEM_LIBRARIES ON)

# --- Platform-specific Generators ---
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "${CPACK_PACKAGE_NAME} ${CPACK_PACKAGE_VERSION}")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    # TODO: Add icon, etc. for NSIS if desired
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_BACKGROUND_IMAGE "") # Optional: path to background image
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "") # Optional: script to customize DMG layout
elseif(UNIX)
    # Assume Linux if not Apple
    set(CPACK_GENERATOR "DEB;RPM") # Generate both DEB and RPM
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_RPM_PACKAGE_RELEASE "1") # Increment on rebuilds
    set(CPACK_RPM_PACKAGE_LICENSE "MIT") # Adjust if needed (e.g., MIT, GPL)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libxcb-cursor0")
    # Add the icon file
    install(FILES ${ICON_PATH} DESTINATION share/icons/hicolor/256x256/apps)
    # Add the desktop file
    INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/app/resources/${PROJECT_NAME_LOWER}.desktop DESTINATION share/applications)
endif()

# Include CPack module AFTER setting generators and other CPack variables
include(CPack)

# --- Components ---
# Group the main application files
cpack_add_component(Runtime
    DISPLAY_NAME "Application"
    DESCRIPTION "The main Logdor application executable and libraries."
    REQUIRED # This component is mandatory
)
