# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)
# The version lives in stereo-declare.h, so the header and the package never disagree.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/stereo-declare.h _version_lines REGEX "^#define STEREO_DECLARE_VERSION_(MAJOR|MINOR|PATCH) ")
foreach(_part MAJOR MINOR PATCH)
    string(REGEX REPLACE ".*#define STEREO_DECLARE_VERSION_${_part} ([0-9]+).*" "\\1" _${_part} "${_version_lines}")
endforeach()
project(StereoDeclare VERSION ${_MAJOR}.${_MINOR}.${_PATCH} LANGUAGES C)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(STEREO_DECLARE_WERROR "Treat compiler warnings as errors" OFF)
option(STEREO_DECLARE_EXAMPLES "Build the X11 and Wayland examples" OFF)
set(STEREO_DECLARE_PYTHON_DIR "${CMAKE_INSTALL_DATADIR}/stereo-declare/python" CACHE STRING
    "Where stereo_declare.py is installed (a distribution sets its site-packages folder)")

find_package(PkgConfig REQUIRED)
find_package(X11 REQUIRED COMPONENTS Xpresent Xext Xcomposite)
find_package(Threads REQUIRED)
pkg_check_modules(Wayland REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# kde-stereo-content-v1.xml: inside the plasma-wayland-protocols fork it is ../src/protocols,
# in a release tarball protocol/; STEREO_CONTENT_XML overrides both.
set(STEREO_CONTENT_XML "" CACHE FILEPATH "kde-stereo-content-v1.xml to build against")
if(NOT STEREO_CONTENT_XML)
    foreach(_candidate ${CMAKE_CURRENT_SOURCE_DIR}/protocol/kde-stereo-content-v1.xml
                       ${CMAKE_CURRENT_SOURCE_DIR}/../src/protocols/kde-stereo-content-v1.xml)
        if(EXISTS ${_candidate})
            set(STEREO_CONTENT_XML ${_candidate})
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${STEREO_CONTENT_XML}")
    message(FATAL_ERROR "kde-stereo-content-v1.xml not found; set STEREO_CONTENT_XML")
endif()
message(STATUS "kde-stereo-content-v1: ${STEREO_CONTENT_XML}")
set(stereo_xml ${STEREO_CONTENT_XML})
set(content_type_xml ${WAYLAND_PROTOCOLS_DIR}/staging/content-type/content-type-v1.xml)
foreach(protocol stereo content_type)
    add_custom_command(OUTPUT ${protocol}-client.h ${protocol}-protocol.c
        COMMAND ${WAYLAND_SCANNER} --strict client-header ${${protocol}_xml} ${protocol}-client.h
        COMMAND ${WAYLAND_SCANNER} --strict private-code ${${protocol}_xml} ${protocol}-protocol.c
        DEPENDS ${${protocol}_xml} VERBATIM)
endforeach()

add_library(stereo-declare SHARED stereo-declare.c stereo-presentation.c stereo-x11-surface.c stereo-client.h stereo-protocol.c content_type-client.h content_type-protocol.c)
target_compile_features(stereo-declare PRIVATE c_std_11)
target_compile_options(stereo-declare PRIVATE -Wall -Wextra $<$<BOOL:${STEREO_DECLARE_WERROR}>:-Werror>)
target_include_directories(stereo-declare PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# stereo-declare.h includes <X11/Xlib.h>, so X11 is part of the interface; Wayland is not.
target_link_libraries(stereo-declare PUBLIC X11::X11 PRIVATE PkgConfig::Wayland X11::Xpresent X11::Xext X11::Xcomposite Threads::Threads)
set_target_properties(stereo-declare PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
add_library(StereoDeclare::stereo-declare ALIAS stereo-declare)

install(TARGETS stereo-declare EXPORT StereoDeclareTargets)
install(FILES stereo-declare.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT StereoDeclareTargets NAMESPACE StereoDeclare:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StereoDeclare)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/StereoDeclareConfig.cmake
    "include(CMakeFindDependencyMacro)\nfind_dependency(X11)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/StereoDeclareTargets.cmake\")\n")
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/StereoDeclareConfigVersion.cmake COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/StereoDeclareConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/StereoDeclareConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/StereoDeclare)
configure_file(stereo-declare.pc.in stereo-declare.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/stereo-declare.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

install(FILES python/stereo_declare.py DESTINATION ${STEREO_DECLARE_PYTHON_DIR} COMPONENT python)

if(STEREO_DECLARE_EXAMPLES)
    add_subdirectory(examples)
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
