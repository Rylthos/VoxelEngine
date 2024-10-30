#include "Descriptors.hpp"

#include "VkCheck.hpp"

DescriptorLayoutBuilder DescriptorLayoutBuilder::start(VkDevice device)
{
    DescriptorLayoutBuilder builder{ device };

    return builder;
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addBinding(uint32_t binding,
                                                             VkDescriptorType descriptorType,
                                                             VkShaderStageFlags shaderStages)
{
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorCount = 1;
    layoutBinding.descriptorType = descriptorType;
    layoutBinding.stageFlags = shaderStages;
    layoutBinding.pImmutableSamplers = nullptr;

    m_Bindings.push_back(layoutBinding);

    return *this;
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addStorageBuffer(uint32_t binding,
                                                                   VkShaderStageFlags shaderStages)
{
    addBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, shaderStages);

    return *this;
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addStorageImage(uint32_t binding,
                                                                  VkShaderStageFlags shaderStages)
{
    addBinding(binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, shaderStages);

    return *this;
}

DescriptorLayoutBuilder&
DescriptorLayoutBuilder::addCombinedImageSampler(uint32_t binding, VkShaderStageFlags shaderStages)
{
    addBinding(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, shaderStages);

    return *this;
}

VkDescriptorSetLayout DescriptorLayoutBuilder::build()
{
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
    descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(m_Bindings.size());
    descriptorSetLayoutCI.pBindings = m_Bindings.data();

    VkDescriptorSetLayout layout;
    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &descriptorSetLayoutCI, nullptr, &layout));

    return layout;
}

DescriptorLayoutBuilder::DescriptorLayoutBuilder(VkDevice device) { m_Device = device; }

DescriptorSetBuilder DescriptorSetBuilder::start(VkDevice device, VkDescriptorPool pool,
                                                 size_t setCount, VkDescriptorSetLayout layout)
{
    DescriptorSetBuilder builder{ device, pool, setCount, layout };
    return builder;
}

DescriptorSetBuilder DescriptorSetBuilder::start(VkDevice device, VkDescriptorPool pool,
                                                 VkDescriptorSetLayout layout)
{
    DescriptorSetBuilder builder{ device, pool, 1, layout };
    return builder;
}

DescriptorSetBuilder&
DescriptorSetBuilder::addWriteDescriptorSet(uint32_t binding, VkDescriptorType type,
                                            VkDescriptorImageInfo* imageInfo,
                                            VkDescriptorBufferInfo* bufferInfo)
{
    m_DescriptorWrites[-1].push_back(
        VkWriteDescriptorSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              .dstBinding = binding,
                              .dstArrayElement = 0,
                              .descriptorCount = 1,
                              .descriptorType = type,
                              .pImageInfo = imageInfo,
                              .pBufferInfo = bufferInfo,
                              .pTexelBufferView = nullptr });
    return *this;
}

DescriptorSetBuilder& DescriptorSetBuilder::addCombinedImageSampler(uint32_t binding,
                                                                    VkImageLayout imageLayout,
                                                                    VkImageView imageView,
                                                                    VkSampler sampler)
{
    m_ImageInfos.push_back(VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = imageView,
        .imageLayout = imageLayout,
    });

    m_DescriptorWrites[-1].push_back(
        VkWriteDescriptorSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              .dstBinding = binding,
                              .dstArrayElement = 0,
                              .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              .pImageInfo = (VkDescriptorImageInfo*)m_ImageInfos.size(),
                              .pBufferInfo = nullptr,
                              .pTexelBufferView = nullptr });

    return *this;
}

DescriptorSetBuilder& DescriptorSetBuilder::addStorageImage(uint32_t binding,
                                                            VkImageLayout imageLayout,
                                                            VkImageView imageView)
{
    m_ImageInfos.push_back(VkDescriptorImageInfo{
        .sampler = 0,
        .imageView = imageView,
        .imageLayout = imageLayout,
    });

    m_DescriptorWrites[-1].push_back(
        VkWriteDescriptorSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              .dstBinding = binding,
                              .dstArrayElement = 0,
                              .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                              .pImageInfo = (VkDescriptorImageInfo*)m_ImageInfos.size(),
                              .pBufferInfo = nullptr,
                              .pTexelBufferView = nullptr });

    return *this;
}

