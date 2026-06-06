#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vma/vk_mem_alloc.h>
#include <iostream>
#include "VirtualFileSystem.hpp"
#include "Map.hpp"
#include <filesystem>
#include <vector>
#include "GpuBuffer.hpp"
#include "Shader.hpp"
#include "PipelineSetup.hpp"

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

static std::filesystem::path find_shader_directory(const std::string& filename, const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> searchPaths = {
        std::filesystem::current_path() / filename,
        exeDir / filename,
        exeDir.parent_path() / filename // This looks in build/ if the exe is in build/Debug/
    };
    for (const auto& p : searchPaths) {
        if (std::filesystem::exists(p)) return p;
    }
    return filename; // Fallback
}

int main(int argc, char* argv[]) {
    std::cout << "Starting Vulkan Quake Engine..." << std::endl;

    engine::GpuBuffer vertexBuffer;
    engine::GpuBuffer indexBuffer;
    VkShaderModule vertShader = VK_NULL_HANDLE;
    VkShaderModule fragShader = VK_NULL_HANDLE;

    // Initialize SDL3
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to init SDL: " << SDL_GetError() << '\n';
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Vulkan Quake Engine", 
        1280, 720, 
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    // Initialize Vulkan Instance via vk-bootstrap
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

    // Create the Window Surface
    VkSurfaceKHR surface;
    SDL_Vulkan_CreateSurface(window, vkb_inst.instance, nullptr, &surface);

    // Select Physical GPU and create Logical Device
    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_ret = selector.set_surface(surface)
                            .set_minimum_version(1, 2)
                            .select();
    vkb::PhysicalDevice physical_device = phys_ret.value();

    vkb::DeviceBuilder device_builder{ physical_device };
    vkb::Device vkb_device = device_builder.build().value();

    // Get Queue and Command Pool
    VkQueue graphicsQueue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    uint32_t graphicsQueueFamily = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    VkCommandPool commandPool;
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;

    if (vkCreateCommandPool(vkb_device.device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        std::cerr << "Failed to create command pool\n";
        return -1;
    }

    // Initialize VMA
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

    // Build the Context struct for our uploader
    engine::VulkanContext vkCtx = {
        .device = vkb_device.device,
        .allocator = allocator,
        .graphicsQueue = graphicsQueue,
        .commandPool = commandPool
    };

    // Find the data directory by searching upward from the current path
    // and from the executable's directory so running from build/Debug works.
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

    // Initialize VFS
    engine::vfs::VirtualFileSystem vfs(dataPath);
    std::cout << "Mounting VFS from: " << std::filesystem::absolute(dataPath) << "\n";

    if (vfs.MountPak("pak0.pak")) {
        std::cout << "Successfully mounted pak0.pak!\n";
    } else {
        std::cerr << "ERROR: Could not find or read pak0.pak!\n";
    }

    // Extract the Map and Parse it
    auto mapData = vfs.ReadFile("maps/e1m1.bsp");
    
    // Upload the Map's vertex and index data to VRAM using our staging buffer helper function
    if (mapData) {
        try {
            engine::Map e1m1(*mapData);
            
            auto verticesSpan = std::span(reinterpret_cast<const std::byte*>(e1m1.GetVertices().data()), 
                                          e1m1.GetVertices().size() * sizeof(engine::RenderVertex));
                                          
            auto indicesSpan = std::span(reinterpret_cast<const std::byte*>(e1m1.GetIndices().data()), 
                                         e1m1.GetIndices().size() * sizeof(uint32_t));

            std::cout << "Uploading Vertices to VRAM...\n";
            vertexBuffer = engine::CreateAndUploadBuffer(
                vkCtx, verticesSpan, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

            std::cout << "Uploading Indices to VRAM...\n";
            indexBuffer = engine::CreateAndUploadBuffer(
                vkCtx, indicesSpan, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

            std::cout << "SUCCESS! Map geometry is now sitting in ultra-fast VRAM.\n";
            
        } catch (const std::exception& e) {
            std::cerr << "Map Parsing Error: " << e.what() << "\n";
        }
    }

    // Load compiled shaders
    std::cout << "Loading Shaders...\n";
    try {
        std::string vertPath = find_shader_directory("mesh.vert.spv", exeDir).string();
        std::string fragPath = find_shader_directory("mesh.frag.spv", exeDir).string();

        vertShader = engine::Shader::LoadModule(vkb_device.device, vertPath);
        fragShader = engine::Shader::LoadModule(vkb_device.device, fragPath);
        std::cout << "Successfully loaded Shader Modules!\n";
    } catch (const std::exception& e) {
        std::cerr << "Shader Loading Error: " << e.what() << "\n";
    }

    // Build the Swapchain via vk-bootstrap
    std::cout << "Building Swapchain...\n";
    vkb::SwapchainBuilder swapchain_builder{ vkb_device };
    vkb::Swapchain vkb_swapchain = swapchain_builder.build().value();
    std::vector<VkImage> swapchain_images = vkb_swapchain.get_images().value();
    std::vector<VkImageView> swapchain_image_views = vkb_swapchain.get_image_views().value();
    VkFormat swapchain_format = vkb_swapchain.image_format;
    VkExtent2D swapchain_extent = vkb_swapchain.extent;

    // Pipeline and Render Targets Setup
    std::cout << "Building Graphics Pipeline...\n";
    
    // Depth Buffer
    engine::DepthBuffer depthBuffer = engine::PipelineSetup::CreateDepthBuffer(
        vkb_device.device, allocator, swapchain_extent);
        
    // Render Pass
    VkRenderPass renderPass = engine::PipelineSetup::CreateRenderPass(
        vkb_device.device, swapchain_format);
        
    // Framebuffers (Binding Images to Render Pass)
    std::vector<VkFramebuffer> framebuffers = engine::PipelineSetup::CreateFramebuffers(
        vkb_device.device, renderPass, swapchain_extent, swapchain_image_views, depthBuffer.view);
        
    // Pipeline Configuration
    VkPipelineLayout pipelineLayout = engine::PipelineSetup::CreatePipelineLayout(vkb_device.device);
    VkPipeline graphicsPipeline = engine::PipelineSetup::CreateGraphicsPipeline(
        vkb_device.device, renderPass, pipelineLayout, vertShader, fragShader, swapchain_extent);

    std::cout << "SUCCESS! GPU State fully baked and ready to draw.\n";

    SDL_Delay(3000); // Hold window open

    // Cleanup (Reverse order of creation!)
    vkDeviceWaitIdle(vkb_device.device); // Make sure the GPU is done before destroying things

    // Destroy Framebuffers
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(vkb_device.device, framebuffer, nullptr);
    }
    vkDestroyPipeline(vkb_device.device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(vkb_device.device, pipelineLayout, nullptr);
    vkDestroyRenderPass(vkb_device.device, renderPass, nullptr);
    
    // Destroy Depth Buffer (This was causing the VMA crash!)
    vkDestroyImageView(vkb_device.device, depthBuffer.view, nullptr);
    vmaDestroyImage(allocator, depthBuffer.image, depthBuffer.allocation);

    // Destroy Swapchain
    vkb_swapchain.destroy_image_views(swapchain_image_views);
    vkb::destroy_swapchain(vkb_swapchain);

    // Destroy Map Buffers
    vertexBuffer.Destroy();
    indexBuffer.Destroy();
    
    // Destroy Command Pool
    vkDestroyCommandPool(vkb_device.device, commandPool, nullptr);
    
    // Destroy Allocator (Now fully empty, so it won't crash!)
    vmaDestroyAllocator(allocator);
    
    // Destroy Shaders Modules
    vkDestroyShaderModule(vkb_device.device, vertShader, nullptr);
    vkDestroyShaderModule(vkb_device.device, fragShader, nullptr);
    
    // Destroy Core Vulkan
    vkb::destroy_device(vkb_device);
    vkDestroySurfaceKHR(vkb_inst.instance, surface, nullptr);
    vkb::destroy_instance(vkb_inst);
    
    // Destroy SDL
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "Vulkan Quake Engine exited cleanly." << std::endl;
    return 0;
}