#pragma once

#include <vulkan/vulkan.h>

#include <span>
#include <vector>

class PipelineBuilder
{
  private:
    std::vector<VkPipelineShaderStageCreateInfo> m_ShaderStages;

    VkPipelineInputAssemblyStateCreateInfo m_InputAssemblyCI;
    VkPipelineRasterizationStateCreateInfo m_RasterizerCI;
    VkPipelineColorBlendAttachmentState m_ColourBlendAttachment;
    VkPipelineMultisampleStateCreateInfo m_MultisampleCI;
    VkPipelineDepthStencilStateCreateInfo m_DepthStencilCI;
    VkPipelineRenderingCreateInfo m_RenderCI;
    VkFormat m_ColourAttachmentFormat;

    VkPipelineLayout m_PipelineLayout;

  public:
    static PipelineBuilder start();

    PipelineBuilder& setPipelineLayout(VkPipelineLayout layout);

    PipelineBuilder&
    setShaders(std::span<std::pair<VkShaderStageFlagBits, VkShaderModule>> shaders);

    PipelineBuilder&
    setShaders(std::initializer_list<std::pair<VkShaderStageFlagBits, VkShaderModule>> shaders);

    PipelineBuilder& inputAssembly(VkPrimitiveTopology topology);
    PipelineBuilder& rasterizer(VkPolygonMode mode, VkCullModeFlags cullMode,
                                VkFrontFace frontFace);
    PipelineBuilder& setMultisampleNone();

    PipelineBuilder& disableBlending();
    PipelineBuilder& enableBlendingAdditive();
    PipelineBuilder& enableBlendingAlphablend();

    PipelineBuilder& setColourAttachmentFormat(VkFormat format);
    PipelineBuilder& setDepthFormat(VkFormat format);

    PipelineBuilder& disableDepthTest();
    PipelineBuilder& enableDepthTest(bool depthWriteEnable, VkCompareOp compareOp);

    VkPipeline buildPipeline(VkDevice device);

  private:
    PipelineBuilder() {};
    void clear();

    void addShader(VkShaderStageFlagBits stage, VkShaderModule module);
};
