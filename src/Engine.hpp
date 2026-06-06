#pragma once
#include "Window.hpp"
#include "Camera.hpp"
#include "GpuBuffer.hpp"
#include "GpuImage.hpp"
#include "Map.hpp"
#include "PipelineSetup.hpp"

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vma/vk_mem_alloc.h>
#include <memory>
#include <vector>

namespace engine {

class Engine {
public:
    Engine();
    ~Engine();

    void Run();

private:
    void Init();
    void MainLoop();
    void Cleanup();

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Camera> m_camera;
    bool m_isRunning = false;

    // ========================================================================
    // Core Vulkan & VMA Handles
    // ========================================================================
    vkb::Instance m_vkbInst;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    vkb::Device m_vkbDevice;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    // ========================================================================
    // Swapchain & Render Targets
    // ========================================================================
    vkb::Swapchain m_vkbSwapchain;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkFormat m_swapchainFormat;
    VkExtent2D m_swapchainExtent;
    engine::DepthBuffer m_depthBuffer;
    std::vector<VkFramebuffer> m_framebuffers;

    // ========================================================================
    // Graphics Pipeline & Shaders
    // ========================================================================
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    VkShaderModule m_vertShader = VK_NULL_HANDLE;
    VkShaderModule m_fragShader = VK_NULL_HANDLE;

    // ========================================================================
    // Map Data
    // ========================================================================
    engine::GpuBuffer m_vertexBuffer;
    engine::GpuBuffer m_indexBuffer;
    std::vector<engine::GpuImage> m_gpuTextures;
    std::vector<engine::RenderBatch> m_renderBatches;

    // ========================================================================
    // Synchronization & Frames in Flight
    // ========================================================================
    uint32_t m_maxFramesInFlight = 0;
    uint32_t m_currentFrame = 0;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    std::vector<VkCommandBuffer> m_commandBuffers;
};

} // namespace engine