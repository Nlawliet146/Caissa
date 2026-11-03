cmake_minimum_required(VERSION 3.14)
project(caissa_engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/O2)
else()
    add_compile_options(-O3 -march=native)
endif()

add_executable(caissa
    cpp_engine/main.cpp
    cpp_engine/board.cpp
    cpp_engine/movegen.cpp
    cpp_engine/evaluate.cpp
    cpp_engine/search.cpp
)