cmake_minimum_required(VERSION 3.13)
project(admesh)

add_library(admesh STATIC
    connect.cpp
    normals.cpp
    shared.cpp
    stl.h
    stl_io.cpp
    stlinit.cpp
    util.cpp
)

target_link_libraries(admesh PRIVATE boost_headeronly)
