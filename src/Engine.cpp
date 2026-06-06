#include "Engine.hpp"
#include "VirtualFileSystem.hpp"
#include "Shader.hpp"
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <span>
#include <SDL3/SDL_vulkan.h>
#include <array>

// Helper functions for locating data and shaders
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
    std::vector<std::filesystem::path> starts = { std::filesystem::current_path(), exeDir };
    for (const auto& s : starts) {
        if (s.empty()) continue;
        std::filesystem::path p = s;
        // Walk upwards until we find the shader file
        while (true) {
            std::filesystem::path cand = p / filename;
            if (std::filesystem::exists(cand) && std::filesystem::is_regular_file(cand)) {
                return cand;
            }
            auto parent = p.parent_path();
            if (parent == p) break; // Hit the root of the drive
            p = parent;
        }
    }
    return filename; // Fallback
}


namespace engine {

Engine::Engine() {
    Init();
}

Engine::~Engine() {
    Cleanup();
}

void Engine::Init() {
    std::cout << "Initializing Engine...\n";
    m_window = std::make_unique<Window>("Vulkan Quake Engine", 1280, 720);
    
    // E1M1 starting room is roughly at (400, 400, 100)
    m_camera = std::make_unique<Camera>(glm::vec3(400.0f, 400.0f, 100.0f));

    // ========================================================================
    // 1. Initialize Vulkan Instance via vk-bootstrap
    // ========================================================================
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("VulkanQuake")
                           .request_validation_layers(true)
                           .use_default_debug_messenger()
                           .build();

    if (!inst_ret) {
        throw std::runtime_error("Failed to create Vulkan Instance");
    }
    m_vkbInst = inst_ret.value();

    // Create the Window Surface
    SDL_Vulkan_CreateSurface(m_window->GetHandle(), m_vkbInst.instance, nullptr, &m_surface);

    // Select Physical GPU and create Logical Device
    vkb::PhysicalDeviceSelector selector{ m_vkbInst };
    auto phys_ret = selector.set_surface(m_surface)
                            .set_minimum_version(1, 2)
                            .select();
    vkb::PhysicalDevice physical_device = phys_ret.value();

    vkb::DeviceBuilder device_builder{ physical_device };
    m_vkbDevice = device_builder.build().value();

    // Get Queue and Command Pool
    m_graphicsQueue = m_vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_graphicsQueueFamily = m_vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

    if (vkCreateCommandPool(m_vkbDevice.device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    // Initialize VMA
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = physical_device.physical_device;
    allocatorInfo.device = m_vkbDevice.device;
    allocatorInfo.instance = m_vkbInst.instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2; 

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) == VK_SUCCESS) {
        std::cout << "SUCCESS! SDL3, Vulkan, and VMA Initialized perfectly.\n";
    } else {
        throw std::runtime_error("Failed to create VMA allocator");
    }

    // Build the Context struct for our uploader
    engine::VulkanContext vkCtx = {
        .device = m_vkbDevice.device,
        .allocator = m_allocator,
        .graphicsQueue = m_graphicsQueue,
        .commandPool = m_commandPool
    };

    // ========================================================================
    // 2. VFS and Map Parsing
    // ========================================================================
    std::filesystem::path exeDir;
    const char* basePath = SDL_GetBasePath(); // SDL3 returns const char*
    if (basePath) {
        exeDir = std::filesystem::path(basePath);
        // No need to call SDL_free in SDL3!
    } else {
        exeDir = std::filesystem::current_path();
    }

    std::filesystem::path dataPath = find_data_directory(std::filesystem::current_path(), exeDir);
    if (dataPath.empty()) {
        std::cerr << "WARNING: data directory with pak0.pak not found by search.\n";
        dataPath = std::filesystem::current_path() / "data"; // best-effort fallback
    }

    engine::vfs::VirtualFileSystem vfs(dataPath);
    std::cout << "Mounting VFS from: " << std::filesystem::absolute(dataPath) << "\n";

    if (vfs.MountPak("pak0.pak")) {
        std::cout << "Successfully mounted pak0.pak!\n";
    } else {
        throw std::runtime_error("ERROR: Could not find or read pak0.pak!");
    }

    // Extract the Palette
    auto paletteData = vfs.ReadFile("gfx/palette.lmp");
    if (!paletteData) {
        throw std::runtime_error("CRITICAL ERROR: Could not find gfx/palette.lmp in pak0.pak!");
    }

    // Extract the Map and Parse it
    auto mapData = vfs.ReadFile("maps/e1m1.bsp");
    if (mapData) {
        engine::Map e1m1(*mapData, *paletteData);
        
        auto verticesSpan = std::span(reinterpret_cast<const std::byte*>(e1m1.GetVertices().data()), 
                                      e1m1.GetVertices().size() * sizeof(engine::RenderVertex));
                                      
        auto indicesSpan = std::span(reinterpret_cast<const std::byte*>(e1m1.GetIndices().data()), 
                                     e1m1.GetIndices().size() * sizeof(uint32_t));

        std::cout << "Uploading Vertices to VRAM...\n";
        m_vertexBuffer = engine::CreateAndUploadBuffer(vkCtx, verticesSpan, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        std::cout << "Uploading Indices to VRAM...\n";
        m_indexBuffer = engine::CreateAndUploadBuffer(vkCtx, indicesSpan, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

        std::cout << "SUCCESS! Map geometry is now sitting in ultra-fast VRAM.\n";

        // Upload Textures
        std::cout << "Uploading " << e1m1.GetTextures().size() << " Textures to VRAM...\n";
        for (const auto& texData : e1m1.GetTextures()) {
            m_gpuTextures.push_back(engine::CreateAndUploadImage(vkCtx, texData));
        }

        // Save the batches for the render loop
        m_renderBatches = e1m1.GetRenderBatches();
    } else {
        throw std::runtime_error("Map Parsing Error: Could not find maps/e1m1.bsp");
    }

    // ========================================================================
    // 3. Load Shaders
    // ========================================================================
    std::cout << "Loading Shaders...\n";
    std::string vertPath = find_shader_directory("mesh.vert.spv", exeDir).string();
    std::string fragPath = find_shader_directory("mesh.frag.spv", exeDir).string();

    m_vertShader = engine::Shader::LoadModule(m_vkbDevice.device, vertPath);
    m_fragShader = engine::Shader::LoadModule(m_vkbDevice.device, fragPath);
    std::cout << "Successfully loaded Shader Modules!\n";

    // ========================================================================
    // 4. Swapchain & Descriptor Sets
    // ========================================================================
    std::cout << "Building Swapchain...\n";
    vkb::SwapchainBuilder swapchain_builder{ m_vkbDevice };
    m_vkbSwapchain = swapchain_builder.build().value();
    m_swapchainImages = m_vkbSwapchain.get_images().value();
    m_swapchainImageViews = m_vkbSwapchain.get_image_views().value();
    m_swapchainFormat = m_vkbSwapchain.image_format;
    m_swapchainExtent = m_vkbSwapchain.extent;

    std::cout << "Building Descriptor Sets...\n";
    m_descriptorSetLayout = engine::PipelineSetup::CreateDescriptorSetLayout(m_vkbDevice.device);

    if (!m_gpuTextures.empty()) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = static_cast<uint32_t>(m_gpuTextures.size());

        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.poolSizeCount = 1;
        descPoolInfo.pPoolSizes = &poolSize;
        descPoolInfo.maxSets = static_cast<uint32_t>(m_gpuTextures.size());

        if (vkCreateDescriptorPool(m_vkbDevice.device, &descPoolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create descriptor pool");
        }

        std::vector<VkDescriptorSetLayout> layouts(m_gpuTextures.size(), m_descriptorSetLayout);
        VkDescriptorSetAllocateInfo descAllocInfo{};
        descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descAllocInfo.descriptorPool = m_descriptorPool;
        descAllocInfo.descriptorSetCount = static_cast<uint32_t>(m_gpuTextures.size());
        descAllocInfo.pSetLayouts = layouts.data();

        m_descriptorSets.resize(m_gpuTextures.size());
        if (vkAllocateDescriptorSets(m_vkbDevice.device, &descAllocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate descriptor sets");
        }

        for (size_t i = 0; i < m_gpuTextures.size(); i++) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = m_gpuTextures[i].imageView;
            imageInfo.sampler = m_gpuTextures[i].sampler;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = m_descriptorSets[i];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(m_vkbDevice.device, 1, &descriptorWrite, 0, nullptr);
        }
    }

    // ========================================================================
    // 5. Graphics Pipeline & Render Targets Setup
    // ========================================================================
    std::cout << "Building Graphics Pipeline...\n";
    m_depthBuffer = engine::PipelineSetup::CreateDepthBuffer(m_vkbDevice.device, m_allocator, m_swapchainExtent);
    m_renderPass = engine::PipelineSetup::CreateRenderPass(m_vkbDevice.device, m_swapchainFormat);
    m_framebuffers = engine::PipelineSetup::CreateFramebuffers(m_vkbDevice.device, m_renderPass, m_swapchainExtent, m_swapchainImageViews, m_depthBuffer.view);
    m_pipelineLayout = engine::PipelineSetup::CreatePipelineLayout(m_vkbDevice.device, m_descriptorSetLayout);
    m_graphicsPipeline = engine::PipelineSetup::CreateGraphicsPipeline(m_vkbDevice.device, m_renderPass, m_pipelineLayout, m_vertShader, m_fragShader, m_swapchainExtent);

    // ========================================================================
    // 6. Synchronization & Frames in Flight
    // ========================================================================
    m_maxFramesInFlight = m_swapchainImages.size();
    m_imageAvailableSemaphores.resize(m_maxFramesInFlight);
    m_renderFinishedSemaphores.resize(m_maxFramesInFlight);
    m_inFlightFences.resize(m_maxFramesInFlight);
    m_commandBuffers.resize(m_maxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = m_maxFramesInFlight;
    vkAllocateCommandBuffers(m_vkbDevice.device, &cmdAllocInfo, m_commandBuffers.data());

    for (uint32_t i = 0; i < m_maxFramesInFlight; i++) {
        vkCreateSemaphore(m_vkbDevice.device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);
        vkCreateSemaphore(m_vkbDevice.device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]);
        vkCreateFence(m_vkbDevice.device, &fenceInfo, nullptr, &m_inFlightFences[i]);
    }

    std::cout << "SUCCESS! GPU State fully baked and ready to draw.\n";
    m_isRunning = true;
}

void Engine::Run() {
    MainLoop();
}

void Engine::MainLoop() {
    std::cout << "Entering Main Loop... Look at the window!\n";
    m_window->CaptureMouse(true);
    uint64_t lastTime = SDL_GetTicks();

    while (m_isRunning) {
        // 1. Process OS Events
        m_window->PollEvents(m_isRunning);

        // 2. Calculate Delta Time
        uint64_t currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        // 3. Process Input
        float mouseX = 0.0f, mouseY = 0.0f;
        SDL_GetRelativeMouseState(&mouseX, &mouseY);
        m_camera->ProcessMouse(mouseX, mouseY);

        const bool* keys = SDL_GetKeyboardState(NULL);
        float moveForward = 0.0f, moveRight = 0.0f;
        if (keys[SDL_SCANCODE_W]) moveForward += 1.0f;
        if (keys[SDL_SCANCODE_S]) moveForward -= 1.0f;
        if (keys[SDL_SCANCODE_D]) moveRight += 1.0f;
        if (keys[SDL_SCANCODE_A]) moveRight -= 1.0f;
        
        m_camera->ProcessKeyboard(moveForward, moveRight, deltaTime);

        // ========================================================================
        // 4. Vulkan Render Loop
        // ========================================================================
        
        // Wait for the GPU to finish THIS SPECIFIC frame before we record
        vkWaitForFences(m_vkbDevice.device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
        vkResetFences(m_vkbDevice.device, 1, &m_inFlightFences[m_currentFrame]);

        // Ask the Swapchain for the next image index
        uint32_t imageIndex;
        vkAcquireNextImageKHR(m_vkbDevice.device, m_vkbSwapchain.swapchain, UINT64_MAX, 
                              m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

        // Begin Command Recording
        VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Begin the Render Pass
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_renderPass;
        renderPassInfo.framebuffer = m_framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_swapchainExtent;

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.1f, 0.05f, 0.1f, 1.0f}}; // Dark Purple void
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // Bind the Pipeline and Map Data
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
        VkBuffer vertexBuffers[] = { m_vertexBuffer.buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        // Calculate Camera Math
        glm::mat4 view = m_camera->GetViewMatrix();
        glm::mat4 proj = glm::perspective(glm::radians(75.0f), 
            (float)m_swapchainExtent.width / (float)m_swapchainExtent.height, 0.1f, 10000.0f);
        proj[1][1] *= -1; // Fix Vulkan Y-down

        glm::mat4 mvp = proj * view;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);

        // THE BATCHED DRAW CALLS!
        for (const auto& batch : m_renderBatches) {
            // Bind the specific texture for this batch
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[batch.textureId], 0, nullptr);
            // Draw only the indices belonging to this texture
            vkCmdDrawIndexed(cmd, batch.indexCount, 1, batch.firstIndex, 0, 0);
        }

        // End Recording
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        // Submit to the GPU Queue
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]);

        // Present the image to the screen
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        VkSwapchainKHR swapchains[] = { m_vkbSwapchain.swapchain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        vkQueuePresentKHR(m_graphicsQueue, &presentInfo);

        // Advance the frame index
        m_currentFrame = (m_currentFrame + 1) % m_maxFramesInFlight;
    }
}

void Engine::Cleanup() {
    std::cout << "Shutting down Engine...\n";
    if (!m_vkbDevice.device) return;

    // Make sure the GPU is done before destroying things
    vkDeviceWaitIdle(m_vkbDevice.device); 

    // Destroy Framebuffers
    for (auto framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_vkbDevice.device, framebuffer, nullptr);
    }
    
