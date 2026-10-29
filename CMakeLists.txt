cmake_minimum_required(VERSION 3.22)
project(Logdor VERSION 1.1.0 LANGUAGES CXX)

# The only place for the version. The code reads it as LOGDOR_VERSION.
add_compile_definitions(LOGDOR_VERSION="${PROJECT_VERSION}")

# Catch typo'd/non-target link entries early (3.27+; older CMake still gets the
# ldd-based GUI-linkage test in core/tests).
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
    set(CMAKE_LINK_LIBRARIES_ONLY_TARGETS ON)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent
             OPTIONAL_COMPONENTS WebEngineWidgets)

qt_standard_project_setup(REQUIRES 6.8)

include(CTest)
option(LOGDOR_WITH_GZIP "Open .gz logs by inflating them at open time (needs zlib)" ON)
if(LOGDOR_WITH_GZIP)
    find_package(ZLIB REQUIRED)
endif()

option(LOGDOR_WITH_TUI "Build the logdor-tui terminal viewer (fetches QtTUI)" ON)
option(LOGDOR_ENABLE_BENCH "Register benchmark executables with CTest (label: bench)" OFF)
option(LOGDOR_BENCH_LARGE "Also register the 5 GB benchmark corpus" OFF)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(plugins)
if(LOGDOR_WITH_TUI)
    include(GNUInstallDirs)
    add_subdirectory(tui)
endif()
