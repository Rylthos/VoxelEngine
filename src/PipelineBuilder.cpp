#include "PipelineBuilder.hpp"

#include "VkCheck.hpp"

#include <vulkan/vk_enum_string_helper.h>

#include <fstream>
#include <iostream>

#include <spdlog/spdlog.h>

PipelineBuilder PipelineBuilder::start()
{
    PipelineBuilder builder;
    builder.clear();
    return builder;
}

PipelineBuilder& PipelineBuilder::setPipelineLayout(VkPipelineLayout layout)
{
    m_PipelineLayout = layout;
    return *this;
}

PipelineBuilder&
PipelineBuilder::setShaders(std::span<std::pair<VkShaderStageFlagBits, VkShaderModule>> shaders)
{
    m_ShaderStages.clear();
    for (auto pair : shaders)
    {
        addShader(pair.first, pair.second);
    }

    return *this;
}

PipelineBuilder& PipelineBuilder::setShaders(
    std::initializer_list<std::pair<VkShaderStageFlagBits, VkShaderModule>> shaders)
{
    m_ShaderStages.clear();
    for (auto pair : shaders)
    {
        addShader(pair.first, pair.second);
    }

    return *this;
}

PipelineBuilder& PipelineBuilder::inputAssembly(VkPrimitiveTopology topology)
{
    m_InputAssemblyCI = {};
    m_InputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    m_InputAssemblyCI.pNext = nullptr;
    m_InputAssemblyCI.flags = 0;
    m_InputAssemblyCI.topology = topology;
    m_InputAssemblyCI.primitiveRestartEnable = VK_FALSE;

    return *this;
}

PipelineBuilder& PipelineBuilder::rasterizer(VkPolygonMode mode, VkCullModeFlags cullMode,
                                             VkFrontFace frontFace)
{
    m_RasterizerCI = {};
    m_RasterizerCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    m_RasterizerCI.pNext = nullptr;
    m_RasterizerCI.flags = 0;
    m_RasterizerCI.depthClampEnable = VK_FALSE;
    m_RasterizerCI.rasterizerDiscardEnable = VK_FALSE;
    m_RasterizerCI.polygonMode = mode;
    m_RasterizerCI.cullMode = cullMode;
    m_RasterizerCI.frontFace = frontFace;
    m_RasterizerCI.depthBiasEnable = VK_FALSE;
    m_RasterizerCI.lineWidth = 1.0f;

    return *this;
}

PipelineBuilder& PipelineBuilder::setMultisampleNone()
{
    m_MultisampleCI = {};
    m_MultisampleCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    m_MultisampleCI.pNext = nullptr;
    m_MultisampleCI.flags = 0;
    m_MultisampleCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    m_MultisampleCI.sampleShadingEnable = VK_FALSE;
    m_MultisampleCI.minSampleShading = 1.0f;
    m_MultisampleCI.pSampleMask = nullptr;
    m_MultisampleCI.alphaToCoverageEnable = VK_FALSE;
    m_MultisampleCI.alphaToOneEnable = VK_FALSE;

    return *this;
}

PipelineBuilder& PipelineBuilder::disableBlending()
{
    m_ColourBlendAttachment = {};
    m_ColourBlendAttachment.blendEnable = VK_FALSE;
    m_ColourBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    return *this;
}

PipelineBuilder& PipelineBuilder::enableBlendingAdditive()
{
    m_ColourBlendAttachment = {};
    m_ColourBlendAttachment.blendEnable = VK_TRUE;
    m_ColourBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    m_ColourBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    m_ColourBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
    m_ColourBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    m_ColourBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_ColourBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_ColourBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    return *this;
}

PipelineBuilder& PipelineBuilder::enableBlendingAlphablend()
{
    m_ColourBlendAttachment = {};
    m_ColourBlendAttachment.blendEnable = VK_TRUE;
    m_ColourBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    m_ColourBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    m_ColourBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
    m_ColourBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    m_ColourBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_ColourBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_ColourBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    return *this;
}

PipelineBuilder& PipelineBuilder::setColourAttachmentFormat(VkFormat format)
{
    m_ColourAttachmentFormat = format;

    m_RenderCI = {};
    m_RenderCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    m_RenderCI.pNext = nullptr;
    m_RenderCI.colorAttachmentCount = 1;
    m_RenderCI.pColorAttachmentFormats = &m_ColourAttachmentFormat;

    return *this;
}

PipelineBuilder& PipelineBuilder::setDepthFormat(VkFormat format)
{
    m_RenderCI.depthAttachmentFormat = format;

    return *this;
}

