cmake_minimum_required(VERSION 3.22)

# The only version source is build_number.txt (see AGENTS.md).
set(PPCB_BUILD_FILE "${CMAKE_CURRENT_SOURCE_DIR}/build_number.txt")
file(STRINGS "${PPCB_BUILD_FILE}" PPCB_VERSION_LINE REGEX "^version=")
string(REGEX REPLACE "^version=" "" PPCB_VERSION "${PPCB_VERSION_LINE}")
if(NOT PPCB_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "build_number.txt: no valid 'version=X.Y.Z' line")
endif()
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

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets LinguistTools Test)
qt_standard_project_setup()

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/gui)
add_subdirectory(src/app)
add_subdirectory(tests)
