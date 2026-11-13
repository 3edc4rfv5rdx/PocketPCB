# GUI library: a thin layer over pocketpcb_core. Tests link it too.
add_library(pocketpcb_gui STATIC
    appsettings.h appsettings.cpp
    canvasview.h canvasview.cpp
    format.h format.cpp
    language.h language.cpp
    mainwindow.h mainwindow.cpp
)
target_include_directories(pocketpcb_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pocketpcb_gui PUBLIC pocketpcb_core Qt6::Widgets)
target_compile_definitions(pocketpcb_gui PRIVATE PPCB_VERSION="${PROJECT_VERSION}")

# Translations: .ts files are compiled to .qm and embedded as resources under
# :/i18n. "cmake --build build --target pocketpcb_gui_lupdate" refreshes the
# .ts files from the sources. English is the language of the code and has
# no file until plural forms need one.
qt_add_translations(pocketpcb_gui
    TS_FILES
        ${PROJECT_SOURCE_DIR}/translations/pocketpcb_uk.ts
        ${PROJECT_SOURCE_DIR}/translations/pocketpcb_ru.ts
    RESOURCE_PREFIX "/i18n"
)
