cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
set(PROJECT_NAME play)
project(${PROJECT_NAME} LANGUAGES C)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_C_FLAGS_DEBUG "-g -O0")

include(FetchContent)

#
# Raylib
#

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

if(EMSCRIPTEN)
    set(PLATFORM "Web" CACHE STRING "" FORCE)
endif()

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)


# Build application
add_executable(${PROJECT_NAME}
  ../src/util.c
  flycam.c
  play.c
)

if(UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE m raylib)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../vendor)
