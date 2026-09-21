#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

#define VK_USE_PLATFORM_WIN32
// #define VOLK_NO_DEVICE_PROTOTYPES
#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// #include <ktx.h>

#ifdef USE_SLANG_RUNTIME_COMPILER
#include <slang/slang.h>
#include <slang/slang-com-ptr.h>
#endif

void CheckVkResult(VkResult res, const char* file, int line) {
  if (res != VK_SUCCESS) {
    throw std::runtime_error(file + std::string(":") + std::to_string(line) + ": failed with VkResult " + std::to_string(res));
  }
}
#define VK_CHECK(x) CheckVkResult(x, __FILE__, __LINE__)

void Check(bool res, const char* file, int line) {
  if (!res) {
    throw std::runtime_error(file + std::string(":") + std::to_string(line) + ": check failed");
  }
}
#define CHECK(x) Check(x, __FILE__, __LINE__)

struct KtxTexture {
  uint32_t baseWidth;
  uint32_t baseHeight;
  uint32_t numLevels;
  VkFormat format;
  uint32_t dataSize;
  void* pData;
  std::vector<size_t> mipOffsets;
};

std::vector<uint8_t> readFile(std::string path) {
  std::ifstream fin(path, std::ios::binary | std::ios::ate);
  if (fin.fail()) {
    throw std::runtime_error("Can't open " + path);
  }
  std::vector<uint8_t> buf(fin.tellg());
  fin.seekg(0);
  fin.read((char*)buf.data(), buf.size());
  if (fin.fail()) {
    throw std::runtime_error("Can't read " + path);
  }
  return buf;
}

KtxTexture ktxTextureFromFile(std::string path) {
  std::vector<uint8_t> buf = readFile(path);
  KtxTexture tex{};
  uint32_t gl_type = *(uint32_t*)&buf[16];
  uint32_t gl_format = *(uint32_t*)&buf[24];
  uint32_t gl_internal_format = *(uint32_t*)&buf[28];
  tex.baseWidth = *(uint32_t*)&buf[36];
  tex.baseHeight = *(uint32_t*)&buf[40];
  tex.numLevels = *(uint32_t*)&buf[56];
  uint32_t bytes_of_key_value_data = *(uint32_t*)&buf[60];
  CHECK(gl_type == 0x1401); // GL_UNSIGNED_BYTE
  CHECK(gl_format == 0x1908); // GL_RGBA
  CHECK(gl_internal_format == 0x8c43); // GL_SRGB8_ALPHA8
  tex.format = VK_FORMAT_R8G8B8A8_SRGB;

  uint32_t cur_width = tex.baseWidth;
  uint32_t cur_height = tex.baseHeight;
  uint32_t total_size = 0;
  for (uint32_t i = 0; i < tex.numLevels; i++) {
    total_size += cur_width * cur_height * 4;
    cur_width = std::max(cur_width / 2, 1u);
    cur_height = std::max(cur_height / 2, 1u);
  }

  tex.pData = malloc(total_size);

  cur_width = tex.baseWidth;
  cur_height = tex.baseHeight;
  uint32_t cur_buf_offset = 64 + bytes_of_key_value_data;
  for (uint32_t mip = 0; mip < tex.numLevels; mip++) {
    uint32_t image_size = *(uint32_t*)&buf[cur_buf_offset];
    CHECK(image_size == cur_width * cur_height * 4);
    memcpy((char*)tex.pData + tex.dataSize, &buf[cur_buf_offset + 4], image_size);
    tex.mipOffsets.push_back(tex.dataSize);
    tex.dataSize += image_size;
    cur_buf_offset += image_size + 4;
    cur_width = std::max(cur_width / 2, 1u);
    cur_height = std::max(cur_height / 2, 1u);
  }
  CHECK(cur_buf_offset == buf.size());

  return tex;
}

VkFormat ktxTexture_GetVkFormat(KtxTexture* tex) {
  return tex->format;
}

typedef uint32_t KTX_error_code;
typedef size_t ktx_size_t;

KTX_error_code ktxTexture_GetImageOffset(KtxTexture* tex, uint32_t mipLevel, uint32_t _0, uint32_t _1, ktx_size_t* mipOffset) {
  *mipOffset = tex->mipOffsets[mipLevel];
  return 0;
}

void ktxTexture_Destroy(KtxTexture* tex) {
  free(tex->pData);
}

struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
};

