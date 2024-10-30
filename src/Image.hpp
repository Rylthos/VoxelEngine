#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include "ImmediateSubmit.hpp"

class Image
{
  private:
    VkImage m_Image = 0;
    VkImageView m_ImageView = 0;
    VmaAllocation m_Allocation = 0;

    VkExtent3D m_Extent;
    VkFormat m_Format;

    VmaAllocator m_Allocator;
    VkDevice m_Device;

  public:
    Image();
    Image(Image&) = delete;
    Image(Image&&) = delete;

    ~Image();

    void create(VmaAllocator allocator, VkFormat format, VkExtent3D extent, VkImageType type,
                VkImageUsageFlags usage, VmaMemoryUsage memoryUsage,
                VkMemoryPropertyFlags memoryProperties);
    void createImageView(VkDevice device, VkImageViewType viewType);
    void free();

    VkImage getImage() const { return m_Image; }
    VkExtent3D getExtent() const { return m_Extent; }
    VkFormat getFormat() const { return m_Format; }
    VkImageView getImageView() const { return m_ImageView; }
    VmaAllocation getAllocation() const { return m_Allocation; }

    void transition(VkCommandBuffer commandBuffer, VkImageLayout current, VkImageLayout target);
    static void transition(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout current,
                           VkImageLayout target);

    void copyToImage(VkCommandBuffer commandBuffer, const Image& image);
    void copyFromImage(VkCommandBuffer commandBuffer, const Image& image);

    static void copyFromTo(VkCommandBuffer commandBuffer, VkImage src, VkImage dst,
                           VkExtent3D srcSize, VkExtent3D dstSize);
};
