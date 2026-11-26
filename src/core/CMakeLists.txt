# Logic library: data model and all logic. QtCore/QtGui only, never QtWidgets.
add_library(pocketpcb_core STATIC
    units.h units.cpp
    autoroute.h autoroute.cpp
    version.h version.cpp
    board.h board.cpp
    boardcopper.h boardcopper.cpp
    boardprint.h boardprint.cpp
    boardedit.h boardedit.cpp
    boardupdate.h boardupdate.cpp
    trackedit.h trackedit.cpp
    bom.h bom.cpp
    document.h document.cpp
    footprint.h footprint.cpp
    footprintedit.h footprintedit.cpp
    gerber.h gerber.cpp
    graphic.h graphic.cpp
    gridroute.h gridroute.cpp
    grid.h grid.cpp
    joins.h
    jsonio.h jsonio.cpp
    libraryfile.h libraryfile.cpp
    objectid.h objectid.cpp
    outline.h outline.cpp
    paint.h paint.cpp
    part.h part.cpp
    point.h
    project.h project.cpp
    ratsnest.h ratsnest.cpp
    projectview.h projectview.cpp
    projectversions.h projectversions.cpp
    library.h library.cpp
    drc.h drc.cpp
    erc.h erc.cpp
    netlist.h netlist.cpp
    schematic.h schematic.cpp
    sheetedit.h sheetedit.cpp
    stephistory.h stephistory.cpp
    strokefont.h strokefont.cpp strokefont_data.cpp
    symbol.h symbol.cpp
    wiring.h wiringdetail.h wiregeometry.cpp wireroute.cpp wirefollow.cpp wireedit.cpp
    zipfile.h zipfile.cpp
)
target_include_directories(pocketpcb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# zlib unpacks the zip archives of the project's versions.
find_package(ZLIB REQUIRED)
target_link_libraries(pocketpcb_core PUBLIC Qt6::Core Qt6::Gui PRIVATE ZLIB::ZLIB)
# The version and the build date are compiled into version.cpp only;
# everything else calls appVersion() and appBuildDate().
target_compile_definitions(pocketpcb_core PRIVATE PPCB_VERSION="${PROJECT_VERSION}"
    PPCB_BUILD_DATE="${PPCB_BUILD_DATE}")
