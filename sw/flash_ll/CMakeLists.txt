cmake_minimum_required(VERSION 3.12)
project(flash_ll_sim C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(FLASH_LL_BUILD_TESTS "Build simulation tests" ON)

add_subdirectory(sim)