DescriptorSetBuilder& DescriptorSetBuilder::addStorageBuffer(uint32_t binding, VkBuffer buffer,
                                                             uint32_t offset, size_t range)
{
    m_BufferInfos.push_back(
        VkDescriptorBufferInfo{ .buffer = buffer, .offset = offset, .range = range });

    m_DescriptorWrites[-1].push_back(
        VkWriteDescriptorSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              .dstBinding = binding,
                              .dstArrayElement = 0,
                              .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                              .pImageInfo = nullptr,
                              .pBufferInfo = (VkDescriptorBufferInfo*)m_BufferInfos.size(),
                              .pTexelBufferView = nullptr });
    return *this;
}

DescriptorSetBuilder& DescriptorSetBuilder::addStorageBuffers(uint32_t binding,
                                                              const std::span<VkBuffer>& buffers,
                                                              uint32_t offset, size_t range)
{
    for (size_t i = 0; i < buffers.size(); i++)
    {
        m_BufferInfos.push_back(
            VkDescriptorBufferInfo{ .buffer = buffers[i], .offset = offset, .range = range });

        m_DescriptorWrites[i].push_back(
            VkWriteDescriptorSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstBinding = binding,
                                  .dstArrayElement = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                  .pImageInfo = nullptr,
                                  .pBufferInfo = (VkDescriptorBufferInfo*)m_BufferInfos.size(),
                                  .pTexelBufferView = nullptr });
    }

    return *this;
}

DescriptorSetBuilder& DescriptorSetBuilder::addStorageBuffers(uint32_t binding,
                                                              const std::span<Buffer>& buffers,
                                                              uint32_t offset, size_t range)
{
    for (size_t i = 0; i < buffers.size(); i++)
    {
        m_BufferInfos.push_back(VkDescriptorBufferInfo{
            .buffer = buffers[i].getBuffer(), .offset = offset, .range = range });

        m_DescriptorWrites[i].push_back(
            VkWriteDescriptorSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstBinding = binding,
                                  .dstArrayElement = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                  .pImageInfo = nullptr,
                                  .pBufferInfo = (VkDescriptorBufferInfo*)m_BufferInfos.size(),
                                  .pTexelBufferView = nullptr });
    }

    return *this;
}

std::vector<VkDescriptorSet> DescriptorSetBuilder::build()
{
    for (size_t i = 0; i < m_Sets; i++)
    {
        std::vector<VkWriteDescriptorSet> sets = m_DescriptorWrites[-1];

        sets.insert(sets.end(), m_DescriptorWrites[i].begin(), m_DescriptorWrites[i].end());

        for (VkWriteDescriptorSet& set : sets)
        {
            set.dstSet = m_DescriptorSets.at(i);
            if (set.pBufferInfo != nullptr)
                set.pBufferInfo = &m_BufferInfos[(size_t)(set.pBufferInfo) - 1];

            if (set.pImageInfo != nullptr)
                set.pImageInfo = &m_ImageInfos[(size_t)(set.pImageInfo) - 1];
        }

        vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(sets.size()), sets.data(), 0,
                               nullptr);
    }

    spdlog::info("Built Descriptor Set");

    return m_DescriptorSets;
}

DescriptorSetBuilder::DescriptorSetBuilder(VkDevice device, VkDescriptorPool pool, size_t setCount,
                                           VkDescriptorSetLayout layout)
    : m_Device{ device }, m_Sets{ setCount }, m_Layout{ layout }
{
    allocate(pool);
}

void DescriptorSetBuilder::allocate(VkDescriptorPool pool)
{
    std::vector<VkDescriptorSetLayout> layouts(m_Sets, m_Layout);
    VkDescriptorSetAllocateInfo descriptorSetAI{};
    descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAI.descriptorPool = pool;
    descriptorSetAI.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    descriptorSetAI.pSetLayouts = layouts.data();

    m_DescriptorSets.resize(m_Sets);

    VK_CHECK(vkAllocateDescriptorSets(m_Device, &descriptorSetAI, m_DescriptorSets.data()));
}
