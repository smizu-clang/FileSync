cmake_minimum_required(VERSION 3.21)
project(FileSync VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Concurrent)

qt_add_executable(FileSync
    main.cpp
    mainwindow.cpp
    mainwindow.h
    syncengine.cpp
    syncengine.h
)

target_link_libraries(FileSync PRIVATE Qt6::Widgets Qt6::Concurrent)
