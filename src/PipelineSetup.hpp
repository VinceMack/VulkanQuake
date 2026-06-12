#pragma once
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <vector>
#include <glm/glm.hpp>

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
    // Defines the binding layout for our textures
    static VkDescriptorSetLayout CreateDescriptorSetLayout(VkDevice device);
    static VkDescriptorSetLayout CreateGlobalDescriptorSetLayout(VkDevice device);
    static VkPipelineLayout CreatePipelineLayout(VkDevice device, const std::vector<VkDescriptorSetLayout>& layouts);
    
    // Bakes the shaders, vertex format, and state into the final GPU state machine
    static VkPipeline CreateGraphicsPipeline(VkDevice device, VkRenderPass renderPass, 
                                             VkPipelineLayout layout, VkShaderModule vertShader, 
                                             VkShaderModule fragShader, VkExtent2D extent);

    // A simple descriptor layout that ONLY takes 1 diffuse texture (no lightmap)
    static VkDescriptorSetLayout CreateSingleTextureDescriptorLayout(VkDevice device);

    // The graphics pipeline tailored for ModelVertex (no lightmap UVs)
    static VkPipeline CreateModelGraphicsPipeline(VkDevice device, VkRenderPass renderPass, 
                                                  VkPipelineLayout layout, VkShaderModule vertShader, 
                                                  VkShaderModule fragShader, VkExtent2D extent);

    // The graphics pipeline tailored for 2D UIVertex (No depth testing, Alpha Blending ON)
    static VkPipeline CreateUIGraphicsPipeline(VkDevice device, VkRenderPass renderPass, 
                                               VkPipelineLayout layout, VkShaderModule vertShader, 
                                               VkShaderModule fragShader, VkExtent2D extent);
};

} // namespace engine