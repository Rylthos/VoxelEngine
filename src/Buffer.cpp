#include "Buffer.hpp"

#include "VkCheck.hpp"

#include <spdlog/spdlog.h>
#include <vulkan/vulkan_core.h>

Buffer::Buffer() { }

Buffer::~Buffer() { }

Buffer::Buffer(Buffer&& other)
{
    m_Buffer = other.m_Buffer;
    m_Allocation = other.m_Allocation;
    m_AllocationInfo = other.m_AllocationInfo;
    m_Allocator = other.m_Allocator;
    m_Size = other.m_Size;

    other.m_Buffer = 0;
}

void Buffer::create(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags flags)
{
    assert(m_Buffer == 0 && "Buffer already initialized");

    m_Allocator = allocator;
    m_Size = size;

    VkBufferCreateInfo bufferCI {};
    bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCI.pNext = nullptr;
    bufferCI.size = m_Size;
    bufferCI.usage = usage;

    VmaAllocationCreateInfo vmaACI {};
    vmaACI.usage = memoryUsage;
    vmaACI.flags = flags;

    VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferCI, &vmaACI, &m_Buffer, &m_Allocation,
        &m_AllocationInfo));

    spdlog::info("Created buffer with size: {}", m_Size);
}

void Buffer::free()
{
    if (m_Buffer == 0)
        return;

    spdlog::info("Freeing Buffer");
    vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);

    m_Allocator = 0;
    m_Buffer = 0;
    m_Size = 0;
    m_Allocation = 0;
    m_AllocationInfo = {};
}

VkDeviceAddress Buffer::getDeviceAddress(VkDevice device) const
{
    VkBufferDeviceAddressInfo deviceAI {};
    deviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    deviceAI.pNext = nullptr;
    deviceAI.buffer = m_Buffer;

    VkDeviceAddress address = vkGetBufferDeviceAddress(device, &deviceAI);

    return address;
}

void Buffer::copyFromBuffer(VkCommandBuffer cmd, const Buffer& buffer, size_t size,
    size_t srcOffset, size_t dstOffset)
{
    if (size == 0) {
        return;
    }

    VkBufferCopy copy {};
    copy.srcOffset = srcOffset;
    copy.dstOffset = dstOffset;
    copy.size = size;

    vkCmdCopyBuffer(cmd, buffer.getBuffer(), getBuffer(), 1, &copy);
}

void Buffer::copyFromBuffer(const Buffer& buffer, size_t size, size_t srcOffset, size_t dstOffset)
{
    ImmediateSubmit::submit(
        [&](VkCommandBuffer cmd) { copyFromBuffer(cmd, buffer, size, srcOffset, dstOffset); });
}
