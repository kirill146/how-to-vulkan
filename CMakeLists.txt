cmake_minimum_required(VERSION 3.25 FATAL_ERROR)
project(how-to-vulkan)

find_package(Vulkan REQUIRED COMPONENTS volk)

add_executable(htv main.cpp)

target_include_directories(htv PRIVATE ${Vulkan_INCLUDE_DIRS})

target_compile_features(htv PRIVATE cxx_std_20)
