#pragma once
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include "GpuBuffer.hpp" // For VulkanContext
#include "Map.hpp"       // For TextureData

namespace engine {

class GpuImage {
public:
    GpuImage() = default;
    ~GpuImage() { Destroy(); }

    GpuImage(const GpuImage&) = delete;
    GpuImage& operator=(const GpuImage&) = delete;

    GpuImage(GpuImage&& other) noexcept { *this = std::move(other); }
    GpuImage& operator=(GpuImage&& other) noexcept {
        if (this != &other) {
            Destroy();
            image = other.image;
            imageView = other.imageView;
            sampler = other.sampler;
            allocation = other.allocation;
            allocator = other.allocator;
            device = other.device;

            other.image = VK_NULL_HANDLE;
            other.imageView = VK_NULL_HANDLE;
            other.sampler = VK_NULL_HANDLE;
            other.allocation = VK_NULL_HANDLE;
        }
        return *this;
    }

    void Destroy();

    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE; // How the shader reads the image (filtering)
    
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
};

// Converts RGBA pixels into a fully usable Vulkan Texture
GpuImage CreateAndUploadImage(const VulkanContext& ctx, const TextureData& texData);

} // namespace engine