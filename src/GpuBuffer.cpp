#include "GpuBuffer.hpp"
#include <stdexcept>
#include <cstring>

namespace engine {

GpuBuffer CreateAndUploadBuffer(const VulkanContext& ctx, std::span<const std::byte> data, VkBufferUsageFlags usage) {
    size_t bufferSize = data.size();

    // 1. Create the Staging Buffer (CPU Visible)
    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = bufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo = {};
    // Tell VMA we want this memory to be writable by the CPU
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo stagingAllocMeta;

    if (vmaCreateBuffer(ctx.allocator, &stagingBufferInfo, &stagingAllocInfo, 
                        &stagingBuffer, &stagingAllocation, &stagingAllocMeta) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create staging buffer");
    }

    // 2. Copy data to the Staging Buffer
    memcpy(stagingAllocMeta.pMappedData, data.data(), bufferSize);

    // 3. Create the actual GPU Buffer (VRAM - not visible to CPU)
    VkBufferCreateInfo gpuBufferInfo = {};
    gpuBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    gpuBufferInfo.size = bufferSize;
    // It will be used for whatever the user requested PLUS it's a transfer destination
    gpuBufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo gpuAllocInfo = {};
    gpuAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; // Fast VRAM

    GpuBuffer finalBuffer;
    finalBuffer.allocator = ctx.allocator;
    finalBuffer.size = bufferSize;

    if (vmaCreateBuffer(ctx.allocator, &gpuBufferInfo, &gpuAllocInfo, 
                        &finalBuffer.buffer, &finalBuffer.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU buffer");
    }

    // 4. Record and Execute the Copy Command (Immediate Submit)
    VkCommandBufferAllocateInfo cmdAllocInfo = {};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = ctx.commandPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx.device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion = {};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, finalBuffer.buffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    // 5. Submit to Queue and Wait
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    // WARNING: vkQueueWaitIdle is a massive sync point. Perfect for level loading, 
    // but NEVER use this in the per-frame render loop!
    vkQueueWaitIdle(ctx.graphicsQueue);

    // 6. Cleanup Temporary Resources
    vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &cmd);
    vmaDestroyBuffer(ctx.allocator, stagingBuffer, stagingAllocation);

    return finalBuffer;
}

} // namespace engine