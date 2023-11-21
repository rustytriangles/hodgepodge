# What CMake to start at
cmake_minimum_required(VERSION 3.12)

set(OUTPUT_NAME hodgepodge)

project(${OUTPUT_NAME} VERSION 1.0.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

message(${CMAKE_SYSTEM_NAME})
if (${CMAKE_SYSTEM_NAME} STREQUAL "PICO")
  message("****** Building for Pico ******")
  include(pico)

  add_subdirectory(hub75)

else()
  message("****** Building for locally ******")
  include(local)
endif()
