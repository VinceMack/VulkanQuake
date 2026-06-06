#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vma/vk_mem_alloc.h> // VMA
#include <iostream>
#include "VirtualFileSystem.hpp"
#include "Map.hpp"
#include <filesystem>
#include <vector>

static std::filesystem::path find_data_directory(const std::filesystem::path& start, const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> starts = { start, exeDir };
    for (const auto& s : starts) {
        if (s.empty()) continue;
        std::filesystem::path p = s;
        while (true) {
            std::filesystem::path cand = p / "data";
            if (std::filesystem::exists(cand) && std::filesystem::is_directory(cand) &&
                std::filesystem::exists(cand / "pak0.pak")) {
                return cand;
            }
            auto parent = p.parent_path();
            if (parent == p) break;
            p = parent;
        }
    }
    // As a last resort, walk up from current_path()
    std::filesystem::path p = std::filesystem::current_path();
    while (true) {
        std::filesystem::path cand = p / "data";
        if (std::filesystem::exists(cand) && std::filesystem::is_directory(cand) &&
            std::filesystem::exists(cand / "pak0.pak")) {
            return cand;
        }
        auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

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

    // 1. Find the data directory by searching upward from the current path
    //    and from the executable's directory so running from build/Debug works.
    std::filesystem::path exeDir;
    if (argc > 0) {
        try {
            exeDir = std::filesystem::absolute(argv[0]).parent_path();
        } catch (...) {
            exeDir = std::filesystem::current_path();
        }
    }

    std::filesystem::path dataPath = find_data_directory(std::filesystem::current_path(), exeDir);
    if (dataPath.empty()) {
        std::cerr << "WARNING: data directory with pak0.pak not found by search.\n";
        dataPath = std::filesystem::current_path() / "data"; // best-effort fallback
    }

    // 2. Initialize VFS
    engine::vfs::VirtualFileSystem vfs(dataPath);
    std::cout << "Mounting VFS from: " << std::filesystem::absolute(dataPath) << "\n";

    if (vfs.MountPak("pak0.pak")) {
        std::cout << "Successfully mounted pak0.pak!\n";
    } else {
        std::cerr << "ERROR: Could not find or read pak0.pak!\n";
    }

    // 3. Extract the Map and Parse it
    auto mapData = vfs.ReadFile("maps/e1m1.bsp");
    
    if (mapData) {
        std::cout << "Successfully extracted maps/e1m1.bsp (" << mapData->size() << " bytes)\n";
        
        try {
            // Pass the byte buffer to our Map parser
            engine::Map e1m1(*mapData);
            
            // At this point, e1m1.GetVertices() and e1m1.GetIndices() 
            // hold our triangulated Vulkan data!
            
        } catch (const std::exception& e) {
            std::cerr << "Map Parsing Error: " << e.what() << "\n";
        }
    } else {
        std::cerr << "ERROR: Could not find maps/e1m1.bsp inside the VFS!\n";
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