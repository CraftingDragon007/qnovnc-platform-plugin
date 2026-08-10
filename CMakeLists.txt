# Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
# SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#####################################################################
## QNoVNC Platform Plugin:
#####################################################################

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(qnovnc VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(QT_NO_PRIVATE_MODULE_WARNING ON)
option(QNOVNC_BUILD_TESTS "Build qnovnc tests" OFF)

set(QT_REQUIRED_PUBLIC_COMPONENTS
    Core Gui Network WebSockets
)
set(QT_REQUIRED_COMMON_PRIVATE_TARGETS
    CorePrivate GuiPrivate
)
set(QT_REQUIRED_PRIVATE_TARGETS_QT6
    ${QT_REQUIRED_COMMON_PRIVATE_TARGETS}
    FbSupportPrivate
)
set(QT_REQUIRED_PRIVATE_TARGETS_QT5
    ${QT_REQUIRED_COMMON_PRIVATE_TARGETS}
    FbSupportPrivate
    FontDatabaseSupportPrivate
    ServiceSupportPrivate
    EventDispatcherSupportPrivate
)
set(QT_QT5_ADDITIONAL_COMPONENTS FbSupport FontDatabaseSupport ServiceSupport EventDispatcherSupport)
set(QT_OPTIONAL_PRIVATE_COMPONENTS InputSupportPrivate)

set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "Set to 5 to force building with Qt5")
set_property(CACHE QT_DEFAULT_MAJOR_VERSION PROPERTY STRINGS 5 6)

set(_qt_package "")
if(QT_DEFAULT_MAJOR_VERSION STREQUAL "6")
    set(_qt_private_components_qt6 ${QT_REQUIRED_PRIVATE_TARGETS_QT6})
    find_package(Qt6 COMPONENTS
        ${QT_REQUIRED_PUBLIC_COMPONENTS}
        ${_qt_private_components_qt6}
        OPTIONAL_COMPONENTS ${QT_OPTIONAL_PRIVATE_COMPONENTS}
        QUIET
    )
    if(Qt6_FOUND)
        set(_qt_package Qt6)
    else()
        message(STATUS "Qt6 not found; falling back to Qt5")
    endif()
elseif(QT_DEFAULT_MAJOR_VERSION STREQUAL "5")
    set(_qt_package Qt5)
else()
    message(FATAL_ERROR "Unsupported QT_DEFAULT_MAJOR_VERSION value: ${QT_DEFAULT_MAJOR_VERSION}")
endif()

if(NOT _qt_package)
    set(_qt_package Qt5)
endif()

set(_qt_private_components_qt5 ${QT_REQUIRED_PRIVATE_TARGETS_QT5})

if(_qt_package STREQUAL "Qt6")
    set(_qt_private_components ${_qt_private_components_qt6})
    if(NOT Qt6_FOUND)
        find_package(Qt6 COMPONENTS
            ${QT_REQUIRED_PUBLIC_COMPONENTS}
            ${_qt_private_components}
            OPTIONAL_COMPONENTS ${QT_OPTIONAL_PRIVATE_COMPONENTS}
            REQUIRED
        )
    endif()
    set(_qt_major 6)
else()
    set(_qt_private_components ${_qt_private_components_qt5})
    find_package(Qt5 COMPONENTS
        ${QT_REQUIRED_PUBLIC_COMPONENTS}
        ${QT_QT5_ADDITIONAL_COMPONENTS}
        REQUIRED
    )
    set(_qt_major 5)
endif()

if(UNIX)
find_package(ZLIB REQUIRED)
endif()

set(QNOVNC_SOURCES
    main.cpp
    qnovnc.cpp qnovnc_p.h
    qnovncbackingstore.cpp qnovncbackingstore.h
    qnovncclient.cpp qnovncclient.h
    qnovncintegration.cpp qnovncintegration.h
    qnovncscreen.cpp qnovncscreen.h
    qnovncframecache.cpp qnovncframecache.h
    qwebsocketdevice.h
    novnc.json
        qnovncwindow.cpp
        qnovncwindow.h
)

                if(UNIX AND NOT APPLE)
                    find_package(PkgConfig QUIET)
                    if(PkgConfig_FOUND)
                        pkg_check_modules(EGL QUIET IMPORTED_TARGET egl)
                        pkg_check_modules(GL QUIET IMPORTED_TARGET gl)
                    endif()

                    if(EGL_FOUND AND GL_FOUND)
                        list(APPEND QNOVNC_SOURCES
                            qnovncopenglcontext.cpp
                            qnovncopenglcontext.h
                        )
                    endif()
                endif()

if(_qt_major EQUAL 6)
    qt_add_plugin(${PROJECT_NAME} PLUGIN_TYPE platforms
        ${QNOVNC_SOURCES}
    )
else()
    add_library(${PROJECT_NAME} MODULE
        ${QNOVNC_SOURCES}
    )
endif()

if(UNIX)
if(ZLIB_FOUND)
    message(STATUS "Found ZLIB_VERSION: ${ZLIB_VERSION}")
    if("${ZLIB_VERSION}" MATCHES "ng")
        target_compile_definitions(${PROJECT_NAME} PRIVATE ZLIB_NG_FOUND=1)
        message(STATUS "Likely using zlib-ng")
    else()
        target_compile_definitions(${PROJECT_NAME} PRIVATE ZLIB_CLASSIC_FOUND=1)
        message(STATUS "Likely using classic zlib")
    endif()
endif()
endif()

if (WIN32)
	if(Qt6_FOUND)
		get_target_property(_qt_include_dir Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
		target_include_directories(${PROJECT_NAME} PRIVATE "${_qt_include_dir}/QtZlib")
	endif()
endif()
#target_include_directories(${PROJECT_NAME} PRIVATE "C:\\Qt\\Qt-6.10.0\\include\\QtZlib")


if (WIN32)
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${_qt_package}::Core
    ${_qt_package}::Gui
    ${_qt_package}::Network
    ${_qt_package}::WebSockets
)
else()
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${_qt_package}::Core
    ${_qt_package}::Gui
    ${_qt_package}::Network
    ${_qt_package}::WebSockets
    ZLIB::ZLIB
)
endif()

foreach(_qt_private_component IN LISTS _qt_private_components)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${_qt_package}::${_qt_private_component})
endforeach()

if(TARGET ${_qt_package}::InputSupportPrivate)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${_qt_package}::InputSupportPrivate)
endif()

if(EGL_FOUND AND GL_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE QNOVNC_HAS_EGL_MESA=1)
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::EGL PkgConfig::GL)
    target_include_directories(${PROJECT_NAME} PRIVATE ${EGL_INCLUDE_DIRS} ${GL_INCLUDE_DIRS})
    message(STATUS "Enabled OpenGL support via EGL/Mesa")
else()
    message(STATUS "EGL and/or GL not found; OpenGL support disabled")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    QT_PLUGIN_TYPE "platforms"
    QT_PLUGIN_CLASS_NAME "QNoVncIntegrationPlugin"
    QT_PLUGIN_META_DATA_FILE "novnc.json"
)

install(TARGETS ${PROJECT_NAME}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt${_qt_major}/plugins/platforms"
)

if(QNOVNC_BUILD_TESTS AND _qt_major EQUAL 6)
    find_package(Qt6 COMPONENTS Widgets Test REQUIRED)

    qt_add_executable(qnovnc_popupgrab_test
        tests/popupgrabtest.cpp
    )
    target_link_libraries(qnovnc_popupgrab_test PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
    )
    add_dependencies(qnovnc_popupgrab_test ${PROJECT_NAME})
endif()
