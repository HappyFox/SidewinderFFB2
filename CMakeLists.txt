cmake_minimum_required(VERSION 3.15...3.26)
set (CMAKE_SYSTEM_VERSION 10.0 CACHE TYPE INTERNAL FORCE)
project(${SKBUILD_PROJECT_NAME} LANGUAGES CXX)



set(PYBIND11_NEWPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(SidewinderFFB2 module.cpp)

install(TARGETS SidewinderFFB2 LIBRARY DESTINATION .)
target_compile_options(SidewinderFFB2 PRIVATE /std:c++14 /permissive- /sdl)
