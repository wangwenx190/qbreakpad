cmake_minimum_required(VERSION 3.18)

project(qbreakpad LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
    set(CMAKE_DEBUG_POSTFIX d)
else()
    set(CMAKE_DEBUG_POSTFIX _debug)
endif()

find_package(Qt5 COMPONENTS Core REQUIRED)
find_package(unofficial-breakpad REQUIRED)

set(SOURCES
    qbreakpad_global.h
    qbreakpad.h
    qbreakpad.cpp
)

if(WIN32)
    list(APPEND SOURCES windowsdllinterceptor.h)
endif()

if(WIN32 AND BUILD_SHARED_LIBS)
    enable_language(RC)
    list(APPEND SOURCES qbreakpad.rc)
endif()

add_library(libqbreakpad ${SOURCES})

set_target_properties(libqbreakpad PROPERTIES OUTPUT_NAME QBreakpad)

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(libqbreakpad PUBLIC QBREAKPAD_STATIC)
endif()

if(MSVC)
    target_compile_options(libqbreakpad PRIVATE /utf-8)
endif()
target_compile_definitions(libqbreakpad PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QBREAKPAD_BUILD_LIBRARY
)
if(WIN32)
    target_compile_definitions(libqbreakpad PRIVATE
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()
target_link_libraries(libqbreakpad PRIVATE
    Qt::Core
    unofficial::breakpad::libbreakpad
    unofficial::breakpad::libbreakpad_client
)
target_include_directories(libqbreakpad PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
)
