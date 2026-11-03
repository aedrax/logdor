# logdor-tui: the terminal shell. It links the core and QtTUI, which use
# only QtCore, so it runs on a host without Qt GUI libraries.

include(FetchContent)
FetchContent_Declare(QtTUI
    GIT_REPOSITORY https://github.com/aedrax/QtTUI.git
    GIT_TAG 30e3d00dd0aa8186a7883d9be5231a38025eb48a) # v0.2.1
FetchContent_MakeAvailable(QtTUI)

# formatcatalog.cpp uses only QtCore. Compile it here to get the same
# declarative formats as the GUI without a link to logdor_interface.
qt_add_executable(logdor-tui main.cpp LogModel.h ../app/src/formatcatalog.cpp)
target_include_directories(logdor-tui PRIVATE ../app/src)
target_compile_definitions(logdor-tui PRIVATE LOGDOR_INTERFACE_LIBRARY)
target_link_libraries(logdor-tui PRIVATE Logdor::Core QtTUI::QtTUI)
install(TARGETS logdor-tui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    qt_add_executable(tst_tui_logmodel tests/tst_logmodel.cpp LogModel.h)
    target_include_directories(tst_tui_logmodel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(tst_tui_logmodel PRIVATE Logdor::Core Qt6::Test)
    add_test(NAME tui.logmodel COMMAND tst_tui_logmodel)
    set_tests_properties(tui.logmodel PROPERTIES LABELS "unit")
endif()
