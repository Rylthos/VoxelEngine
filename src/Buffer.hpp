#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <memory>
#include <optional>
#include <span>

#include "ImmediateSubmit.hpp"

class Buffer
{
  public:
    Buffer();
    Buffer(Buffer&) = delete;
    Buffer(Buffer&&);

    ~Buffer();

    void create(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
                VmaMemoryUsage properties,
                VmaAllocationCreateFlags flags = VMA_ALLOCATION_CREATE_MAPPED_BIT);
    void free();

    VkBuffer getBuffer() const { return m_Buffer; }
    VmaAllocation getAllocation() const { return m_Allocation; }
    VmaAllocationInfo getAllocationInfo() const { return m_AllocationInfo; }
    size_t getSize() { return m_Size; }

    VkDeviceAddress getDeviceAddress(VkDevice device) const;

    void copyFromBuffer(const Buffer& buffer, size_t size, size_t srcOffset = 0,
                        size_t dstOffset = 0);

    void copyFromBuffer(VkCommandBuffer cmd, const Buffer& buffer, size_t size,
                        size_t srcOffset = 0, size_t dstOffset = 0);

    template<typename T>
    void copyFromData(const std::span<T>& data)
    {
        size_t size = data.size() * sizeof(T);
        Buffer stagingBuffer;
        stagingBuffer.create(m_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VMA_MEMORY_USAGE_CPU_TO_GPU);

        std::memcpy(stagingBuffer.getAllocationInfo().pMappedData, data.data(), size);

        copyFromBuffer(stagingBuffer, size);

        stagingBuffer.free();
    }

    template<typename T>
    void copyToVector(std::vector<T>& data)
    {
        size_t elements = m_Size / sizeof(T);
        data.resize(elements);
        std::memcpy(data.data(), getAllocationInfo().pMappedData, m_Size);
    }

    template<typename T>
    void copyFromData_CPUOnly(const std::span<T>& data)
    {
        size_t size = data.size() * sizeof(T);

        std::memcpy(getAllocationInfo().pMappedData, data.data(), size);
    }

  private:
    VkBuffer m_Buffer = 0;
    VmaAllocation m_Allocation = 0;
    VmaAllocationInfo m_AllocationInfo;

    VmaAllocator m_Allocator;

    size_t m_Size = 0;
};
