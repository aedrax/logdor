# Unit tests: one Qt Test executable per core class, label "unit".
# Add new tests by appending to LOGDOR_CORE_TESTS.
set(LOGDOR_CORE_TESTS
    lineindex
    filesource
    lineindexer
    formatparsers
    formatdetect
    rowset
    filterscan
    query
    columnscan
)

foreach(t IN LISTS LOGDOR_CORE_TESTS)
    qt_add_executable(tst_${t} tst_${t}.cpp)
    target_link_libraries(tst_${t} PRIVATE Logdor::Core Qt6::Test)
    add_test(NAME core.${t} COMMAND tst_${t})
    set_tests_properties(core.${t} PROPERTIES LABELS "unit")
endforeach()

# Guard 1: an executable that links ONLY Logdor::Core. Its link step fails if
# core references undefined GUI symbols; the ldd test below catches transitive
# GUI libraries pulled in via interface dependencies.
add_executable(core_link_guard core_link_guard.cpp)
target_link_libraries(core_link_guard PRIVATE Logdor::Core)

add_test(NAME core.no_gui_linkage COMMAND ${CMAKE_COMMAND}
    -DBIN=$<TARGET_FILE:core_link_guard>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_gui.cmake)

# Guard 2: no GUI includes anywhere in core sources.
add_test(NAME core.no_gui_includes COMMAND ${CMAKE_COMMAND}
    -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/..
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_gui_includes.cmake)

set_tests_properties(core.no_gui_linkage core.no_gui_includes
    PROPERTIES LABELS "unit")
