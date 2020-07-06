cmake_minimum_required(VERSION 3.18)

project(qbreakpad LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Core REQUIRED)
find_package(unofficial-breakpad)

set(SOURCES
    qbreakpad.h
    qbreakpad.cpp
)

if(WIN32 AND unofficial-breakpad_FOUND)
    list(APPEND SOURCES windowsdllinterceptor.h)
endif()

add_library(libqbreakpad STATIC ${SOURCES})

if(unofficial-breakpad_FOUND)
    target_compile_definitions(libqbreakpad PRIVATE
        LIBQBREAKPAD_HAS_BREAKPAD=1
    )
    target_link_libraries(libqbreakpad PRIVATE
        unofficial::breakpad::libbreakpad
        unofficial::breakpad::libbreakpad_client
    )
else()
    target_compile_definitions(libqbreakpad PRIVATE
        LIBQBREAKPAD_HAS_BREAKPAD=0
    )
endif()

if(MSVC)
    target_compile_options(libqbreakpad PRIVATE /utf-8)
endif()
target_compile_definitions(libqbreakpad PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)
if(WIN32)
    target_compile_definitions(libqbreakpad PRIVATE
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()
target_link_libraries(libqbreakpad PRIVATE Qt::Core)
target_include_directories(libqbreakpad PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
)
