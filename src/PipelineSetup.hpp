#pragma once
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <vector>

namespace engine {

struct DepthBuffer {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

class PipelineSetup {
public:
    // Creates the 3D Z-Buffer
    static DepthBuffer CreateDepthBuffer(VkDevice device, VmaAllocator allocator, VkExtent2D extent);
    
    // Describes our color and depth targets
    static VkRenderPass CreateRenderPass(VkDevice device, VkFormat swapchainFormat);
    
    // Connects our physical images to the Render Pass
    static std::vector<VkFramebuffer> CreateFramebuffers(VkDevice device, VkRenderPass renderPass, 
                                                         VkExtent2D extent, 
                                                         const std::vector<VkImageView>& swapchainViews, 
                                                         VkImageView depthView);
    
    // Defines external shader variables (like our Camera Matrix Push Constant)
    static VkPipelineLayout CreatePipelineLayout(VkDevice device);
    
    // Bakes the shaders, vertex format, and state into the final GPU state machine
    static VkPipeline CreateGraphicsPipeline(VkDevice device, VkRenderPass renderPass, 
                                             VkPipelineLayout layout, VkShaderModule vertShader, 
                                             VkShaderModule fragShader, VkExtent2D extent);
};

} // namespace engine