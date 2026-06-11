#pragma once
#include "Window.hpp"
#include "Camera.hpp"
#include "Map.hpp"
#include "GpuBuffer.hpp"
#include "GpuImage.hpp"
#include "PipelineSetup.hpp"
#include "RenderEntity.hpp"

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>
#include <vma/vk_mem_alloc.h>
#include <vector>
#include <string>

namespace engine {

class Renderer {
public:
    Renderer(Window* window, const std::string& exeDir);
    ~Renderer();

    // Prevent copying
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Uploads map geometry and textures to VRAM, and builds Descriptor Sets
    void UploadMap(const Map& map);
    
    // The main Render Loop execution
    void DrawFrame(const Camera& camera, const Map& map, const std::vector<RenderEntity>& renderEntities);

private:
    void InitVulkan();
    void InitSwapchain();
    void InitPipeline();
    void InitSyncStructures();

    Window* m_window;
    std::string m_exeDir;

    engine::GpuImage m_lightmapAtlasTexture;

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
    // Map GPU Data
    // ========================================================================
    engine::GpuBuffer m_vertexBuffer;
    std::vector<engine::GpuImage> m_gpuTextures;
    std::vector<engine::GpuBuffer> m_dynamicIndexBuffers;

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