void run(uint32_t deviceIndex) {
  VK_CHECK(volkInitialize());
  CHECK(SDL_Init(SDL_INIT_VIDEO));
  CHECK(SDL_Vulkan_LoadLibrary(NULL));

  VkApplicationInfo appInfo{
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = "htv",
    .apiVersion = VK_API_VERSION_1_3,
  };
  uint32_t instanceExtensionsCount = 0;
  const char* const* instanceExtensions = SDL_Vulkan_GetInstanceExtensions(&instanceExtensionsCount);
  // const char* const* instanceExtensions = nullptr;
  VkInstanceCreateInfo instanceInfo{
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &appInfo,
    // .enabledExtensionCount = 0,
    // .ppEnabledLayerNames = nullptr,
    .enabledExtensionCount = instanceExtensionsCount,
    .ppEnabledExtensionNames = instanceExtensions,
  };
  VkInstance instance;
  VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &instance));
  volkLoadInstanceOnly(instance);

  uint32_t deviceCount;
  VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr));
  if (deviceIndex >= deviceCount) {
    throw std::runtime_error("Invalid device index: " + std::to_string(deviceIndex));
  }
  std::vector<VkPhysicalDevice> devices(deviceCount);
  VK_CHECK(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()));
  VkPhysicalDeviceProperties2 deviceProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
  vkGetPhysicalDeviceProperties2(devices[deviceIndex], &deviceProperties);
  std::cout << "Selected: " << deviceProperties.properties.deviceName << std::endl;

  uint32_t queueFamilyCount;
  vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, queueFamilies.data());
  uint32_t queueFamily = 0;
  for (size_t i = 0; i < queueFamilies.size(); i++) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queueFamily = i;
      break;
    }
  }
  CHECK(SDL_Vulkan_GetPresentationSupport(instance, devices[deviceIndex], queueFamily));

  const float qfpriorities = 1.0f;
  VkDeviceQueueCreateInfo queueInfo{
    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = queueFamily,
    .queueCount = 1,
    .pQueuePriorities = &qfpriorities
  };
  const std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  VkPhysicalDeviceVulkan12Features enabledVk12Features{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    .descriptorIndexing = true,
    .shaderSampledImageArrayNonUniformIndexing = true,
    .descriptorBindingVariableDescriptorCount = true,
    .runtimeDescriptorArray = true,
    .bufferDeviceAddress = true
  };
  VkPhysicalDeviceVulkan13Features enabledVk13Features{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    .pNext = &enabledVk12Features,
    .synchronization2 = true,
    .dynamicRendering = true,
  };
  VkPhysicalDeviceFeatures enabledVk10Features{
    .samplerAnisotropy = VK_TRUE
  };
  VkDeviceCreateInfo deviceInfo{
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = &enabledVk13Features,
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &queueInfo,
    .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
    .ppEnabledExtensionNames = deviceExtensions.data(),
    .pEnabledFeatures = &enabledVk10Features
  };
  VkDevice device;
  VK_CHECK(vkCreateDevice(devices[deviceIndex], &deviceInfo, nullptr, &device));
  volkLoadDevice(device);
  VkQueue queue;
  vkGetDeviceQueue(device, queueFamily, 0, &queue);

  VmaVulkanFunctions vkFunctions{
    .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
    .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    .vkCreateImage = vkCreateImage
  };
  VmaAllocatorCreateInfo allocatorInfo{
    .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
    .physicalDevice = devices[deviceIndex],
    .device = device,
    .pVulkanFunctions = &vkFunctions,
    .instance = instance
  };
  VmaAllocator allocator;
  VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));

  SDL_Window* window = SDL_CreateWindow("How to Vulkan", 1280u, 720u, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    throw std::runtime_error("SDL_CreateWindow() failed");
  }
  VkSurfaceKHR surface;
  CHECK(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));
  uint32_t windowWidth, windowHeight;
  CHECK(SDL_GetWindowSize(window, (int*)&windowWidth, (int*)&windowHeight));
  VkSurfaceCapabilitiesKHR surfaceCaps;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCaps));

  VkExtent2D swapchainExtent{ surfaceCaps.currentExtent };
  if (surfaceCaps.currentExtent.width == 0xFFFFFFFF) {
    swapchainExtent = { .width = windowWidth, .height = windowHeight };
  }
  const VkFormat imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
  VkSwapchainCreateInfoKHR swapchainInfo{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface,
    .minImageCount = surfaceCaps.minImageCount,
    .imageFormat = imageFormat,
    .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
    .imageExtent = swapchainExtent,
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = VK_PRESENT_MODE_FIFO_KHR
  };
  VkSwapchainKHR swapchain;
  VK_CHECK(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain));

  uint32_t imageCount;
  VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));
  std::vector<VkImage> swapchainImages;
  std::vector<VkImageView> swapchainImageViews;
  swapchainImages.resize(imageCount);
  swapchainImageViews.resize(imageCount);
  VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()));

  for (auto i = 0; i < imageCount; i++) {
    VkImageViewCreateInfo viewCI{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = swapchainImages[i],
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = imageFormat,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
    };
    VK_CHECK(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
  }

  std::vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;
  for (VkFormat& format : depthFormatList) {
    VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
    vkGetPhysicalDeviceFormatProperties2(devices[deviceIndex], format, &formatProperties);
    if (formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      depthFormat = format;
      break;
    }
  }
  VkImageCreateInfo depthImageInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = depthFormat,
    .extent{.width = windowWidth, .height = windowHeight, .depth = 1 },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VmaAllocationCreateInfo allocInfo{
    .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    .usage = VMA_MEMORY_USAGE_AUTO
  };
  VmaAllocation depthImageAllocation;
  VkImage depthImage;
  VK_CHECK(vmaCreateImage(allocator, &depthImageInfo, &allocInfo, &depthImage, &depthImageAllocation, nullptr));
  VkImageViewCreateInfo depthViewInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = depthImage,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = depthFormat,
    .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
  };
  VkImageView depthImageView;
  VK_CHECK(vkCreateImageView(device, &depthViewInfo, nullptr, &depthImageView));

  // Mesh data
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  if (!tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, "assets/suzanne.obj")) {
    throw std::runtime_error("Can't load .obj");
  }
  const VkDeviceSize indexCount(shapes[0].mesh.indices.size());
  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;
  // Load vertex and index data
  for (auto& index : shapes[0].mesh.indices) {
    Vertex v{
      .pos = { attrib.vertices[index.vertex_index * 3], -attrib.vertices[index.vertex_index * 3 + 1], attrib.vertices[index.vertex_index * 3 + 2] },
      .normal = { attrib.normals[index.normal_index * 3], -attrib.normals[index.normal_index * 3 + 1], attrib.normals[index.normal_index * 3 + 2] },
      .uv = { attrib.texcoords[index.texcoord_index * 2], 1.0 - attrib.texcoords[index.texcoord_index * 2 + 1] }
    };
    vertices.push_back(v);
    indices.push_back(indices.size());
  }
  VkDeviceSize vBufSize = sizeof(Vertex) * vertices.size();
  VkDeviceSize iBufSize = sizeof(uint16_t) * indices.size();
  VkBufferCreateInfo bufferInfo{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = vBufSize + iBufSize,
    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
  };
  VmaAllocationCreateInfo vBufferAllocCI{
    .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .usage = VMA_MEMORY_USAGE_AUTO
  };
  VmaAllocationInfo vBufferAllocInfo;
  VmaAllocation vBufferAllocation;
  VkBuffer vBuffer;
  VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vBufferAllocCI, &vBuffer, &vBufferAllocation, &vBufferAllocInfo));
  memcpy(vBufferAllocInfo.pMappedData, vertices.data(), vBufSize);
  memcpy(((char*)vBufferAllocInfo.pMappedData) + vBufSize, indices.data(), iBufSize);

  constexpr uint32_t maxFramesInFlight = 2;
  struct ShaderDataBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocationInfo;
    VkDeviceSize deviceAddress;
  };
  std::array<ShaderDataBuffer, maxFramesInFlight> shaderDataBuffers;
  struct ShaderData {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model[3];
    glm::vec4 lightPos{ 0.0f, -10.0f, 10.0f, 0.0f };
    uint32_t selected{1};
  } shaderData{};
  for (uint32_t i = 0; i < maxFramesInFlight; i++) {
    VkBufferCreateInfo uBufferCI{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sizeof(ShaderData),
      .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    };
    VmaAllocationCreateInfo uBufferAllocCI{
      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO
    };
    VK_CHECK(vmaCreateBuffer(allocator, &uBufferCI, &uBufferAllocCI, &shaderDataBuffers[i].buffer, &shaderDataBuffers[i].allocation, &shaderDataBuffers[i].allocationInfo));
    VkBufferDeviceAddressInfo uBufferBdaInfo{
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = shaderDataBuffers[i].buffer
    };
    shaderDataBuffers[i].deviceAddress = vkGetBufferDeviceAddress(device, &uBufferBdaInfo);
  }

  VkSemaphoreCreateInfo semaphoreCI{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
  };
  VkFenceCreateInfo fenceCI{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    .flags = VK_FENCE_CREATE_SIGNALED_BIT
  };
  std::vector<VkFence> fences(maxFramesInFlight);
  std::vector<VkSemaphore> imageAcquiredSemaphores(maxFramesInFlight);
  std::vector<VkSemaphore> renderCompleteSemaphores(swapchainImages.size());
  for (uint32_t i = 0; i < maxFramesInFlight; i++) {
    VK_CHECK(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
    VK_CHECK(vkCreateSemaphore(device, &semaphoreCI, nullptr, &imageAcquiredSemaphores[i]));
  }
  for (uint32_t i = 0; i < (uint32_t)renderCompleteSemaphores.size(); i++) {
    VK_CHECK(vkCreateSemaphore(device, &semaphoreCI, nullptr, &renderCompleteSemaphores[i]));
  }

  VkCommandPoolCreateInfo commandPoolCI{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = queueFamily
  };
  VkCommandPool commandPool;
  VK_CHECK(vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool));

  VkCommandBufferAllocateInfo cbAllocCI{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = commandPool,
    .commandBufferCount = maxFramesInFlight
  };
  std::array<VkCommandBuffer, maxFramesInFlight> commandBuffers;
  VK_CHECK(vkAllocateCommandBuffers(device, &cbAllocCI, commandBuffers.data()));

  struct Texture {
    VkImage image;
    VmaAllocation allocation;
    VkImageView view;
    VkSampler sampler;
  };
  std::vector<Texture> textures(3);
  std::vector<VkDescriptorImageInfo> textureDescriptors(textures.size());
  for (uint32_t i = 0; i < (uint32_t)textures.size(); i++) {
    std::string filename = "assets/suzanne" + std::to_string(i) + ".ktx";
    // ktxTexture* ktxTexture = nullptr;
    // ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
    KtxTexture tex = ktxTextureFromFile(filename);
    KtxTexture* ktxTexture = &tex;

    VkImageCreateInfo texImgCI{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = ktxTexture_GetVkFormat(ktxTexture),
      .extent = {.width = ktxTexture->baseWidth, .height = ktxTexture->baseHeight, .depth = 1 },
      .mipLevels = ktxTexture->numLevels,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VmaAllocationCreateInfo texImageAllocCI{ .usage = VMA_MEMORY_USAGE_AUTO };
    VK_CHECK(vmaCreateImage(allocator, &texImgCI, &texImageAllocCI, &textures[i].image, &textures[i].allocation, nullptr));

    VkImageViewCreateInfo texViewCI{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = textures[i].image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = texImgCI.format,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
    };
    VK_CHECK(vkCreateImageView(device, &texViewCI, nullptr, &textures[i].view));

    VkBuffer imgSrcBuffer;
    VmaAllocation imgSrcAllocation;
    VkBufferCreateInfo imgSrcBufferCI{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = (uint32_t)ktxTexture->dataSize,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo imgSrcAllocCI{
      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO
    };
    VmaAllocationInfo imgSrcAllocInfo;
    VK_CHECK(vmaCreateBuffer(allocator, &imgSrcBufferCI, &imgSrcAllocCI, &imgSrcBuffer, &imgSrcAllocation, &imgSrcAllocInfo));
    memcpy(imgSrcAllocInfo.pMappedData, ktxTexture->pData, ktxTexture->dataSize);

    VkFenceCreateInfo fenceOneTimeCI{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };
    VkFence fenceOneTime;
    VK_CHECK(vkCreateFence(device, &fenceOneTimeCI, nullptr, &fenceOneTime));

    VkCommandBuffer cbOneTime;
    VkCommandBufferAllocateInfo cbOneTimeAI{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .commandBufferCount = 1
    };
    VK_CHECK(vkAllocateCommandBuffers(device, &cbOneTimeAI, &cbOneTime));

    VkCommandBufferBeginInfo cbOneTimeBI{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(cbOneTime, &cbOneTimeBI));
    VkImageMemoryBarrier2 barrierTexImage{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
      .srcAccessMask = VK_ACCESS_2_NONE,
      .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .image = textures[i].image,
      .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
    };
    VkDependencyInfo barrierTexInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrierTexImage
    };
    vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
    std::vector<VkBufferImageCopy> copyRegions;
    for (auto j = 0; j < ktxTexture->numLevels; j++) {
      ktx_size_t mipOffset = 0;
      KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, j, 0, 0, &mipOffset);
      copyRegions.push_back({
        .bufferOffset = mipOffset,
        .imageSubresource{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = (uint32_t)j, .layerCount = 1},
        .imageExtent{.width = ktxTexture->baseWidth >> j, .height = ktxTexture->baseHeight >> j, .depth = 1 },
      });
    }
    vkCmdCopyBufferToImage(cbOneTime, imgSrcBuffer, textures[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
    VkImageMemoryBarrier2 barrierTexRead{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
      .image = textures[i].image,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = ktxTexture->numLevels, .layerCount = 1 }
    };
    barrierTexInfo.pImageMemoryBarriers = &barrierTexRead;
    vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
    VK_CHECK(vkEndCommandBuffer(cbOneTime));
    VkCommandBufferSubmitInfo cbOneTimeSubmitInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cbOneTime
    };
    VkSubmitInfo2 oneTimeSI{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cbOneTimeSubmitInfo
    };
    VK_CHECK(vkQueueSubmit2(queue, 1, &oneTimeSI, fenceOneTime));
    VK_CHECK(vkWaitForFences(device, 1, &fenceOneTime, VK_TRUE, UINT64_MAX));

    VkSamplerCreateInfo samplerCI{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .anisotropyEnable = VK_TRUE,
      .maxAnisotropy = 8.0f, // 8 is a widely supported value for max anisotropy
      .maxLod = (float)ktxTexture->numLevels,
    };
    VK_CHECK(vkCreateSampler(device, &samplerCI, nullptr, &textures[i].sampler));

    ktxTexture_Destroy(ktxTexture);
    textureDescriptors[i] = {
      .sampler = textures[i].sampler,
      .imageView = textures[i].view,
      .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
    };

    vkDestroyFence(device, fenceOneTime, nullptr);
    vkFreeCommandBuffers(device, commandPool, 1, &cbOneTime);
    vmaDestroyBuffer(allocator, imgSrcBuffer, imgSrcAllocation);
  }

  VkDescriptorBindingFlags descVariableFlag = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
  VkDescriptorSetLayoutBindingFlagsCreateInfo descBindingFlags{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
    .bindingCount = 1,
    .pBindingFlags = &descVariableFlag
  };
  VkDescriptorSetLayoutBinding descLayoutBindingTex{
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = static_cast<uint32_t>(textures.size()),
    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
  };
  VkDescriptorSetLayoutCreateInfo descLayoutTexCI{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .pNext = &descBindingFlags,
    .bindingCount = 1,
    .pBindings = &descLayoutBindingTex
  };
  VkDescriptorSetLayout descriptorSetLayoutTex;
  VK_CHECK(vkCreateDescriptorSetLayout(device, &descLayoutTexCI, nullptr, &descriptorSetLayoutTex));

  VkDescriptorPoolSize poolSize{
    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = static_cast<uint32_t>(textures.size())
  };
  VkDescriptorPoolCreateInfo descPoolCI{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 1,
    .poolSizeCount = 1,
    .pPoolSizes = &poolSize
  };
  VkDescriptorPool descriptorPool;
  VK_CHECK(vkCreateDescriptorPool(device, &descPoolCI, nullptr, &descriptorPool));

  uint32_t variableDescCount = static_cast<uint32_t>(textures.size());
  VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAI{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
    .descriptorSetCount = 1,
    .pDescriptorCounts = &variableDescCount
  };
  VkDescriptorSetAllocateInfo texDescSetAlloc{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .pNext = &variableDescCountAI,
    .descriptorPool = descriptorPool,
    .descriptorSetCount = 1,
    .pSetLayouts = &descriptorSetLayoutTex
  };
  VkDescriptorSet descriptorSetTex;
  VK_CHECK(vkAllocateDescriptorSets(device, &texDescSetAlloc, &descriptorSetTex));

  VkWriteDescriptorSet writeDescSet{
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = descriptorSetTex,
    .dstBinding = 0,
    .descriptorCount = static_cast<uint32_t>(textureDescriptors.size()),
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo = textureDescriptors.data()
  };
  vkUpdateDescriptorSets(device, 1, &writeDescSet, 0, nullptr);