PipelineBuilder& PipelineBuilder::disableDepthTest()
{
    m_DepthStencilCI = {};
    m_DepthStencilCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    m_DepthStencilCI.pNext = nullptr;
    m_DepthStencilCI.depthTestEnable = VK_FALSE;
    m_DepthStencilCI.depthWriteEnable = VK_FALSE;
    m_DepthStencilCI.depthCompareOp = VK_COMPARE_OP_NEVER;
    m_DepthStencilCI.depthBoundsTestEnable = VK_FALSE;
    m_DepthStencilCI.stencilTestEnable = VK_FALSE;
    m_DepthStencilCI.front = {};
    m_DepthStencilCI.back = {};
    m_DepthStencilCI.minDepthBounds = 0.0f;
    m_DepthStencilCI.maxDepthBounds = 1.0f;

    return *this;
}

PipelineBuilder& PipelineBuilder::enableDepthTest(bool depthWriteEnable, VkCompareOp compareOp)
{
    m_DepthStencilCI = {};
    m_DepthStencilCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    m_DepthStencilCI.pNext = nullptr;
    m_DepthStencilCI.depthTestEnable = VK_TRUE;
    m_DepthStencilCI.depthWriteEnable = depthWriteEnable;
    m_DepthStencilCI.depthCompareOp = compareOp;
    m_DepthStencilCI.depthBoundsTestEnable = VK_FALSE;
    m_DepthStencilCI.stencilTestEnable = VK_FALSE;
    m_DepthStencilCI.front = {};
    m_DepthStencilCI.back = {};
    m_DepthStencilCI.minDepthBounds = 0.0f;
    m_DepthStencilCI.maxDepthBounds = 1.0f;

    return *this;
}

VkPipeline PipelineBuilder::buildPipeline(VkDevice device)
{
    VkPipelineViewportStateCreateInfo viewportCI{};
    viewportCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportCI.pNext = nullptr;
    viewportCI.viewportCount = 1;
    viewportCI.scissorCount = 1;

    VkPipelineColorBlendStateCreateInfo colourBlendingCI{};
    colourBlendingCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colourBlendingCI.pNext = nullptr;
    colourBlendingCI.logicOpEnable = VK_FALSE;
    colourBlendingCI.logicOp = VK_LOGIC_OP_COPY;
    colourBlendingCI.attachmentCount = 1;
    colourBlendingCI.pAttachments = &m_ColourBlendAttachment;

    VkPipelineVertexInputStateCreateInfo vertexInputCI{};
    vertexInputCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputCI.pNext = nullptr;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicStateCI{};
    dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCI.pNext = nullptr;
    dynamicStateCI.dynamicStateCount = 2;
    dynamicStateCI.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo graphicsPipelineCI{};
    graphicsPipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    graphicsPipelineCI.pNext = &m_RenderCI;
    graphicsPipelineCI.stageCount = static_cast<uint32_t>(m_ShaderStages.size());
    graphicsPipelineCI.pStages = m_ShaderStages.data();
    graphicsPipelineCI.pVertexInputState = &vertexInputCI;
    graphicsPipelineCI.pInputAssemblyState = &m_InputAssemblyCI;
    graphicsPipelineCI.pViewportState = &viewportCI;
    graphicsPipelineCI.pRasterizationState = &m_RasterizerCI;
    graphicsPipelineCI.pMultisampleState = &m_MultisampleCI;
    graphicsPipelineCI.pColorBlendState = &colourBlendingCI;
    graphicsPipelineCI.pDepthStencilState = &m_DepthStencilCI;
    graphicsPipelineCI.pDynamicState = &dynamicStateCI;
    graphicsPipelineCI.layout = m_PipelineLayout;

    VkPipeline newPipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphicsPipelineCI, nullptr,
                                       &newPipeline));

    return newPipeline;
}

void PipelineBuilder::clear()
{
    m_ShaderStages.clear();
    m_InputAssemblyCI = {};
    m_RasterizerCI = {};
    m_ColourBlendAttachment = {};
    m_MultisampleCI = {};
    m_DepthStencilCI = {};
    m_RenderCI = {};

    m_PipelineLayout = 0;
}

void PipelineBuilder::addShader(VkShaderStageFlagBits stage, VkShaderModule module)
{
    VkPipelineShaderStageCreateInfo shaderCI{};
    shaderCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderCI.pNext = nullptr;
    shaderCI.flags = 0;
    shaderCI.stage = stage;
    shaderCI.module = module;
    shaderCI.pName = "main";

    m_ShaderStages.push_back(shaderCI);
}
