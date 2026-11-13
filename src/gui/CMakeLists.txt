# GUI library: a thin layer over pocketpcb_core. Tests link it too.
add_library(pocketpcb_gui STATIC
    appsettings.h appsettings.cpp
    canvasview.h canvasview.cpp
    format.h format.cpp
    mainwindow.h mainwindow.cpp
)
target_include_directories(pocketpcb_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pocketpcb_gui PUBLIC pocketpcb_core Qt6::Widgets)
target_compile_definitions(pocketpcb_gui PRIVATE PPCB_VERSION="${PROJECT_VERSION}")
