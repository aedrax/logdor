# Fails if the core link guard binary transitively links any Qt GUI library.
# Usage: cmake -DBIN=<path-to-core_link_guard> -P check_no_gui.cmake

if(NOT DEFINED BIN)
    message(FATAL_ERROR "BIN not set")
endif()

if(NOT (UNIX AND NOT APPLE))
    message(STATUS "no-GUI linkage check: skipped (ldd only available on Linux)")
    return()
endif()

execute_process(
    COMMAND ldd "${BIN}"
    OUTPUT_VARIABLE ldd_output
    ERROR_VARIABLE ldd_error
    RESULT_VARIABLE ldd_result
)
if(NOT ldd_result EQUAL 0)
    message(FATAL_ERROR "ldd failed on ${BIN}: ${ldd_error}")
endif()

if(ldd_output MATCHES "libQt6(Gui|Widgets|Qml|Quick)")
    message(FATAL_ERROR
        "logdor-core must not link GUI libraries, but the link guard binary does:\n${ldd_output}")
endif()

message(STATUS "no-GUI linkage check: OK")
