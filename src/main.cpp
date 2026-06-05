#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vma/vk_mem_alloc.h> // VMA
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "Starting Vulkan Quake Engine..." << std::endl;

    // 1. Initialize SDL3
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to init SDL: " << SDL_GetError() << '\n';
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Vulkan Quake Engine", 
        1280, 720, 
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    // 2. Initialize Vulkan Instance via vk-bootstrap
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("VulkanQuake")
                           .request_validation_layers(true)
                           .use_default_debug_messenger()
                           .build();

    if (!inst_ret) {
        std::cerr << "Failed to create Vulkan Instance\n";
        return -1;
    }
    vkb::Instance vkb_inst = inst_ret.value();

    // 3. Create the Window Surface
    VkSurfaceKHR surface;
    SDL_Vulkan_CreateSurface(window, vkb_inst.instance, nullptr, &surface);

    // 4. Select Physical GPU and create Logical Device
    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_ret = selector.set_surface(surface)
                            .set_minimum_version(1, 2)
                            .select();
    vkb::PhysicalDevice physical_device = phys_ret.value();

    vkb::DeviceBuilder device_builder{ physical_device };
    vkb::Device vkb_device = device_builder.build().value();

    // 5. Initialize VMA (The moment of truth!)
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physical_device.physical_device;
    allocatorInfo.device = vkb_device.device;
    allocatorInfo.instance = vkb_inst.instance;
    // We tell VMA we are using Vulkan 1.2 at a minimum
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2; 

    VmaAllocator allocator;
    if (vmaCreateAllocator(&allocatorInfo, &allocator) == VK_SUCCESS) {
        std::cout << "SUCCESS! SDL3, Vulkan, and VMA Initialized perfectly.\n";
    }

    SDL_Delay(3000); // Hold window open

    // 6. Cleanup (Reverse order!)
    vmaDestroyAllocator(allocator);
    vkb::destroy_device(vkb_device);
    vkDestroySurfaceKHR(vkb_inst.instance, surface, nullptr);
    vkb::destroy_instance(vkb_inst);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Vulkan Quake Engine exited cleanly." << std::endl;
    return 0;
}