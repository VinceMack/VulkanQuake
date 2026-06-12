#include "Renderer.hpp"
#include "Shader.hpp"
#include <SDL3/SDL_vulkan.h>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <span>
#include <array>

static std::filesystem::path find_shader_directory(const std::string& filename, const std::filesystem::path& exeDir) {
    std::vector<std::filesystem::path> starts = { std::filesystem::current_path(), exeDir };
    for (const auto& s : starts) {
        if (s.empty()) continue;
        std::filesystem::path p = s;
        while (true) {
            std::filesystem::path cand = p / filename;
            if (std::filesystem::exists(cand) && std::filesystem::is_regular_file(cand)) return cand;
            auto parent = p.parent_path();
            if (parent == p) break; 
            p = parent;
        }
    }
    return filename;
}

namespace engine {

Renderer::Renderer(Window* window, const std::string& exeDir) 
    : m_window(window), m_exeDir(exeDir) {
    InitVulkan();
    InitSwapchain();
    InitPipeline();
    InitSyncStructures();
    InitUIBuffers(); // <--- NEW!
}

Renderer::~Renderer() {
    if (!m_vkbDevice.device) return;
    
    // 1. Wait for the GPU to completely finish the last frame
    vkDeviceWaitIdle(m_vkbDevice.device); 

    // 2. Destroy Framebuffers & Pipelines
    for (auto framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(m_vkbDevice.device, framebuffer, nullptr);
    }
    vkDestroyPipeline(m_vkbDevice.device, m_graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(m_vkbDevice.device, m_pipelineLayout, nullptr);
    vkDestroyPipeline(m_vkbDevice.device, m_modelPipeline, nullptr);
    vkDestroyPipelineLayout(m_vkbDevice.device, m_modelPipelineLayout, nullptr);
    vkDestroyPipeline(m_vkbDevice.device, m_uiPipeline, nullptr);
    vkDestroyDescriptorSetLayout(m_vkbDevice.device, m_modelDescriptorLayout, nullptr);
    if (m_modelDescriptorPool) vkDestroyDescriptorPool(m_vkbDevice.device, m_modelDescriptorPool, nullptr);
    vkDestroyRenderPass(m_vkbDevice.device, m_renderPass, nullptr);
    
    // 3. Destroy Command Pool (This frees all Command Buffers and their references)
    if (m_commandPool) vkDestroyCommandPool(m_vkbDevice.device, m_commandPool, nullptr);

    // 4. Destroy Descriptor Pool (This frees all Descriptor Sets and their references)
    if (m_descriptorPool) vkDestroyDescriptorPool(m_vkbDevice.device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_vkbDevice.device, m_descriptorSetLayout, nullptr);

    // 5. Destroy Images and Buffers
    vkDestroyImageView(m_vkbDevice.device, m_depthBuffer.view, nullptr);
    vmaDestroyImage(m_allocator, m_depthBuffer.image, m_depthBuffer.allocation);

    m_vertexBuffer.Destroy();
    for (auto& buffer : m_dynamicIndexBuffers) {
        buffer.Destroy();
    }
    for (auto& buffer : m_dynamicUIBuffers) {
        buffer.Destroy();
    }
    for (auto& tex : m_gpuTextures) tex.Destroy();
    m_lightmapAtlasTexture.Destroy(); // Safely destroy the Atlas here!
    m_fontTexture.Destroy();
    for (auto& mdl : m_gpuAliasModels) {
        mdl.vertexBuffer.Destroy();
        mdl.indexBuffer.Destroy();
        mdl.texture.Destroy();
    }

    // 6. Destroy Sync Primitives & Swapchain
    for (uint32_t i = 0; i < m_maxFramesInFlight; i++) {
        vkDestroySemaphore(m_vkbDevice.device, m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_vkbDevice.device, m_renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(m_vkbDevice.device, m_inFlightFences[i], nullptr);
    }
    m_vkbSwapchain.destroy_image_views(m_swapchainImageViews);
    vkb::destroy_swapchain(m_vkbSwapchain);

    // 7. Destroy Core Vulkan & VMA
    if (m_allocator) vmaDestroyAllocator(m_allocator);

    vkDestroyShaderModule(m_vkbDevice.device, m_vertShader, nullptr);
    vkDestroyShaderModule(m_vkbDevice.device, m_fragShader, nullptr);
    vkDestroyShaderModule(m_vkbDevice.device, m_modelVertShader, nullptr);
    vkDestroyShaderModule(m_vkbDevice.device, m_modelFragShader, nullptr);
    vkDestroyShaderModule(m_vkbDevice.device, m_uiVertShader, nullptr);
    vkDestroyShaderModule(m_vkbDevice.device, m_uiFragShader, nullptr);

    vkb::destroy_device(m_vkbDevice);
    vkDestroySurfaceKHR(m_vkbInst.instance, m_surface, nullptr);
    vkb::destroy_instance(m_vkbInst);
}

void Renderer::InitVulkan() {
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("VulkanQuake").request_validation_layers(true).use_default_debug_messenger().build();
    if (!inst_ret) throw std::runtime_error("Failed to create Vulkan Instance");
    m_vkbInst = inst_ret.value();

    if (!SDL_Vulkan_CreateSurface(m_window->GetHandle(), m_vkbInst.instance, nullptr, &m_surface)) {
        throw std::runtime_error("Failed to create Vulkan Surface");
    }

    // Downgrade to 1.1 to maximize compatibility across different PCs
    vkb::PhysicalDeviceSelector selector{ m_vkbInst };
    auto phys_ret = selector.set_surface(m_surface).set_minimum_version(1, 1).select();
    if (!phys_ret) throw std::runtime_error("Failed to find suitable GPU: " + phys_ret.error().message());

    vkb::DeviceBuilder device_builder{ phys_ret.value() };
    auto dev_ret = device_builder.build();
    if (!dev_ret) throw std::runtime_error("Failed to build Logical Device");
    m_vkbDevice = dev_ret.value();

    m_graphicsQueue = m_vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_graphicsQueueFamily = m_vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
    if (vkCreateCommandPool(m_vkbDevice.device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    // Safe VMA Initialization
    // Explicitly hand VMA the Vulkan loader functions so it doesn't execute null pointers
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = phys_ret.value().physical_device;
    allocatorInfo.device = m_vkbDevice.device;
    allocatorInfo.instance = m_vkbInst.instance;
    // Set to 1.0 to guarantee standard Vulkan pointers are used safely
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0; 
    allocatorInfo.pVulkanFunctions = &vulkanFunctions; // Pass the safe pointers

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }
}

void Renderer::InitSwapchain() {
    vkb::SwapchainBuilder swapchain_builder{ m_vkbDevice };
    m_vkbSwapchain = swapchain_builder.build().value();
    m_swapchainImages = m_vkbSwapchain.get_images().value();
    m_swapchainImageViews = m_vkbSwapchain.get_image_views().value();
    m_swapchainFormat = m_vkbSwapchain.image_format;
    m_swapchainExtent = m_vkbSwapchain.extent;
}

void Renderer::InitPipeline() {
    std::string vertPath = find_shader_directory("mesh.vert.spv", m_exeDir).string();
    std::string fragPath = find_shader_directory("mesh.frag.spv", m_exeDir).string();
    m_vertShader = engine::Shader::LoadModule(m_vkbDevice.device, vertPath);
    m_fragShader = engine::Shader::LoadModule(m_vkbDevice.device, fragPath);

    std::string mdlVertPath = find_shader_directory("model.vert.spv", m_exeDir).string();
    std::string mdlFragPath = find_shader_directory("model.frag.spv", m_exeDir).string();
    m_modelVertShader = engine::Shader::LoadModule(m_vkbDevice.device, mdlVertPath);
    m_modelFragShader = engine::Shader::LoadModule(m_vkbDevice.device, mdlFragPath);

    m_descriptorSetLayout = engine::PipelineSetup::CreateDescriptorSetLayout(m_vkbDevice.device);
    m_depthBuffer = engine::PipelineSetup::CreateDepthBuffer(m_vkbDevice.device, m_allocator, m_swapchainExtent);
    m_renderPass = engine::PipelineSetup::CreateRenderPass(m_vkbDevice.device, m_swapchainFormat);
    m_framebuffers = engine::PipelineSetup::CreateFramebuffers(m_vkbDevice.device, m_renderPass, m_swapchainExtent, m_swapchainImageViews, m_depthBuffer.view);
    m_pipelineLayout = engine::PipelineSetup::CreatePipelineLayout(m_vkbDevice.device, m_descriptorSetLayout);
    m_graphicsPipeline = engine::PipelineSetup::CreateGraphicsPipeline(m_vkbDevice.device, m_renderPass, m_pipelineLayout, m_vertShader, m_fragShader, m_swapchainExtent);

    m_modelDescriptorLayout = engine::PipelineSetup::CreateSingleTextureDescriptorLayout(m_vkbDevice.device);
    m_modelPipelineLayout = engine::PipelineSetup::CreatePipelineLayout(m_vkbDevice.device, m_modelDescriptorLayout, sizeof(RenderPushConstants));
    m_modelPipeline = engine::PipelineSetup::CreateModelGraphicsPipeline(
        m_vkbDevice.device, m_renderPass, m_modelPipelineLayout, m_modelVertShader, m_modelFragShader, m_swapchainExtent);

    std::string uiVertPath = find_shader_directory("ui.vert.spv", m_exeDir).string();
    std::string uiFragPath = find_shader_directory("ui.frag.spv", m_exeDir).string();
    m_uiVertShader = engine::Shader::LoadModule(m_vkbDevice.device, uiVertPath);
    m_uiFragShader = engine::Shader::LoadModule(m_vkbDevice.device, uiFragPath);

    m_uiPipeline = engine::PipelineSetup::CreateUIGraphicsPipeline(
        m_vkbDevice.device, m_renderPass, m_modelPipelineLayout, m_uiVertShader, m_uiFragShader, m_swapchainExtent);

    // Create descriptor pool for alias models (up to 128 models)
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 128;

    VkDescriptorPoolCreateInfo descPoolInfo{};
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.poolSizeCount = 1;
    descPoolInfo.pPoolSizes = &poolSize;
    descPoolInfo.maxSets = 128;
    if (vkCreateDescriptorPool(m_vkbDevice.device, &descPoolInfo, nullptr, &m_modelDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create model descriptor pool");
    }
}

void Renderer::InitSyncStructures() {
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
}

void Renderer::UploadMap(const Map& map) {
    engine::VulkanContext vkCtx = { m_vkbDevice.device, m_allocator, m_graphicsQueue, m_commandPool };

    auto verticesSpan = std::span(reinterpret_cast<const std::byte*>(map.GetVertices().data()), map.GetVertices().size() * sizeof(engine::RenderVertex));

    m_vertexBuffer = engine::CreateAndUploadBuffer(vkCtx, verticesSpan, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    for (const auto& texData : map.GetTextures()) {
        m_gpuTextures.push_back(engine::CreateAndUploadImage(vkCtx, texData));
    }
    
    std::cout << "Allocating Dynamic Index Buffers...\n";
    size_t indexBufferSize = map.GetMaxIndexCount() * sizeof(uint32_t);
    for (uint32_t i = 0; i < m_maxFramesInFlight; i++) {
        m_dynamicIndexBuffers.push_back(
            engine::CreateDynamicBuffer(vkCtx, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
        );
        // Copy the master indices once at startup so sub-models are populated
        memcpy(m_dynamicIndexBuffers.back().mappedData, 
               map.GetMasterIndices().data(), 
               indexBufferSize);
    }
    


    m_lightmapAtlasTexture = engine::CreateAndUploadImage(vkCtx, map.GetLightmapAtlas());

    // Descriptor Sets for Textures
    if (!m_gpuTextures.empty()) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        // We now need 2 samplers per texture batch (Diffuse + Lightmap)
        poolSize.descriptorCount = static_cast<uint32_t>(m_gpuTextures.size()) * 2;

        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.poolSizeCount = 1;
        descPoolInfo.pPoolSizes = &poolSize;
        descPoolInfo.maxSets = static_cast<uint32_t>(m_gpuTextures.size());
        vkCreateDescriptorPool(m_vkbDevice.device, &descPoolInfo, nullptr, &m_descriptorPool);

        std::vector<VkDescriptorSetLayout> layouts(m_gpuTextures.size(), m_descriptorSetLayout);
        VkDescriptorSetAllocateInfo descAllocInfo{};
        descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descAllocInfo.descriptorPool = m_descriptorPool;
        descAllocInfo.descriptorSetCount = static_cast<uint32_t>(m_gpuTextures.size());
        descAllocInfo.pSetLayouts = layouts.data();

        m_descriptorSets.resize(m_gpuTextures.size());
        vkAllocateDescriptorSets(m_vkbDevice.device, &descAllocInfo, m_descriptorSets.data());

for (size_t i = 0; i < m_gpuTextures.size(); i++) {
            VkDescriptorImageInfo diffuseInfo{};
            diffuseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            diffuseInfo.imageView = m_gpuTextures[i].imageView;
            diffuseInfo.sampler = m_gpuTextures[i].sampler;

            VkDescriptorImageInfo lightmapInfo{};
            lightmapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            lightmapInfo.imageView = m_lightmapAtlasTexture.imageView;
            
            // Note: Lightmaps traditionally use Linear filtering (VK_FILTER_LINEAR) to smooth out shadows
            // Our CreateAndUploadImage hardcodes nearest right now.
            lightmapInfo.sampler = m_lightmapAtlasTexture.sampler; 

            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
            
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = m_descriptorSets[i];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pImageInfo = &diffuseInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = m_descriptorSets[i];
            descriptorWrites[1].dstBinding = 1; // Binding 1!
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &lightmapInfo;

            vkUpdateDescriptorSets(m_vkbDevice.device, 2, descriptorWrites.data(), 0, nullptr);
        }
    }
}

uint32_t Renderer::UploadAliasModel(const AliasModel& model) {
    engine::VulkanContext vkCtx = { m_vkbDevice.device, m_allocator, m_graphicsQueue, m_commandPool };
    GpuAliasModel gpuModel;

    auto vertSpan = std::span(reinterpret_cast<const std::byte*>(model.GetVertices().data()), model.GetVertices().size() * sizeof(engine::ModelVertex));
    auto indSpan = std::span(reinterpret_cast<const std::byte*>(model.GetIndices().data()), model.GetIndices().size() * sizeof(uint32_t));

    gpuModel.vertexBuffer = engine::CreateAndUploadBuffer(vkCtx, vertSpan, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    gpuModel.indexBuffer = engine::CreateAndUploadBuffer(vkCtx, indSpan, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    gpuModel.texture = engine::CreateAndUploadImage(vkCtx, model.GetTexture());
    gpuModel.indexCount = static_cast<uint32_t>(model.GetIndices().size());

    // Allocate Descriptor Set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_modelDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_modelDescriptorLayout;
    vkAllocateDescriptorSets(m_vkbDevice.device, &allocInfo, &gpuModel.descriptorSet);

    // Bind Image to Descriptor Set
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = gpuModel.texture.imageView;
    imageInfo.sampler = gpuModel.texture.sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = gpuModel.descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_vkbDevice.device, 1, &descriptorWrite, 0, nullptr);

    gpuModel.verticesPerFrame = model.GetVerticesPerFrame();
    gpuModel.numFrames = model.GetNumFrames();

    m_gpuAliasModels.push_back(std::move(gpuModel));
    return static_cast<uint32_t>(m_gpuAliasModels.size() - 1);
}

void Renderer::UploadFont(const TextureData& fontData) {
    engine::VulkanContext vkCtx = { m_vkbDevice.device, m_allocator, m_graphicsQueue, m_commandPool };
    m_fontTexture = engine::CreateAndUploadImage(vkCtx, fontData);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    // ---> FIX: Use the dedicated, properly-sized model pool!
    allocInfo.descriptorPool = m_modelDescriptorPool; 
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_modelDescriptorLayout;
    
    if (vkAllocateDescriptorSets(m_vkbDevice.device, &allocInfo, &m_fontDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate font descriptor set!");
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_fontTexture.imageView;
    imageInfo.sampler = m_fontTexture.sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_fontDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_vkbDevice.device, 1, &descriptorWrite, 0, nullptr);
}

void Renderer::DrawFrame(const Camera& camera, const Map& map, const std::vector<RenderEntity>& renderEntities, 
                         const RenderEntity* viewModel, const std::vector<UIVertex>& uiVertices,
                         float totalTime) {
    vkWaitForFences(m_vkbDevice.device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(m_vkbDevice.device, 1, &m_inFlightFences[m_currentFrame]);

    // PVS CULLING HAPPENS HERE!
    std::vector<uint32_t> visibleIndices;
    std::vector<RenderBatch> visibleBatches;
    map.BuildVisibleBatches(camera.GetPosition(), visibleIndices, visibleBatches);

    // Copy the visible indices to this specific frame's dynamic buffer
    if (!visibleIndices.empty()) {
        memcpy(m_dynamicIndexBuffers[m_currentFrame].mappedData, 
               visibleIndices.data(), 
               visibleIndices.size() * sizeof(uint32_t));
    }

    uint32_t imageIndex;
    vkAcquireNextImageKHR(m_vkbDevice.device, m_vkbSwapchain.swapchain, UINT64_MAX, 
                          m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.1f, 0.05f, 0.1f, 1.0f}}; 
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
    VkBuffer vertexBuffers[] = { m_vertexBuffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    
    // Bind the dynamic index buffer for THIS frame
    vkCmdBindIndexBuffer(cmd, m_dynamicIndexBuffers[m_currentFrame].buffer, 0, VK_INDEX_TYPE_UINT32);

    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(75.0f), (float)m_swapchainExtent.width / (float)m_swapchainExtent.height, 0.1f, 10000.0f);
    proj[1][1] *= -1; 

    // Use the dynamically built batches
    for (const auto& batch : visibleBatches) {
        // For the static map batches
        RenderPushConstants pc{};
        pc.renderMatrix = proj * view;
        pc.cameraPos = camera.GetPosition();
        pc.timeOrInterp = totalTime;
        pc.surfaceType = batch.surfaceType;

        vkCmdPushConstants(cmd, m_pipelineLayout, 
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                           0, sizeof(RenderPushConstants), &pc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[batch.textureId], 0, nullptr);
        vkCmdDrawIndexed(cmd, batch.indexCount, 1, batch.firstIndex, 0, 0);
    }

    // Draw Dynamic Brush Entities (Doors, platforms)
    for (const auto& rent : renderEntities) {
        if (rent.type != EntityModelType::BspBrush) continue;
        if (rent.modelId == 0) continue; // Model 0 was already drawn by the PVS pass!

        // ---> UPDATED: Don't draw invisible entities, UNLESS the developer override is ON
        if (!rent.isVisible && !m_showTriggers) continue;

        const SubModel& subModel = map.GetSubModel(rent.modelId);
        
        // Calculate the specific matrix for THIS door
        glm::mat4 modelMatrix = rent.GetTransformMatrix();
        
        // Draw the door's batches
        for (const auto& batch : subModel.batches) {
            RenderPushConstants pc{};
            pc.renderMatrix = proj * view * modelMatrix;
            pc.cameraPos = camera.GetPosition();
            pc.timeOrInterp = totalTime;
            pc.surfaceType = batch.surfaceType;

            vkCmdPushConstants(cmd, m_pipelineLayout, 
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                               0, sizeof(RenderPushConstants), &pc);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[batch.textureId], 0, nullptr);
            vkCmdDrawIndexed(cmd, batch.indexCount, 1, batch.firstIndex, 0, 0);
        }
    }

    // Draw Alias Models
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipeline);

    for (const auto& rent : renderEntities) {
        if (rent.type != EntityModelType::Alias) continue;
        if (rent.modelId >= m_gpuAliasModels.size()) continue;

        const auto& gpuModel = m_gpuAliasModels[rent.modelId];

        // Safety clamp the frames
        uint32_t frameA = rent.frame % gpuModel.numFrames;
        uint32_t frameB = rent.nextFrame % gpuModel.numFrames;

        // The Magic Zero-Cost Binding: We bind the SAME buffer twice, but with different byte offsets!
        VkBuffer vBuffers[] = { gpuModel.vertexBuffer.buffer, gpuModel.vertexBuffer.buffer };
        VkDeviceSize vOffsets[] = { 
            frameA * gpuModel.verticesPerFrame * sizeof(engine::ModelVertex),
            frameB * gpuModel.verticesPerFrame * sizeof(engine::ModelVertex)
        };
        
        vkCmdBindVertexBuffers(cmd, 0, 2, vBuffers, vOffsets);
        vkCmdBindIndexBuffer(cmd, gpuModel.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        // For the dynamic entities
        RenderPushConstants pc{};
        pc.renderMatrix = proj * view * rent.GetTransformMatrix();
        pc.cameraPos = camera.GetPosition();
        pc.timeOrInterp = rent.interp;
        pc.surfaceType = 0; // Models don't use liquid warping or sky scrolling
        
        vkCmdPushConstants(cmd, m_modelPipelineLayout, 
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                           0, sizeof(RenderPushConstants), &pc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 0, 1, &gpuModel.descriptorSet, 0, nullptr);
        vkCmdDrawIndexed(cmd, gpuModel.indexCount, 1, 0, 0, 0);
    }

    // ---> NEW: View Model Pass
    if (viewModel && viewModel->modelId < m_gpuAliasModels.size()) {
        // 1. Clear the Depth Buffer so the gun never clips into walls!
        VkClearAttachment clearDepth{};
        clearDepth.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clearDepth.clearValue.depthStencil = {1.0f, 0};
        
        VkClearRect clearRect{};
        clearRect.rect.offset = {0, 0};
        clearRect.rect.extent = m_swapchainExtent;
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;
        
        vkCmdClearAttachments(cmd, 1, &clearDepth, 1, &clearRect);

        // 2. Bind the View Model Pipeline
        const auto& gpuModel = m_gpuAliasModels[viewModel->modelId];
        VkBuffer vBuffers[] = { gpuModel.vertexBuffer.buffer, gpuModel.vertexBuffer.buffer };
        
        uint32_t frameA = viewModel->frame % gpuModel.numFrames;
        uint32_t frameB = viewModel->nextFrame % gpuModel.numFrames;
        VkDeviceSize vOffsets[] = { 
            frameA * gpuModel.verticesPerFrame * sizeof(engine::ModelVertex),
            frameB * gpuModel.verticesPerFrame * sizeof(engine::ModelVertex)
        };
        
        vkCmdBindVertexBuffers(cmd, 0, 2, vBuffers, vOffsets);
        vkCmdBindIndexBuffer(cmd, gpuModel.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        // 3. The Math Hack: We lock the camera at (0,0,0) looking down the X-axis
        glm::mat4 viewIdent = glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        
        RenderPushConstants pc{};
        pc.renderMatrix = proj * viewIdent * viewModel->GetTransformMatrix();
        pc.cameraPos = camera.GetPosition();
        pc.timeOrInterp = viewModel->interp;
        pc.surfaceType = 0;
        
        vkCmdPushConstants(cmd, m_modelPipelineLayout, 
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                           0, sizeof(RenderPushConstants), &pc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 0, 1, &gpuModel.descriptorSet, 0, nullptr);
        vkCmdDrawIndexed(cmd, gpuModel.indexCount, 1, 0, 0, 0);
    }
    // <--- END NEW

    // ---> NEW: 2D UI Pass
    if (!uiVertices.empty() && m_fontDescriptorSet) {
        // Copy dynamic UI vertices to the CPU-mapped GPU buffer
        memcpy(m_dynamicUIBuffers[m_currentFrame].mappedData, uiVertices.data(), uiVertices.size() * sizeof(engine::UIVertex));

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_uiPipeline);

        VkBuffer uiBuffers[] = { m_dynamicUIBuffers[m_currentFrame].buffer };
        VkDeviceSize uiOffsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, uiBuffers, uiOffsets);

        // Create Ortho Matrix for pixel-perfect 2D rendering (0,0 is top-left)
        glm::mat4 ortho = glm::ortho(0.0f, (float)m_swapchainExtent.width, 0.0f, (float)m_swapchainExtent.height, -1.0f, 1.0f);
        
        RenderPushConstants pc{};
        pc.renderMatrix = ortho;
        pc.cameraPos = glm::vec3(0.0f);
        pc.timeOrInterp = 0.0f;
        pc.surfaceType = 0;
        
        vkCmdPushConstants(cmd, m_modelPipelineLayout, 
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
                           0, sizeof(RenderPushConstants), &pc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_modelPipelineLayout, 0, 1, &m_fontDescriptorSet, 0, nullptr);
        
        // Draw all UI vertices (no index buffer needed for standard arrays)
        vkCmdDraw(cmd, static_cast<uint32_t>(uiVertices.size()), 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

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

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = { m_vkbSwapchain.swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(m_graphicsQueue, &presentInfo);

    m_currentFrame = (m_currentFrame + 1) % m_maxFramesInFlight;
}

void Renderer::InitUIBuffers() {
    engine::VulkanContext vkCtx = { m_vkbDevice.device, m_allocator, m_graphicsQueue, m_commandPool };
    size_t uiBufferSize = 10000 * sizeof(engine::UIVertex); 
    
    for (uint32_t i = 0; i < m_maxFramesInFlight; i++) {
        m_dynamicUIBuffers.push_back(engine::CreateDynamicBuffer(vkCtx, uiBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    }
}

void Renderer::UnloadMap() {
    if (!m_vkbDevice.device) return;
    
    // Stop the GPU so we don't delete memory while it's drawing!
    vkDeviceWaitIdle(m_vkbDevice.device);

    m_vertexBuffer.Destroy();
    
    for (auto& tex : m_gpuTextures) tex.Destroy();
    m_gpuTextures.clear();
    
    m_lightmapAtlasTexture.Destroy();

    for (auto& buf : m_dynamicIndexBuffers) buf.Destroy();
    m_dynamicIndexBuffers.clear();

    if (m_descriptorPool) {
        vkDestroyDescriptorPool(m_vkbDevice.device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_descriptorSets.clear();
}

} // namespace engine