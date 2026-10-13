# SPDX-License-Identifier: MIT
# Built with the library (STEREO_DECLARE_EXAMPLES=ON), or on its own against an installed library:
#   cmake -S examples -B build && cmake --build build
cmake_minimum_required(VERSION 3.16)
if(NOT TARGET StereoDeclare::stereo-declare)
    project(stereo-declare-examples LANGUAGES C)
    find_package(StereoDeclare 1.1 CONFIG REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Wayland REQUIRED IMPORTED_TARGET wayland-client)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
endif()

pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(xdg-shell_xml ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
set(viewporter_xml ${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml)
foreach(protocol xdg-shell viewporter)
    add_custom_command(OUTPUT ${protocol}-client.h ${protocol}-protocol.c
        COMMAND ${WAYLAND_SCANNER} --strict client-header ${${protocol}_xml} ${protocol}-client.h
        COMMAND ${WAYLAND_SCANNER} --strict private-code ${${protocol}_xml} ${protocol}-protocol.c
        DEPENDS ${${protocol}_xml} VERBATIM)
endforeach()

add_executable(declare-wayland declare-wayland.c xdg-shell-protocol.c viewporter-protocol.c xdg-shell-client.h viewporter-client.h)
target_include_directories(declare-wayland PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(declare-wayland PRIVATE StereoDeclare::stereo-declare PkgConfig::Wayland)

# X11 comes with the library's CMake package, since its header includes Xlib.h.
add_executable(declare-x11 declare-x11.c)
target_link_libraries(declare-x11 PRIVATE StereoDeclare::stereo-declare)
