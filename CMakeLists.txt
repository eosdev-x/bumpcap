cmake_minimum_required(VERSION 3.21)

project(Bumpcap
    VERSION 0.1.0
    DESCRIPTION "Browse, install, and manage Linux kernels on Fedora"
    LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(KERNELHUB_BUILD_TESTS "Build unit/integration tests" OFF)
option(KERNELHUB_BUILD_GUI "Build the Qt Widgets GUI" OFF)
option(KERNELHUB_BUILD_CLI "Build the CLI" ON)
option(KERNELHUB_BUILD_HELPER "Build the privileged D-Bus helper" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network DBus Sql Concurrent Svg)

find_package(packagekitqt6 QUIET)
find_package(PolkitQt6-1 QUIET)

if(NOT packagekitqt6_FOUND)
    message(STATUS "packagekitqt6 not found; using raw PackageKit D-Bus backend")
endif()
if(NOT PolkitQt6-1_FOUND)
    message(STATUS "PolkitQt6-1 not found; privileged helper targets are disabled for now")
endif()

add_subdirectory(core)

if(KERNELHUB_BUILD_GUI)
    add_subdirectory(gui)
endif()

if(KERNELHUB_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(KERNELHUB_BUILD_HELPER)
    add_subdirectory(helper)
endif()

if(KERNELHUB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(FILES packaging/org.bumpcap.Bumpcap.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES packaging/org.bumpcap.Bumpcap.appdata.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

install(DIRECTORY packaging/icons/hicolor/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
        FILES_MATCHING PATTERN "*.svg")
