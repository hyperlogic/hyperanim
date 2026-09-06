cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
set(PROJECT_NAME play)
project(${PROJECT_NAME} LANGUAGES C)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_C_FLAGS_DEBUG "-g -O0")

# Build application
add_executable(${PROJECT_NAME}
  ../src/arena.c
  ../src/context.c
  ../src/loadjson.c
  ../src/loadgltf.c
  ../src/mathutil.c
  ../src/util.c
  play.c
)

if(UNIX AND NOT APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../vendor)
