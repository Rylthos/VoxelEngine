#include "Chunk.hpp"
#include "SuperBrick.hpp"
#include "VkBootstrap.h"
#include <vulkan/vulkan_core.h>

Chunk::Chunk() { }

void Chunk::init(VkDevice device, VmaAllocator allocator, Queue* computeQueue)
{
    m_Allocator = allocator;
    m_Device = device;

    glm::ivec3 firstIndex = { 0, 0, 0 };
    m_SuperBricks[firstIndex].init(device, allocator, computeQueue);
    m_SuperBricks[firstIndex].addBrickToQueue(0);

    for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++) {
        m_ChunkStruct.data[i] = {};
        m_ChunkStruct.data[i].loaded = 1;
        m_ChunkStruct.data[i].empty_flag = 1;
    }

    m_SuperBrickLocations.create(allocator, sizeof(SuperBrickStruct) * 1,
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
}

void Chunk::free()
{
    for (auto& brick : m_SuperBricks) {
        brick.second.free();
    }

    m_SuperBrickLocations.free();
    m_Staging.free();
}

ChunkStruct Chunk::getStruct()
{

    if (m_SuperBricks[{ 0, 0, 0 }].isLoaded(0) && !m_Generated) {
        m_Generated = true;

        createStaging(sizeof(SuperBrickStruct));

        std::vector<SuperBrickStruct> temp { m_SuperBricks[{ 0, 0, 0 }].getStruct() };

        m_ChunkStruct.data[0].empty_flag = 0;
        m_ChunkStruct.data[0].pointer = 0;

        m_Staging.copyFromData_CPUOnly<SuperBrickStruct>(temp);
        m_SuperBrickLocations.copyFromBuffer(m_Staging, sizeof(SuperBrickStruct) * 1);
        m_ChunkStruct.pointers = m_SuperBrickLocations.getDeviceAddress(m_Device);
    }
    return m_ChunkStruct;
}

void Chunk::createStaging(size_t size)
{
    if (m_Staging.getSize() >= size) {
        return;
    }

    m_Staging.free();

    m_Staging.create(m_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
}