#ifdef USE_SLANG_RUNTIME_COMPILER
  Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
  slang::createGlobalSession(slangGlobalSession.writeRef());
  auto slangTargets{ std::to_array<slang::TargetDesc>({ {
    .format = SLANG_SPIRV,
    .profile = slangGlobalSession->findProfile("spirv_1_4")
  } })};
  auto slangOptions{ std::to_array<slang::CompilerOptionEntry>({ {
      slang::CompilerOptionName::EmitSpirvDirectly,
      {slang::CompilerOptionValueKind::Int, 1}
  } })};
  slang::SessionDesc slangSessionDesc{
    .targets = slangTargets.data(),
    .targetCount = SlangInt(slangTargets.size()),
    .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
    .compilerOptionEntries = slangOptions.data(),
    .compilerOptionEntryCount = (uint32_t)slangOptions.size()
  };
  Slang::ComPtr<slang::ISession> slangSession;
  slangGlobalSession->createSession(slangSessionDesc, slangSession.writeRef());
  Slang::ComPtr<slang::IModule> slangModule{
    slangSession->loadModuleFromSource("triangle", "assets/shader.slang", nullptr, nullptr)
  };
  Slang::ComPtr<ISlangBlob> spirv;
  std::cout << "1111" << std::endl;
  slangModule->getTargetCode(0, spirv.writeRef());
  std::cout << "2222" << std::endl;

  VkShaderModuleCreateInfo shaderModuleCI{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = spirv->getBufferSize(),
    .pCode = (uint32_t*)spirv->getBufferPointer()
  };
