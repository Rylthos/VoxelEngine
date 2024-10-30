#include "ShaderModule.hpp"

#include "VkCheck.hpp"

#include <spdlog/spdlog.h>

#include <fstream>

ShaderModule::ShaderModule() {}
ShaderModule::~ShaderModule() { free(); }

void ShaderModule::create(const char* filePath, VkDevice device)
{
    assert(m_Module == 0 && "Module already created");
    m_Device = device;

    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        spdlog::error("Failed to open file: {}", filePath);
        return;
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read((char*)buffer.data(), fileSize);

    file.close();

    VkShaderModuleCreateInfo shaderModuleCI{};
    shaderModuleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCI.pNext = nullptr;
    shaderModuleCI.codeSize = buffer.size() * sizeof(uint32_t);
    shaderModuleCI.pCode = buffer.data();

    VkResult result = vkCreateShaderModule(m_Device, &shaderModuleCI, nullptr, &m_Module);
    if (result != VK_SUCCESS)
    {
        spdlog::error("Failed to compile Shader module: {}: {}", filePath, string_VkResult(result));
        return;
    }
    spdlog::info("Compiled shader module: {}", filePath);
}

void ShaderModule::free()
{
    vkDestroyShaderModule(m_Device, m_Module, nullptr);
    m_Module = 0;
    m_Device = 0;
}
