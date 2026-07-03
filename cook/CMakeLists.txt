cmake_minimum_required(VERSION 3.13 FATAL_ERROR)
set(PROJECT_NAME cook)
project(${PROJECT_NAME} LANGUAGES C)
set(CMAKE_C_STANDARD 11)

# Build application
add_executable(${PROJECT_NAME}
  arena.c
  cook.c
)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
