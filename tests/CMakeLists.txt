# SPDX-License-Identifier: MIT
pkg_check_modules(WaylandServer REQUIRED IMPORTED_TARGET wayland-server)
foreach(protocol stereo content_type)
    add_custom_command(OUTPUT ${protocol}-server.h ${protocol}-client.h ${protocol}-code.c
        COMMAND ${WAYLAND_SCANNER} --strict server-header ${${protocol}_xml} ${protocol}-server.h
        COMMAND ${WAYLAND_SCANNER} --strict client-header ${${protocol}_xml} ${protocol}-client.h
        COMMAND ${WAYLAND_SCANNER} --strict private-code ${${protocol}_xml} ${protocol}-code.c
        DEPENDS ${${protocol}_xml} VERBATIM)
endforeach()

add_executable(test-wayland test-wayland.c stereo-code.c content_type-code.c stereo-server.h content_type-server.h content_type-client.h)
target_include_directories(test-wayland PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(test-wayland PRIVATE stereo-declare PkgConfig::Wayland PkgConfig::WaylandServer)
add_test(NAME wayland COMMAND test-wayland)

add_executable(test-x11 test-x11.c)
target_link_libraries(test-x11 PRIVATE stereo-declare X11::X11)
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
    add_test(NAME x11 COMMAND ${XVFB_RUN} -a $<TARGET_FILE:test-x11>)
else()
    message(STATUS "xvfb-run not found: the X11 test is built, not run")
endif()
