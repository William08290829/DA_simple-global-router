cmake_minimum_required(VERSION 3.10)

project(cmake_test)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(flute STATIC
    third_party/flute/flute.c
)

target_include_directories(flute PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/flute
)

add_executable(main
    src/main.cpp
    src/Parser/Parser.cpp
    src/Router/FluteRouter.cpp
    src/Router/Router.cpp
    src/Router/LayerAssignment.cpp
    src/OutputWriter/OutputWriter.cpp
)

target_include_directories(main PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/flute
)

target_link_libraries(main PRIVATE
    flute
)

target_compile_options(main PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

configure_file(
    ${CMAKE_SOURCE_DIR}/third_party/flute/POWV9.dat
    ${CMAKE_BINARY_DIR}/POWV9.dat
    COPYONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/third_party/flute/POST9.dat
    ${CMAKE_BINARY_DIR}/POST9.dat
    COPYONLY
)
