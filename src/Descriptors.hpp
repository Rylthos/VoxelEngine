#pragma once

#include <vulkan/vulkan.h>

#include <map>
#include <span>
#include <vector>

#include "Buffer.hpp"

class DescriptorLayoutBuilder
{
  public:
    static DescriptorLayoutBuilder start(VkDevice device);

    DescriptorLayoutBuilder& addBinding(uint32_t binding, VkDescriptorType descriptorType,
                                        VkShaderStageFlags shaderStages);

    DescriptorLayoutBuilder& addStorageBuffer(uint32_t binding, VkShaderStageFlags shaderStages);
    DescriptorLayoutBuilder& addStorageImage(uint32_t binding, VkShaderStageFlags shaderStages);
    DescriptorLayoutBuilder& addCombinedImageSampler(uint32_t binding,
                                                     VkShaderStageFlags shaderStages);

    VkDescriptorSetLayout build();

  private:
    DescriptorLayoutBuilder(VkDevice device);

  private:
    VkDevice m_Device;

    std::vector<VkDescriptorSetLayoutBinding> m_Bindings;
};

class DescriptorSetBuilder
{
  public:
    static DescriptorSetBuilder start(VkDevice device, VkDescriptorPool pool, size_t setCount,
                                      VkDescriptorSetLayout layout);
    static DescriptorSetBuilder start(VkDevice device, VkDescriptorPool pool,
                                      VkDescriptorSetLayout layout);

    DescriptorSetBuilder& addWriteDescriptorSet(uint32_t binding, VkDescriptorType type,
                                                VkDescriptorImageInfo* imageInfo,
                                                VkDescriptorBufferInfo* bufferInfo);
    DescriptorSetBuilder& addCombinedImageSampler(uint32_t binding, VkImageLayout imageLayout,
                                                  VkImageView imageView, VkSampler sampler);

    DescriptorSetBuilder& addImageArraySampler(uint32_t binding, VkImageLayout imageLayout,
                                               VkImageView imageView, VkSampler sampler);

    DescriptorSetBuilder& addStorageImage(uint32_t binding, VkImageLayout imageLayout,
                                          VkImageView imageView);

    DescriptorSetBuilder& addStorageBuffer(uint32_t binding, VkBuffer buffer, uint32_t offset,
                                           size_t range);

    DescriptorSetBuilder& addStorageBuffers(uint32_t binding, const std::span<VkBuffer>& buffer,
                                            uint32_t offset, size_t range);
    DescriptorSetBuilder& addStorageBuffers(uint32_t binding, const std::span<Buffer>& buffer,
                                            uint32_t offset, size_t range);

    std::vector<VkDescriptorSet> build();

  private:
    DescriptorSetBuilder(VkDevice device, VkDescriptorPool pool, size_t setCount,
                         VkDescriptorSetLayout layout);

    void allocate(VkDescriptorPool pool);

  private:
    VkDevice m_Device;

    size_t m_Sets;
    VkDescriptorSetLayout m_Layout;

    std::map<int32_t, std::vector<VkWriteDescriptorSet>> m_DescriptorWrites;
    std::vector<VkDescriptorSet> m_DescriptorSets;

    std::vector<VkDescriptorImageInfo> m_ImageInfos;
    std::vector<VkDescriptorBufferInfo> m_BufferInfos;
};
