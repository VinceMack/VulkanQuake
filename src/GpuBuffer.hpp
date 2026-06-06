#pragma once
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <span>
#include <cstddef>

namespace engine {

class GpuBuffer {
public:
    GpuBuffer() = default;
    
    // Deletes the buffer from GPU memory automatically
    ~GpuBuffer() { Destroy(); }

    // Prevent copying, but allow moving
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    
    GpuBuffer(GpuBuffer&& other) noexcept {
        *this = std::move(other);
    }
    
    GpuBuffer& operator=(GpuBuffer&& other) noexcept {
        if (this != &other) {
            Destroy();
            buffer = other.buffer;
            allocation = other.allocation;
            allocator = other.allocator;
            size = other.size;

            other.buffer = VK_NULL_HANDLE;
            other.allocation = VK_NULL_HANDLE;
        }
        return *this;
    }

    void Destroy() {
        if (buffer != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffer, allocation);
            buffer = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
        }
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    size_t size = 0;
};

// A helper structure to hold the Vulkan context needed for uploads
struct VulkanContext {
    VkDevice device;
    VmaAllocator allocator;
    VkQueue graphicsQueue;
    VkCommandPool commandPool;
};

// The function that performs the Staging Buffer transfer
GpuBuffer CreateAndUploadBuffer(
    const VulkanContext& ctx, 
    std::span<const std::byte> data, 
    VkBufferUsageFlags usage
);

} // namespace engine