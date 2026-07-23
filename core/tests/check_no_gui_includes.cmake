# Fails if any core source includes a Qt GUI header.
# Usage: cmake -DCORE_DIR=<repo>/core -P check_no_gui_includes.cmake

if(NOT DEFINED CORE_DIR)
    message(FATAL_ERROR "CORE_DIR not set")
endif()

file(GLOB_RECURSE core_sources
    "${CORE_DIR}/include/*.h"
    "${CORE_DIR}/src/*.h"
    "${CORE_DIR}/src/*.cpp"
    "${CORE_DIR}/tools/*.h"
    "${CORE_DIR}/tools/*.cpp"
    "${CORE_DIR}/bench/*.h"
    "${CORE_DIR}/bench/*.cpp"
)

set(forbidden "(QtGui/|QtWidgets/|<QWidget>|<QApplication>|<QGuiApplication>|<QPixmap>|<QPainter>)")

foreach(f IN LISTS core_sources)
    file(READ "${f}" contents)
    if(contents MATCHES "${forbidden}")
        message(FATAL_ERROR "GUI include found in core source: ${f}")
    endif()
endforeach()

list(LENGTH core_sources n)
message(STATUS "no-GUI include check: OK (${n} files scanned)")