    // Destroy Pipeline and RenderPass
    vkDestroyPipeline(m_vkbDevice.device, m_graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(m_vkbDevice.device, m_pipelineLayout, nullptr);
    vkDestroyRenderPass(m_vkbDevice.device, m_renderPass, nullptr);
    
    // Destroy Descriptor Pool and Layout
    if (m_descriptorPool) vkDestroyDescriptorPool(m_vkbDevice.device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_vkbDevice.device, m_descriptorSetLayout, nullptr);

    // Destroy Depth Buffer
    vkDestroyImageView(m_vkbDevice.device, m_depthBuffer.view, nullptr);
    vmaDestroyImage(m_allocator, m_depthBuffer.image, m_depthBuffer.allocation);

    // Destroy Sync Primitives
    for (uint32_t i = 0; i < m_maxFramesInFlight; i++) {
        vkDestroySemaphore(m_vkbDevice.device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_vkbDevice.device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(m_vkbDevice.device, m_inFlightFences[i], nullptr);
    }

    // Destroy Swapchain
    m_vkbSwapchain.destroy_image_views(m_swapchainImageViews);
    vkb::destroy_swapchain(m_vkbSwapchain);

    // Destroy Map Buffers
    m_vertexBuffer.Destroy();
    m_indexBuffer.Destroy();
    
    // Destroy Textures
    for (auto& tex : m_gpuTextures) {
        tex.Destroy();
    }
    
    // Destroy Command Pool
    vkDestroyCommandPool(m_vkbDevice.device, m_commandPool, nullptr);
    
    // Destroy Allocator
    vmaDestroyAllocator(m_allocator);
    
    // Destroy Shaders Modules
    vkDestroyShaderModule(m_vkbDevice.device, m_vertShader, nullptr);
    vkDestroyShaderModule(m_vkbDevice.device, m_fragShader, nullptr);
    
    // Destroy Core Vulkan
    vkb::destroy_device(m_vkbDevice);
    vkDestroySurfaceKHR(m_vkbInst.instance, m_surface, nullptr);
    vkb::destroy_instance(m_vkbInst);
    
    // Smart pointers for Window and Camera will automatically clean up
}

} // namespace engine