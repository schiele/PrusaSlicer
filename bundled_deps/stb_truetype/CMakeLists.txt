cmake_minimum_required(VERSION 2.8.12)
project(stb_truetype)

add_library(stb_truetype INTERFACE)
target_include_directories(stb_truetype INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
