cmake_minimum_required(VERSION 3.20)
project(caissa_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Tells VS Code where to find headers

if(MSVC)
    add_compile_options(/O2)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_options(-O3 -march=native)
else()
    add_compile_options(-O3)
endif()

# Locate Python and Pybind11 on the system
find_package(Python3 COMPONENTS Interpreter Development.Embed REQUIRED)
find_package(pybind11 CONFIG REQUIRED)
find_package(Threads REQUIRED) # the search runs on its own std::thread

add_executable(caissa
    cpp_engine/main.cpp
    cpp_engine/board.cpp
    cpp_engine/movegen.cpp
    cpp_engine/evaluate.cpp
    cpp_engine/search.cpp
    cpp_engine/tt.cpp
    cpp_engine/py_bridge.cpp
    cpp_engine/uci.cpp
)

target_compile_definitions(caissa PRIVATE CAISSA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Link the Pybind11 embed library to the executable
target_link_libraries(caissa PRIVATE pybind11::embed Threads::Threads)
