#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <memory>
#include <span>

#include "ImmediateSubmit.hpp"

class Buffer
{
  private:
    VkBuffer m_Buffer = 0;
    VmaAllocation m_Allocation = 0;
    VmaAllocationInfo m_AllocationInfo;

    VmaAllocator m_Allocator;

  public:
    Buffer();
    Buffer(Buffer&) = delete;
    Buffer(Buffer&&) = delete;

    ~Buffer();

    void create(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
                VmaMemoryUsage properties);
    void free();

    VkBuffer getBuffer() const { return m_Buffer; }
    VmaAllocation getAllocation() const { return m_Allocation; }
    VmaAllocationInfo getAllocationInfo() const { return m_AllocationInfo; }

    void copyFromBuffer(const Buffer& buffer, size_t size, size_t srcOffset = 0,
                        size_t dstOffset = 0);

    template<typename T>
    void copyFromData(const std::span<T>& data)
    {
        size_t size = data.size() * sizeof(T);
        Buffer stagingBuffer;
        stagingBuffer.create(m_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VMA_MEMORY_USAGE_CPU_TO_GPU);

        memcpy(stagingBuffer.getAllocationInfo().pMappedData, data.data(), size);

        ImmediateSubmit::submit([&](VkCommandBuffer cmd) {
            VkBufferCopy copy{};
            copy.srcOffset = 0;
            copy.dstOffset = 0;
            copy.size = size;

            vkCmdCopyBuffer(cmd, stagingBuffer.getBuffer(), getBuffer(), 1, &copy);
        });
    }
};
