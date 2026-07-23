cmake_minimum_required(VERSION 3.22)
project(Logdor LANGUAGES CXX)

# Catch typo'd/non-target link entries early (3.27+; older CMake still gets the
# ldd-based GUI-linkage test in core/tests).
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.27)
    set(CMAKE_LINK_LIBRARIES_ONLY_TARGETS ON)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Sql)

qt_standard_project_setup(REQUIRES 6.8)

include(CTest)
option(LOGDOR_ENABLE_BENCH "Register benchmark executables with CTest (label: bench)" OFF)
option(LOGDOR_BENCH_LARGE "Also register the 5 GB benchmark corpus" OFF)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(plugins)
