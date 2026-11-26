# GUI library: a thin layer over pocketpcb_core. Tests link it too.
add_library(pocketpcb_gui STATIC
    appsettings.h appsettings.cpp
    boardeditor.h boardeditor.cpp
    boardprintdialog.h boardprintdialog.cpp
    bompanel.h bompanel.cpp
    canvasview.h canvasview.cpp
    designviewitem.h designviewitem.cpp
    drcpanel.h drcpanel.cpp
    rulescheck.h rulescheck.cpp
    ercpanel.h ercpanel.cpp
    footprinteditor.h footprinteditor.cpp
    footprintpanels.cpp
    format.h format.cpp
    gerberexport.h gerberexport.cpp
    inplaceedit.h inplaceedit.cpp
    language.h language.cpp
    libraryeditor.h libraryeditor.cpp
    librarypanel.h librarypanel.cpp
    libraryviewitem.h libraryviewitem.cpp
    mainwindow.h mainwindow.cpp
    netspanel.h netspanel.cpp
    projectpropertiesdialog.h projectpropertiesdialog.cpp
    propertiespanel.h propertiespanel.cpp
    rectangleboarddialog.h rectangleboarddialog.cpp
    sheeteditor.h sheeteditor.cpp
    sheettabs.h sheettabs.cpp
    swatches.h swatches.cpp
    toolicons.h toolicons.cpp
    updateboarddialog.h updateboarddialog.cpp
    updatepartsdialog.h updatepartsdialog.cpp
    versionspanel.h versionspanel.cpp
    saveprojectdialog.h saveprojectdialog.cpp
    schematicprint.h schematicprint.cpp
    screensizedialog.h screensizedialog.cpp
)
target_include_directories(pocketpcb_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pocketpcb_gui PUBLIC pocketpcb_core Qt6::Widgets Qt6::PrintSupport)
# Where the program finds the built-in library and the user guide when it
# is not installed.
target_compile_definitions(pocketpcb_gui PRIVATE
    PPCB_SOURCE_LIBRARY_DIR="${PROJECT_SOURCE_DIR}/library"
    PPCB_SOURCE_HELP_FILE="${PROJECT_SOURCE_DIR}/help.html")

# Translations: .ts files are compiled to .qm and embedded as resources under
# :/i18n. "cmake --build build --target pocketpcb_gui_lupdate" refreshes the
# .ts files from the sources. English is the language of the code and has
# no file until plural forms need one.
# Strings come from the core library too, so the sources are listed here
# instead of being taken from this target alone.
file(GLOB PPCB_TRANSLATED_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/core/*.h ${PROJECT_SOURCE_DIR}/src/core/*.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/*.h ${PROJECT_SOURCE_DIR}/src/gui/*.cpp
)
qt_add_translations(pocketpcb_gui
    SOURCES ${PPCB_TRANSLATED_SOURCES}
    TS_FILES
        ${PROJECT_SOURCE_DIR}/translations/pocketpcb_uk.ts
        ${PROJECT_SOURCE_DIR}/translations/pocketpcb_ru.ts
    RESOURCE_PREFIX "/i18n"
)
