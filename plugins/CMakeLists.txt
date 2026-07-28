# CSV, W3C, NetLog, and CLF have no plugins: their parsers live in the core
# registry (builtin or file-derived) and render in the Plain Text Viewer.
add_subdirectory(annotationviewer)
add_subdirectory(hexdumpviewer)
add_subdirectory(logcatviewer)
if(TARGET Qt6::WebEngineWidgets)
    add_subdirectory(mapviewer)
else()
    message(STATUS "Qt6 WebEngineWidgets not found: skipping the Map Viewer plugin")
endif()
add_subdirectory(plaintextviewer)
add_subdirectory(customformatviewer)
add_subdirectory(selectedlineviewer)
add_subdirectory(timelineviewer)
