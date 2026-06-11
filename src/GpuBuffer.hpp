#pragma once
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <span>
#include <cstddef>

namespace engine {

class GpuBuffer {
public:
    GpuBuffer() = default;
    ~GpuBuffer() { Destroy(); }

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    GpuBuffer(GpuBuffer&& other) noexcept { *this = std::move(other); }
    
    GpuBuffer& operator=(GpuBuffer&& other) noexcept {
        if (this != &other) {
            Destroy();
            buffer = other.buffer;
            allocation = other.allocation;
            allocator = other.allocator;
            size = other.size;
            mappedData = other.mappedData;

            other.buffer = VK_NULL_HANDLE;
            other.allocation = VK_NULL_HANDLE;
            other.mappedData = nullptr;
        }
        return *this;
    }

    void Destroy() {
        if (buffer != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, buffer, allocation);
            buffer = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
            mappedData = nullptr;
        }
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    size_t size = 0;
    void* mappedData = nullptr; // Pointer to mapped CPU memory
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

// Creates a buffer that is permanently mapped to the CPU for per-frame updates
GpuBuffer CreateDynamicBuffer(const VulkanContext& ctx, size_t size, VkBufferUsageFlags usage);

} // namespace engine