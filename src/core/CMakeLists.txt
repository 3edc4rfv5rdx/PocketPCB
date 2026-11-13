# Logic library: data model and all logic. QtCore/QtGui only, never QtWidgets.
add_library(pocketpcb_core STATIC
    units.h units.cpp
    grid.h grid.cpp
)
target_include_directories(pocketpcb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pocketpcb_core PUBLIC Qt6::Core)
