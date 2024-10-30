#pragma once

#include <vulkan/vulkan.h>

class ShaderModule
{
  private:
    VkShaderModule m_Module = 0;

    VkDevice m_Device;

  public:
    ShaderModule();
    ShaderModule(ShaderModule&) = delete;
    ShaderModule(ShaderModule&&) = delete;
    ~ShaderModule();

    void create(const char* filePath, VkDevice device);
    VkShaderModule getShaderModule() { return m_Module; }
    void free();
};
