#include "Image.hpp"

#include "VkCheck.hpp"

Image::Image() {}

Image::~Image() { free(); }

void Image::create(VmaAllocator allocator, VkFormat format, VkExtent3D extent, VkImageType type,
                   VkImageUsageFlags usage, VmaMemoryUsage memoryUsage,
                   VkMemoryPropertyFlags memoryProperties)
{
    m_Allocator = allocator;
    m_Format = format;
    m_Extent = extent;

    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.pNext = nullptr;
    imageCI.imageType = type;
    imageCI.format = m_Format;
    imageCI.extent = m_Extent;
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = usage;

    VmaAllocationCreateInfo vmaImageCI{};
    vmaImageCI.usage = memoryUsage;
    vmaImageCI.requiredFlags = memoryProperties;
    VK_CHECK(vmaCreateImage(m_Allocator, &imageCI, &vmaImageCI, &m_Image, &m_Allocation, nullptr));
}

void Image::createImageView(VkDevice device, VkImageViewType viewType)
{
    m_Device = device;
    VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.pNext = nullptr;
    imageViewCI.viewType = viewType;
    imageViewCI.image = m_Image;
    imageViewCI.format = m_Format;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VK_CHECK(vkCreateImageView(m_Device, &imageViewCI, nullptr, &m_ImageView));
}

void Image::free()
{
    if (m_ImageView != 0)
    {
        vkDestroyImageView(m_Device, m_ImageView, nullptr);
        m_ImageView = 0;
    }

    if (m_Image != 0)
    {
        vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
        m_Image = 0;
    }
}

void Image::transition(VkCommandBuffer commandBuffer, VkImageLayout current, VkImageLayout target)
{
    Image::transition(commandBuffer, m_Image, current, target);
}

void Image::transition(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout current,
                       VkImageLayout target)
{
    VkImageMemoryBarrier2 imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imageBarrier.pNext = nullptr;
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    imageBarrier.oldLayout = current;
    imageBarrier.newLayout = target;

    VkImageAspectFlags aspectMask = (target == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                        ? VK_IMAGE_ASPECT_DEPTH_BIT
                                        : VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.aspectMask = aspectMask;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    imageBarrier.image = image;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.pNext = nullptr;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void Image::copyToImage(VkCommandBuffer commandBuffer, const Image& image)
{
    Image::copyFromTo(commandBuffer, m_Image, image.m_Image, m_Extent, image.m_Extent);
}
void Image::copyFromImage(VkCommandBuffer commandBuffer, const Image& image)
{
    Image::copyFromTo(commandBuffer, image.m_Image, m_Image, image.m_Extent, m_Extent);
}

void Image::copyFromTo(VkCommandBuffer commandBuffer, VkImage src, VkImage dst, VkExtent3D srcSize,
                       VkExtent3D dstSize)
{

    VkImageBlit2 blitRegion{};
    blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blitRegion.pNext = nullptr;

    blitRegion.srcOffsets[1].x = srcSize.width;
    blitRegion.srcOffsets[1].y = srcSize.height;
    blitRegion.srcOffsets[1].z = srcSize.depth;

    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcSubresource.mipLevel = 0;

    blitRegion.dstOffsets[1].x = dstSize.width;
    blitRegion.dstOffsets[1].y = dstSize.height;
    blitRegion.dstOffsets[1].z = dstSize.depth;

    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstSubresource.mipLevel = 0;

    VkBlitImageInfo2 blitInfo{};
    blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blitInfo.pNext = nullptr;
    blitInfo.srcImage = src;
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.dstImage = dst;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.filter = VK_FILTER_LINEAR;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;

    vkCmdBlitImage2(commandBuffer, &blitInfo);
}
