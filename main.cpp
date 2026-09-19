#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define VK_USE_PLATFORM_WIN32
// #define VOLK_NO_DEVICE_PROTOTYPES
#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

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
  int windowWidth, windowHeight;
  SDL_CHECK(SDL_GetWindowSize(window, &windowWidth, &windowHeight));
  VkSurfaceCapabilitiesKHR surfaceCaps;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[deviceIndex], surface, &surfaceCaps));

  VkExtent2D swapchainExtent{ surfaceCaps.currentExtent };
  if (surfaceCaps.currentExtent.width == 0xFFFFFFFF) {
    swapchainExtent = { .width = (uint32_t)windowWidth, .height = (uint32_t)windowHeight };
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
