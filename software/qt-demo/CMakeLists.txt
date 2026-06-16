cmake_minimum_required(VERSION 3.16)
project(wing-qt-demo VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)

add_executable(wing-qt-demo
    main.cpp
    mainwindow.h
    mainwindow.cpp
)

target_link_libraries(wing-qt-demo PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
