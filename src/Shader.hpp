#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>

namespace engine {

class Shader {
public:
    // Reads a compiled .spv file and creates a Vulkan Shader Module
    static VkShaderModule LoadModule(VkDevice device, const std::string& filePath) {
        // Open file at the very end (ate) and in binary mode
        std::ifstream file(filePath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open shader file: " + filePath);
        }

        // Get file size and allocate a buffer
        size_t fileSize = (size_t)file.tellg();
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
        
        // Go back to the start and read the bytes
        file.seekg(0);
        file.read((char*)buffer.data(), fileSize);
        file.close();

        // Create the Vulkan Shader Module
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = buffer.size() * sizeof(uint32_t);
        createInfo.pCode = buffer.data();

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module from: " + filePath);
        }

        return shaderModule;
    }
};

} // namespace engine