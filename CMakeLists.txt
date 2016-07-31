cmake_minimum_required(VERSION 2.8.12)
project(jet)

if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "CMAKE_BUILD_TYPE not defined, defaulting to Debug build")
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Werror -Wextra -std=c++14 -pedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -DNDEBUG -flto")

if (UBSAN)
    message(STATUS "Compiling with undefined behavior sanitation")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,memory,undefined,safe-stack -fno-sanitize-recover")
endif()

add_subdirectory(src)