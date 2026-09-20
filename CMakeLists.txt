cmake_minimum_required(VERSION 3.31 FATAL_ERROR)
project(how-to-vulkan)

option(USE_SLANG_RUNTIME_COMPILER "Compile shader at runtime" OFF)

find_package(Vulkan REQUIRED COMPONENTS volk)

file(DOWNLOAD
  https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/refs/tags/v3.4.0/include/vk_mem_alloc.h
  ${CMAKE_SOURCE_DIR}/external/vma/vk_mem_alloc.h
  EXPECTED_HASH SHA256=8487b7995ad3b263eb73bc5b9a77d71aa69b6bef5d58a715c02d2663afd81f1a
)

file(DOWNLOAD
  https://raw.githubusercontent.com/tinyobjloader/tinyobjloader/refs/tags/v2.0.0rc13/tiny_obj_loader.h
  ${CMAKE_SOURCE_DIR}/external/tinyobjloader/tiny_obj_loader.h
  EXPECTED_HASH SHA256=a55a0933a29caa0e9e84ef62a45cbe8cb25dd0e17bc2850c591c6b06f95d5ebe
)

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/assets/suzanne.obj)
  file(DOWNLOAD
    https://raw.githubusercontent.com/SaschaWillems/HowToVulkan/refs/heads/main/source/assets/suzanne.obj
    ${CMAKE_SOURCE_DIR}/assets/suzanne.obj
  )
endif()

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/assets/suzanne.mtl)
  file(DOWNLOAD
    https://raw.githubusercontent.com/SaschaWillems/HowToVulkan/refs/heads/main/source/assets/suzanne.mtl
    ${CMAKE_SOURCE_DIR}/assets/suzanne.mtl
  )
endif()

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/assets/suzanne0.ktx)
  file(DOWNLOAD
    https://raw.githubusercontent.com/SaschaWillems/HowToVulkan/refs/heads/main/source/assets/suzanne0.ktx
    ${CMAKE_SOURCE_DIR}/assets/suzanne0.ktx
  )
endif()

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/assets/suzanne1.ktx)
  file(DOWNLOAD
    https://raw.githubusercontent.com/SaschaWillems/HowToVulkan/refs/heads/main/source/assets/suzanne1.ktx
    ${CMAKE_SOURCE_DIR}/assets/suzanne1.ktx
  )
endif()

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/assets/suzanne2.ktx)
  file(DOWNLOAD
    https://raw.githubusercontent.com/SaschaWillems/HowToVulkan/refs/heads/main/source/assets/suzanne2.ktx
    ${CMAKE_SOURCE_DIR}/assets/suzanne2.ktx
  )
endif()

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

set(GLM_VER 1.0.3)
file(DOWNLOAD
  https://github.com/g-truc/glm/releases/download/${GLM_VER}/glm-${GLM_VER}.7z
  ${CMAKE_SOURCE_DIR}/external/glm-${GLM_VER}.7z
  EXPECTED_HASH SHA256=df9c59d70d39dfc00903339a2841d46551b88fa55a03e53459c261efc99042f4
)
file(ARCHIVE_EXTRACT
  INPUT ${CMAKE_SOURCE_DIR}/external/glm-${GLM_VER}.7z
  DESTINATION ${CMAKE_SOURCE_DIR}/external/
)

set(SLANG_VER 2026.17.1)
file(DOWNLOAD
  https://github.com/shader-slang/slang/releases/download/v2026.18/slang-2026.18-windows-x86_64.zip
  ${CMAKE_SOURCE_DIR}/external/slang-${SLANG_VER}-windows-x86_64.zip
  EXPECTED_HASH SHA256=6ffa4827b519fd0a85b38407049d87ab0c1f045fe2289cb1e6831f965169f8a1
)
file(ARCHIVE_EXTRACT
  INPUT ${CMAKE_SOURCE_DIR}/external/slang-${SLANG_VER}-windows-x86_64.zip
  DESTINATION ${CMAKE_SOURCE_DIR}/external/slang-${SLANG_VER}
)

add_executable(htv main.cpp)

target_include_directories(htv SYSTEM PRIVATE
  ${Vulkan_INCLUDE_DIRS}
  external/vma
  external/tinyobjloader
  external/glm
)

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/external/SDL3-${SDL_VER}")
find_package(SDL3 REQUIRED SDL3-shared)
target_link_libraries(htv PRIVATE SDL3::SDL3)
add_custom_command(TARGET htv POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:htv>
)

if (USE_SLANG_RUNTIME_COMPILER)
  list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/external/slang-${SLANG_VER}")
  find_package(slang REQUIRED)
  target_link_libraries(htv PRIVATE slang::slang)
  target_compile_definitions(htv PUBLIC USE_SLANG_RUNTIME_COMPILER)
else()
  # add_custom_command(OUTPUT $<TARGET_FILE_DIR:htv>/assets COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:htv>/assets)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader.spirv
    COMMAND slangc assets/shader.slang -target spirv -profile spirv_1_4 -fvk-use-entrypoint-name -o ${CMAKE_CURRENT_BINARY_DIR}//shader.spirv
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS assets/shader.slang
    COMMENT "Compiling shader.slang"
    VERBATIM
  )
  add_custom_target(shader DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/shader.spirv)
  add_dependencies(htv shader)
endif()

target_compile_features(htv PRIVATE cxx_std_20)

add_custom_command(TARGET htv POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/assets/ $<TARGET_FILE_DIR:htv>/assets/
)
