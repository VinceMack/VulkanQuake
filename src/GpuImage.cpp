#include "GpuImage.hpp"
#include <stdexcept>
#include <cstring>

namespace engine {

void GpuImage::Destroy() {
    if (device != VK_NULL_HANDLE) {
        if (sampler) vkDestroySampler(device, sampler, nullptr);
        if (imageView) vkDestroyImageView(device, imageView, nullptr);
        if (image && allocator) vmaDestroyImage(allocator, image, allocation);
        
        image = VK_NULL_HANDLE;
        imageView = VK_NULL_HANDLE;
        sampler = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
}

GpuImage CreateAndUploadImage(const VulkanContext& ctx, const TextureData& texData) {
    size_t imageSize = texData.width * texData.height * 4; // 4 bytes per pixel (RGBA)

    // 1. Create a CPU-visible Staging Buffer
    VkBufferCreateInfo stagingBufferInfo = {};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VmaAllocationInfo stagingMeta;
    if (vmaCreateBuffer(ctx.allocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, &stagingMeta) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image staging buffer");
    }

    // Copy pixels to staging buffer
    memcpy(stagingMeta.pMappedData, texData.pixelsRGBA.data(), imageSize);

    // 2. Create the GPU-only Image
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = { texData.width, texData.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VmaAllocationCreateInfo imgAllocInfo = {};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    GpuImage finalImage;
    finalImage.allocator = ctx.allocator;
    finalImage.device = ctx.device;

    if (vmaCreateImage(ctx.allocator, &imageInfo, &imgAllocInfo, &finalImage.image, &finalImage.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU image");
    }

    // 3. Command Recording: Transition Layout -> Copy -> Transition Layout
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

    // Transition A: Undefined -> Transfer Destination
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = finalImage.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy Buffer to Image
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { texData.width, texData.height, 1 };

    vkCmdCopyBufferToImage(cmd, stagingBuffer, finalImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition B: Transfer Destination -> Shader Read Only
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    // Submit and Wait
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue);

    vkFreeCommandBuffers(ctx.device, ctx.commandPool, 1, &cmd);
    vmaDestroyBuffer(ctx.allocator, stagingBuffer, stagingAllocation);

    // 4. Create ImageView and Sampler
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = finalImage.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(ctx.device, &viewInfo, nullptr, &finalImage.imageView);

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST; // Quake uses pixelated/nearest filtering!
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    // Repeat UVs infinitely so textures can tile across walls
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCreateSampler(ctx.device, &samplerInfo, nullptr, &finalImage.sampler);

    return finalImage;
}

} // namespace engine