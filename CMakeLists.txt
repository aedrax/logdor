cmake_minimum_required(VERSION 3.16)
project(Logdor LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup(REQUIRES 6.8)

# add_subdirectory(plugins)
add_subdirectory(app)
add_subdirectory(plugins)
