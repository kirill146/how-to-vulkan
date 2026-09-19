cmake_minimum_required(VERSION 3.31 FATAL_ERROR)
project(how-to-vulkan)

find_package(Vulkan REQUIRED COMPONENTS volk)

file(DOWNLOAD
  https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/refs/tags/v3.4.0/include/vk_mem_alloc.h
  ${CMAKE_SOURCE_DIR}/external/vma/vk_mem_alloc.h
  EXPECTED_HASH SHA256=8487b7995ad3b263eb73bc5b9a77d71aa69b6bef5d58a715c02d2663afd81f1a
)

add_executable(htv main.cpp)

target_include_directories(htv SYSTEM PRIVATE
  ${Vulkan_INCLUDE_DIRS}
  external/vma
)

target_compile_features(htv PRIVATE cxx_std_20)