#else
  std::vector<uint8_t> spirv = readFile("shader.spirv");
  VkShaderModuleCreateInfo shaderModuleCI{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = spirv.size(),
    .pCode = (uint32_t*)spirv.data()
  };
#endif
  VkShaderModule shaderModule;
  VK_CHECK(vkCreateShaderModule(device, &shaderModuleCI, nullptr, &shaderModule));

  VkPushConstantRange pushConstantRange{
    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    .size = sizeof(VkDeviceAddress)
  };
  VkPipelineLayoutCreateInfo pipelineLayoutCI{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1,
    .pSetLayouts = &descriptorSetLayoutTex,
    .pushConstantRangeCount = 1,
    .pPushConstantRanges = &pushConstantRange
  };
  VkPipelineLayout pipelineLayout;
  VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

  VkVertexInputBindingDescription vertexBinding{
    .binding = 0,
    .stride = sizeof(Vertex),
    .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
  };
  std::vector<VkVertexInputAttributeDescription> vertexAttributes{
    { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
    { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
    { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
  };
  VkPipelineVertexInputStateCreateInfo vertexInputState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &vertexBinding,
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
    .pVertexAttributeDescriptions = vertexAttributes.data(),
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
  };

  std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
    { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = shaderModule, .pName = "main"},
    { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = shaderModule, .pName = "main" }
  };

  VkPipelineViewportStateCreateInfo viewportState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .scissorCount = 1
  };

  std::vector<VkDynamicState> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dynamicState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = 2,
    .pDynamicStates = dynamicStates.data()
  };

  VkPipelineDepthStencilStateCreateInfo depthStencilState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .depthTestEnable = VK_TRUE,
    .depthWriteEnable = VK_TRUE,
    .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
  };

  VkPipelineRenderingCreateInfo renderingCI{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &imageFormat,
    .depthAttachmentFormat = depthFormat
  };

  VkPipelineColorBlendAttachmentState blendAttachment{
    .colorWriteMask = 0xF
  };

  VkPipelineColorBlendStateCreateInfo colorBlendState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &blendAttachment
  };

  VkPipelineRasterizationStateCreateInfo rasterizationState{
     .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
     .lineWidth = 1.0f
  };

  VkPipelineMultisampleStateCreateInfo multisampleState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
  };

  VkGraphicsPipelineCreateInfo pipelineCI{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext = &renderingCI,
    .stageCount = (uint32_t)shaderStages.size(),
    .pStages = shaderStages.data(),
    .pVertexInputState = &vertexInputState,
    .pInputAssemblyState = &inputAssemblyState,
    .pViewportState = &viewportState,
    .pRasterizationState = &rasterizationState,
    .pMultisampleState = &multisampleState,
    .pDepthStencilState = &depthStencilState,
    .pColorBlendState = &colorBlendState,
    .pDynamicState = &dynamicState,
    .layout = pipelineLayout,
  };
  VkPipeline pipeline;
  VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline));

  uint64_t lastTime = SDL_GetTicks();
  bool quit = false;
  uint32_t frameIndex = 0;
  glm::vec3 camPos{ 0.0f, 0.0f, -6.0f };
  glm::vec3 objectRotations[3]{};
  bool updateSwapchain = false;
  while (!quit) {
    VK_CHECK(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));
    VK_CHECK(vkResetFences(device, 1, &fences[frameIndex]));

    uint32_t imageIndex;
    VkResult err = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAcquiredSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
      updateSwapchain = true;
    } else {
      VK_CHECK(err);
    }

    shaderData.projection = glm::perspective(glm::radians(45.0f), (float)windowWidth / (float)windowHeight, 0.1f, 32.0f);
    shaderData.view = glm::translate(glm::mat4(1.0f), camPos);
    for (int i = 0; i < 3; i++) {
      glm::vec3 instancePos = glm::vec3((float)(i - 1) * 3.0f, 0.0f, 0.0f);
      shaderData.model[i] = glm::translate(glm::mat4(1.0f), instancePos) * glm::mat4_cast(glm::quat(objectRotations[i]));
    }
    memcpy(shaderDataBuffers[frameIndex].allocationInfo.pMappedData, &shaderData, sizeof(ShaderData));

    VkCommandBuffer cb = commandBuffers[frameIndex];
    VK_CHECK(vkResetCommandBuffer(cb, 0));

    VkCommandBufferBeginInfo cbBI{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    VK_CHECK(vkBeginCommandBuffer(cb, &cbBI));

    std::array<VkImageMemoryBarrier2, 2> outputBarriers{
      VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image = swapchainImages[imageIndex],
        .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
      },
      VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image = depthImage,
        .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
      }
    };
    VkDependencyInfo barrierDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 2,
      .pImageMemoryBarriers = outputBarriers.data()
    };
    vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);

    VkRenderingAttachmentInfo colorAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = swapchainImageViews[imageIndex],
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue{.color{ 0.0f, 0.0f, 0.2f, 1.0f }}
    };
    VkRenderingAttachmentInfo depthAttachmentInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = depthImageView,
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .clearValue = {.depthStencil = {1.0f,  0}}
    };
    VkRenderingInfo renderingInfo{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea{.extent{.width = windowWidth, .height = windowHeight }},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachmentInfo,
      .pDepthAttachment = &depthAttachmentInfo
    };
    vkCmdBeginRendering(cb, &renderingInfo);

    VkViewport vp{
      .width = static_cast<float>(windowWidth),
      .height = static_cast<float>(windowHeight),
      .minDepth = 0.0f,
      .maxDepth = 1.0f
    };
    vkCmdSetViewport(cb, 0, 1, &vp);

    VkRect2D scissor{ .extent{ .width = windowWidth, .height = windowHeight } };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSetTex, 0, nullptr);
    VkDeviceSize vOffset = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &vBuffer, &vOffset);
    vkCmdBindIndexBuffer(cb, vBuffer, vBufSize, VK_INDEX_TYPE_UINT16);
    vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkDeviceAddress), &shaderDataBuffers[frameIndex].deviceAddress);

    vkCmdDrawIndexed(cb, indexCount, 3, 0, 0, 0);

    vkCmdEndRendering(cb);

    VkImageMemoryBarrier2 barrierPresent{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = 0,
      .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .image = swapchainImages[imageIndex],
      .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo barrierPresentDependencyInfo{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrierPresent
    };
    vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);

    vkEndCommandBuffer(cb);

    VkSemaphoreSubmitInfo waitSemaphoreInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = imageAcquiredSemaphores[frameIndex],
      .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkCommandBufferSubmitInfo commandBufferSubmitInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cb
    };
    VkSemaphoreSubmitInfo signalSemaphoreInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = renderCompleteSemaphores[imageIndex],
      .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSubmitInfo2 submitInfo{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = 1,
      .pWaitSemaphoreInfos = &waitSemaphoreInfo,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &commandBufferSubmitInfo,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos = &signalSemaphoreInfo,
    };
    VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, fences[frameIndex]));

    VkPresentInfoKHR presentInfo{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &renderCompleteSemaphores[imageIndex],
      .swapchainCount = 1,
      .pSwapchains = &swapchain,
      .pImageIndices = &imageIndex
    };
    err = vkQueuePresentKHR(queue, &presentInfo);
    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
      updateSwapchain = true;
    } else {
      VK_CHECK(err);
    }

    float elapsedTime = (SDL_GetTicks() - lastTime) / 1000.0f;
    lastTime = SDL_GetTicks();
    for (SDL_Event event; SDL_PollEvent(&event);) {
      // Exit loop if the application is about to close
      if (event.type == SDL_EVENT_QUIT) {
        quit = true;
        break;
      }

      // Rotate the selected object with mouse drag
      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          objectRotations[shaderData.selected].x -= (float)event.motion.yrel * elapsedTime;
          objectRotations[shaderData.selected].y += (float)event.motion.xrel * elapsedTime;
        }
      }

      // Zooming with the mouse wheel
      if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        camPos.z += (float)event.wheel.y * elapsedTime * 10.0f;
      }

      // Select active model instance
      if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_L) {
          shaderData.selected = (shaderData.selected < 2) ? shaderData.selected + 1 : 0;
        }
        if (event.key.key == SDLK_H) {
          shaderData.selected = (shaderData.selected > 0) ? shaderData.selected - 1 : 2;
        }
      }

      // Window resize
      if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        CHECK(SDL_GetWindowSize(window, (int*)&windowWidth, (int*)&windowHeight));
        updateSwapchain = true;
      }
    }

    if (updateSwapchain) {
      updateSwapchain = false;
      VK_CHECK(vkDeviceWaitIdle(device));
      VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCaps));
      swapchainInfo.oldSwapchain = swapchain;
      swapchainInfo.imageExtent = { .width = windowWidth, .height = windowHeight };
      VK_CHECK(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain));
      for (uint32_t i = 0; i < imageCount; i++) {
        vkDestroyImageView(device, swapchainImageViews[i], nullptr);
      }
      VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr));
      swapchainImages.resize(imageCount);
      VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()));
      swapchainImageViews.resize(imageCount);
      for (auto i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewCI{
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image = swapchainImages[i],
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = imageFormat,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
        };
        VK_CHECK(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews[i]));
      }
      for (auto& semaphore : renderCompleteSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
      }
      renderCompleteSemaphores.resize(imageCount);
      for (auto& semaphore : renderCompleteSemaphores) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore));
      }
      vkDestroySwapchainKHR(device, swapchainInfo.oldSwapchain, nullptr);
      vmaDestroyImage(allocator, depthImage, depthImageAllocation);
      vkDestroyImageView(device, depthImageView, nullptr);
      depthImageInfo.extent = { .width = windowWidth, .height = windowHeight, .depth = 1 };
      VmaAllocationCreateInfo allocCI{
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
      };
      VK_CHECK(vmaCreateImage(allocator, &depthImageInfo, &allocCI, &depthImage, &depthImageAllocation, nullptr));
      VkImageViewCreateInfo viewCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = depthFormat,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
      };
      VK_CHECK(vkCreateImageView(device, &viewCI, nullptr, &depthImageView));
    }

    frameIndex = (frameIndex + 1) % maxFramesInFlight;
  }

  vkDeviceWaitIdle(device);
  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  vkDestroyShaderModule(device, shaderModule, nullptr);
  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  vkDestroyDescriptorSetLayout(device, descriptorSetLayoutTex, nullptr);
  for (uint32_t i = 0; i < (uint32_t)textures.size(); i++) {
    vkDestroySampler(device, textures[i].sampler, nullptr);
    vkDestroyImageView(device, textures[i].view, nullptr);
    vmaDestroyImage(allocator, textures[i].image, textures[i].allocation);
  }
  vkFreeCommandBuffers(device, commandPool, maxFramesInFlight, commandBuffers.data());
  vkDestroyCommandPool(device, commandPool, nullptr);
  for (uint32_t i = 0; i < (uint32_t)renderCompleteSemaphores.size(); i++) {
    vkDestroySemaphore(device, renderCompleteSemaphores[i], nullptr);
  }
  for (uint32_t i = 0; i < maxFramesInFlight; i++) {
    vkDestroyFence(device, fences[i], nullptr);
    vkDestroySemaphore(device, imageAcquiredSemaphores[i], nullptr);
  }
  for (uint32_t i = 0; i < maxFramesInFlight; i++) {
    vmaDestroyBuffer(allocator, shaderDataBuffers[i].buffer, shaderDataBuffers[i].allocation);
  }
  vmaDestroyBuffer(allocator, vBuffer, vBufferAllocation);
  vkDestroyImageView(device, depthImageView, nullptr);
  vmaDestroyImage(allocator, depthImage, depthImageAllocation);
  for (auto i = 0; i < imageCount; i++) {
    vkDestroyImageView(device, swapchainImageViews[i], nullptr);
  }
  vkDestroySwapchainKHR(device, swapchain, nullptr);
  SDL_Vulkan_DestroySurface(instance, surface, nullptr);
  vmaDestroyAllocator(allocator);
  vkDestroyDevice(device, nullptr);
  vkDestroyInstance(instance, nullptr);
}

int main(int argc, const char* argv[]) {
  uint32_t deviceIndex = 0;
  if (argc > 1) {
    deviceIndex = std::stoi(argv[1]);
  }

  try {
    run(deviceIndex);
  } catch (std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }

  return 0;
}
