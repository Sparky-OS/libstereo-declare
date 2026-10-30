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
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import Xlib" RESULT_VARIABLE _xlib_missing OUTPUT_QUIET ERROR_QUIET)
        if(NOT _xlib_missing)
            add_test(NAME python-x11 COMMAND ${XVFB_RUN} -a ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/../python/tests/test_stereo_declare.py)
            set_tests_properties(python-x11 PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:stereo-declare>")
        else()
            message(STATUS "python3-xlib not found: the Python module test is not run")
        endif()
    endif()
else()
    message(STATUS "xvfb-run not found: the X11 test is built, not run")
endif()

add_executable(test-x11-place test-x11-place.c)
target_link_libraries(test-x11-place PRIVATE stereo-declare X11::X11 ${CMAKE_DL_LIBS})
if(XVFB_RUN)
    add_test(NAME x11-place COMMAND ${XVFB_RUN} -a $<TARGET_FILE:test-x11-place>)
    set_tests_properties(x11-place PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:stereo-declare>")
endif()

add_executable(test-x11-child-place test-x11-child-place.c)
target_link_libraries(test-x11-child-place PRIVATE X11::X11 ${CMAKE_DL_LIBS})
if(XVFB_RUN)
    add_test(NAME x11-child-place COMMAND ${XVFB_RUN} -a $<TARGET_FILE:test-x11-child-place>)
    set_tests_properties(x11-child-place PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:stereo-declare>")
endif()

add_executable(test-x11-child test-x11-child.c)
target_link_libraries(test-x11-child PRIVATE stereo-declare X11::X11)
if(XVFB_RUN)
    add_test(NAME x11-child COMMAND ${XVFB_RUN} -a $<TARGET_FILE:test-x11-child>)
endif()

add_executable(test-x11-membership test-x11-membership.c)
target_include_directories(test-x11-membership PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(test-x11-membership PRIVATE X11::X11 ${CMAKE_DL_LIBS})
if(XVFB_RUN)
    add_test(NAME x11-membership COMMAND ${XVFB_RUN} -a $<TARGET_FILE:test-x11-membership>)
    set_tests_properties(x11-membership PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:stereo-declare>")
endif()
