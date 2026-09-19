cmake_minimum_required(VERSION 3.31 FATAL_ERROR)
project(how-to-vulkan)

find_package(Vulkan REQUIRED COMPONENTS volk)

file(DOWNLOAD
  https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/refs/tags/v3.4.0/include/vk_mem_alloc.h
  ${CMAKE_SOURCE_DIR}/external/vma/vk_mem_alloc.h
  EXPECTED_HASH SHA256=8487b7995ad3b263eb73bc5b9a77d71aa69b6bef5d58a715c02d2663afd81f1a
)

set(SDL_VER 3.4.16)
file(DOWNLOAD
  https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL3-devel-${SDL_VER}-VC.zip
  ${CMAKE_SOURCE_DIR}/external/SDL3-devel-${SDL_VER}-VC.zip
  EXPECTED_HASH SHA256=1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de
)
file(ARCHIVE_EXTRACT
  INPUT ${CMAKE_SOURCE_DIR}/external/SDL3-devel-${SDL_VER}-VC.zip
  DESTINATION ${CMAKE_SOURCE_DIR}/external/
)

add_executable(htv main.cpp)

target_include_directories(htv SYSTEM PRIVATE
  ${Vulkan_INCLUDE_DIRS}
  external/vma
)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/external/SDL3-${SDL_VER}")
find_package(SDL3 REQUIRED SDL3-shared)
target_link_libraries(htv PRIVATE SDL3::SDL3)
add_custom_command(TARGET htv POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:htv>
)

target_compile_features(htv PRIVATE cxx_std_20)
