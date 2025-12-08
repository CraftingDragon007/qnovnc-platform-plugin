cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(qnovnc VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(QT_NO_PRIVATE_MODULE_WARNING ON)

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

find_package(ZLIB REQUIRED)

set(QNOVNC_SOURCES
    main.cpp
    qnovnc.cpp qnovnc_p.h
    qnovncclient.cpp qnovncclient.h
    qnovncintegration.cpp qnovncintegration.h
    qnovncscreen.cpp qnovncscreen.h
    qwebsocketdevice.h
    novnc.json
        qnovncwindow.cpp
        qnovncwindow.h
)

if(_qt_major EQUAL 6)
    qt_add_plugin(${PROJECT_NAME} PLUGIN_TYPE platforms
        ${QNOVNC_SOURCES}
    )
else()
    add_library(${PROJECT_NAME} MODULE
        ${QNOVNC_SOURCES}
    )
endif()

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


target_link_libraries(${PROJECT_NAME} PRIVATE
    ${_qt_package}::Core
    ${_qt_package}::Gui
    ${_qt_package}::Network
    ${_qt_package}::WebSockets
    ZLIB::ZLIB
)

foreach(_qt_private_component IN LISTS _qt_private_components)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${_qt_package}::${_qt_private_component})
endforeach()

if(TARGET ${_qt_package}::InputSupportPrivate)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${_qt_package}::InputSupportPrivate)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    QT_PLUGIN_TYPE "platforms"
    QT_PLUGIN_CLASS_NAME "QNoVncIntegrationPlugin"
    QT_PLUGIN_META_DATA_FILE "novnc.json"
)

install(TARGETS ${PROJECT_NAME}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt${_qt_major}/plugins/platforms"
)
