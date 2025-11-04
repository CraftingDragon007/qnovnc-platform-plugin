cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(qnovnc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Network WebSockets
    CorePrivate FbSupportPrivate GuiPrivate
)
find_package(Qt6 COMPONENTS InputSupportPrivate)

qt_add_plugin(${PROJECT_NAME} PLUGIN_TYPE platforms
    main.cpp
    qnovnc.cpp qnovnc_p.h
    qnovncclient.cpp qnovncclient.h
    qnovncintegration.cpp qnovncintegration.h
    qnovncscreen.cpp qnovncscreen.h
    qwebsocketdevice.h
    novnc.json
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::WebSockets
    Qt6::CorePrivate
    Qt6::FbSupportPrivate
    Qt6::GuiPrivate
)

if(TARGET Qt6::InputSupportPrivate)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::InputSupportPrivate)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    QT_PLUGIN_TYPE "platforms"
    QT_PLUGIN_CLASS_NAME "QNoVncIntegrationPlugin"
    QT_PLUGIN_META_DATA_FILE "novnc.json"
)

install(TARGETS ${PROJECT_NAME}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/platforms"
)