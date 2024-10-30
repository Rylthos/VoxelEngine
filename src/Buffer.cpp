#include "Buffer.hpp"

#include "VkCheck.hpp"

#include <spdlog/spdlog.h>

Buffer::Buffer() {}

Buffer::~Buffer() { free(); }

void Buffer::create(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
                    VmaMemoryUsage memoryUsage)
{
    assert(m_Buffer == 0 && "Buffer already initialized");

    m_Allocator = allocator;

    VkBufferCreateInfo bufferCI{};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.pNext = nullptr;
    bufferCI.size = size;
    bufferCI.usage = usage;

    VmaAllocationCreateInfo vmaACI{};
    vmaACI.usage = memoryUsage;
    vmaACI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    // VK_CHECK(vkCreateBuffer(m_Device, &bufferCI, nullptr, &m_Buffer));
    VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferCI, &vmaACI, &m_Buffer, &m_Allocation,
                             &m_AllocationInfo));
    spdlog::info("Created buffer with size: {}", size);
}

void Buffer::free()
{
    if (m_Buffer == 0) return;

    spdlog::info("Freeing Buffer");
    vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);

    m_Allocator = 0;
    m_Buffer = 0;
    m_Allocation = 0;
    m_AllocationInfo = {};
}

void Buffer::copyFromBuffer(const Buffer& buffer, size_t size, size_t srcOffset, size_t dstOffset)
{
    ImmediateSubmit::submit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{};
        copy.srcOffset = srcOffset;
        copy.dstOffset = dstOffset;
        copy.size = size;

        vkCmdCopyBuffer(cmd, buffer.getBuffer(), getBuffer(), 1, &copy);
    });
}
