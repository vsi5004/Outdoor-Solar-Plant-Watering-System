cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Restrict to only the components needed for host tests.
# Expand this list as new test modules are added.
set(COMPONENTS main unity)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_host)
