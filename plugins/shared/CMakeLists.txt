set(CMAKE_AUTOMOC ON)

add_library(shared_components STATIC
    linenumberdelegate.cpp
    linenumberdelegate.h
)

target_link_libraries(shared_components
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

target_include_directories(shared_components
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
