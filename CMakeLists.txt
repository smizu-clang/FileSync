cmake_minimum_required(VERSION 4.2)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
set(FILESYNC_VERSION "1.0.0" CACHE STRING "FileSync application version")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(FileSync VERSION ${FILESYNC_VERSION} LANGUAGES CXX)

if(MSVC)
    # MSVC exposes the latest draft standard as /std:c++latest, while CMake
    # does not recognize C++26 as a supported dialect for its try_compile checks.
    set(CMAKE_CXX_STANDARD 23)
    add_compile_options(/std:c++latest /utf-8)
else()
    set(CMAKE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_MODULE_STD ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets Concurrent)

qt_add_executable(FileSync
    main.cpp
    mainwindow.cpp
    mainwindow.h
    syncengine.cpp
    syncengine.h
)

qt_add_resources(FileSync filesync_fonts
    PREFIX "/fonts"
    BASE "assets/fonts"
    FILES "assets/fonts/NotoSansJP-Regular.ttf"
)

target_link_libraries(FileSync PRIVATE Qt6::Widgets Qt6::Concurrent)
set_property(TARGET FileSync PROPERTY CXX_MODULE_STD ON)
set_property(TARGET FileSync PROPERTY CXX_SCAN_FOR_MODULES ON)

set_target_properties(FileSync PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)
