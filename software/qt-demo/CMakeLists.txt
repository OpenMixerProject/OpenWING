cmake_minimum_required(VERSION 3.16)
project(wing-qt-demo VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Try to find Qt6 first, fallback to Qt5
find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)
if (Qt6_FOUND)
    message(STATUS "Found Qt6")
    set(QT_LIBRARIES Qt6::Core Qt6::Gui Qt6::Widgets)
else()
    find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)
    message(STATUS "Found Qt5")
    set(QT_LIBRARIES Qt5::Core Qt5::Gui Qt5::Widgets)
endif()

add_executable(wing-qt-demo
    main.cpp
    mainwindow.h
    mainwindow.cpp
)

target_link_libraries(wing-qt-demo PRIVATE ${QT_LIBRARIES})
