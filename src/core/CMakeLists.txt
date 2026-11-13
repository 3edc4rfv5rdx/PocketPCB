# Logic library: data model and all logic. QtCore/QtGui only, never QtWidgets.
add_library(pocketpcb_core STATIC
    units.h units.cpp
    version.h version.cpp
    footprint.h footprint.cpp
    graphic.h graphic.cpp
    grid.h grid.cpp
    jsonio.h jsonio.cpp
    objectid.h objectid.cpp
    part.h part.cpp
    point.h
    symbol.h symbol.cpp
)
target_include_directories(pocketpcb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pocketpcb_core PUBLIC Qt6::Core)
# The version is compiled into version.cpp only; everything else calls appVersion().
target_compile_definitions(pocketpcb_core PRIVATE PPCB_VERSION="${PROJECT_VERSION}")
