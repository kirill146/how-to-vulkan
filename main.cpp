#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>

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

void CheckVkResult(VkResult res, const char* file, int line) {
  if (res != VK_SUCCESS) {
    throw std::runtime_error(file + std::string(":") + std::to_string(line) + ": failed with VkResult " + std::to_string(res));
  }
}
#define VK_CHECK(x) CheckVkResult(x, __FILE__, __LINE__)

void CheckSDLResult(bool res, const char* file, int line) {
  if (!res) {
    throw std::runtime_error(file + std::string(":") + std::to_string(line) + ": SDL call failed");
  }
}
#define SDL_CHECK(x) CheckSDLResult(x, __FILE__, __LINE__)

struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
};

void run(uint32_t deviceIndex) {
  VK_CHECK(volkInitialize());
  SDL_CHECK(SDL_Init(SDL_INIT_VIDEO));
  SDL_CHECK(SDL_Vulkan_LoadLibrary(NULL));

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
  SDL_CHECK(SDL_Vulkan_GetPresentationSupport(instance, devices[deviceIndex], queueFamily));

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
  SDL_CHECK(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));
  uint32_t windowWidth, windowHeight;
  SDL_CHECK(SDL_GetWindowSize(window, (int*)&windowWidth, (int*)&windowHeight));
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
  std::array<VkCommandBuffer, maxFramesInFlight> commandBuffers;
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

  for (uint32_t i = 0; i < maxFramesInFlight; i++) {
    vmaDestroyBuffer(allocator, shaderDataBuffers[i].buffer, shaderDataBuffers[i].allocation);
  }
  vmaDestroyBuffer(allocator, vBuffer, vBufferAllocation);
  vkDestroyImageView(device, depthImageView, nullptr);
  vmaDestroyImage(allocator, depthImage, depthImageAllocation);
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
