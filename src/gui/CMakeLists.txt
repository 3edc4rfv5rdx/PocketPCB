# GUI library: a thin layer over pocketpcb_core. Tests link it too.
add_library(pocketpcb_gui STATIC
    appsettings.h appsettings.cpp
    canvasview.h canvasview.cpp
    designviewitem.h designviewitem.cpp
    format.h format.cpp
    language.h language.cpp
    librarypanel.h librarypanel.cpp
    libraryviewitem.h libraryviewitem.cpp
    mainwindow.h mainwindow.cpp
    netspanel.h netspanel.cpp
    propertiespanel.h propertiespanel.cpp
    sheeteditor.h sheeteditor.cpp
    saveprojectdialog.h saveprojectdialog.cpp
)
target_include_directories(pocketpcb_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pocketpcb_gui PUBLIC pocketpcb_core Qt6::Widgets)
# Where the program finds the built-in library when it is not installed.
target_compile_definitions(pocketpcb_gui PRIVATE
    PPCB_SOURCE_LIBRARY_DIR="${PROJECT_SOURCE_DIR}/library")

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
