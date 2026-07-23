# logdor-core: the non-GUI functional core.
# Hard rule: may link QtCore/QtConcurrent (later QtSql) but NEVER QtGui/QtWidgets.
# Enforced by core/tests/check_no_gui.cmake and check_no_gui_includes.cmake.

qt_add_library(logdor-core STATIC
    include/logdor/Version.h
    src/Version.cpp
    include/logdor/LineIndex.h
    src/LineIndex.cpp
    include/logdor/FileSource.h
    src/FileSource.cpp
    include/logdor/LineIndexer.h
    src/LineIndexer.cpp
    include/logdor/FormatParser.h
    include/logdor/PlainTextParser.h
    src/PlainTextParser.cpp
    include/logdor/LogcatParser.h
    src/LogcatParser.cpp
    include/logdor/ClfParser.h
    src/ClfParser.cpp
)
add_library(Logdor::Core ALIAS logdor-core)

target_include_directories(logdor-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(logdor-core
    PUBLIC Qt6::Core
    PRIVATE Qt6::Concurrent
)

# Configure-time guard: reject GUI modules in the direct link list. (The ldd
# test in core/tests catches actual GUI symbol usage; --as-needed hides a
# merely-listed-but-unused library from ldd, so both checks are needed.)
get_target_property(_core_links logdor-core LINK_LIBRARIES)
if(_core_links MATCHES "Qt6::(Gui|Widgets|Qml|Quick)")
    message(FATAL_ERROR
        "logdor-core must not link Qt GUI modules; found: ${_core_links}")
endif()
target_compile_features(logdor-core PUBLIC cxx_std_20)
set_target_properties(logdor-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(tools/loggen)
if(BUILD_TESTING)
    add_subdirectory(tests)
    add_subdirectory(bench)
endif()
