cmake_minimum_required(VERSION 3.22)

# The only version source is build_number.txt (see AGENTS.md).
set(PPCB_BUILD_FILE "${CMAKE_CURRENT_SOURCE_DIR}/build_number.txt")
file(STRINGS "${PPCB_BUILD_FILE}" PPCB_VERSION_LINE REGEX "^version=")
string(REGEX REPLACE "^version=" "" PPCB_VERSION "${PPCB_VERSION_LINE}")
if(NOT PPCB_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "build_number.txt: no valid 'version=X.Y.Z' line")
endif()
file(STRINGS "${PPCB_BUILD_FILE}" PPCB_BUILD_DATE_LINE REGEX "^build_date=")
string(REGEX REPLACE "^build_date=" "" PPCB_BUILD_DATE "${PPCB_BUILD_DATE_LINE}")
# Re-run configure when the build number changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PPCB_BUILD_FILE}")

project(pocketpcb VERSION ${PPCB_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets PrintSupport LinguistTools Test)
qt_standard_project_setup()
# The install leaves the program's library path alone: the one Qt's setup asks
# for does not fit into a binary built without one, and linuxdeploy writes its
# own into the AppImage.
set(CMAKE_SKIP_INSTALL_RPATH ON)

enable_testing()

# The installed layout, as the AppImage carries it: the program in bin/, the
# built-in library where it looks for it (../share/pocketpcb/library), the
# desktop entry and the icon.
include(GNUInstallDirs)
install(DIRECTORY library/ DESTINATION ${CMAKE_INSTALL_DATADIR}/pocketpcb/library)
install(DIRECTORY help/ DESTINATION ${CMAKE_INSTALL_DATADIR}/pocketpcb/help)
install(FILES res/pocketpcb.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES res/pocketpcb.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

add_subdirectory(src/core)
add_subdirectory(src/gui)
add_subdirectory(src/app)
add_subdirectory(tests)
