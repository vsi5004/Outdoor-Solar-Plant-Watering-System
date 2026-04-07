cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET esp32c6)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(plant-waterer)
