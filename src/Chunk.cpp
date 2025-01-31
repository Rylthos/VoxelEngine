#include "Chunk.hpp"
#include "SuperBrick.hpp"
#include "VkBootstrap.h"

#include <spdlog/spdlog.h>

#include <vulkan/vulkan_core.h>

Chunk::Chunk() { }

void Chunk::init(VkDevice device, VmaAllocator allocator, Queue* computeQueue)
{
    m_Allocator = allocator;
    m_Device = device;

    for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++) {
        m_ChunkStruct.data[i] = {
            .loaded = 1,
            .empty_flag = 1,
        };
    }
    m_ChunkStruct.data[0] = {
        .loaded = 0,
        .empty_flag = 0,
    };

    loadSuperBrick({ 0, 0, 0 });

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

void Chunk::loadSuperBrick(glm::ivec3 index)
{
    if (index != glm::ivec3 { 0, 0, 0 })
        return;

    if (m_ChunkStruct.data[positionToIndex(index)].loaded)
        return;

    if (m_ToBeLoaded.contains(index))
        return;

    if (m_SuperBricks.contains(index))
        return;

    m_ToBeLoaded.insert(index);

    m_SuperBricks[index].init(m_Device, m_Allocator);
}

void Chunk::loadBrick(std::tuple<glm::ivec3, glm::ivec3, glm::ivec3> position, Brick& brick)
{
    if (std::get<1>(position) != glm::ivec3 { 0, 0, 0 })
        return;

    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_LoadedLock);

    m_SuperBricks[std::get<1>(position)].loadBrick(position, brick);
    m_ToBeLoaded.insert(std::get<1>(position));
}

ChunkStruct Chunk::getStruct()
{
    if (m_ToBeLoaded.size() != 0) {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_LoadedLock);

        createStaging(sizeof(SuperBrickStruct));

        for (const auto& pos : m_ToBeLoaded) {
            if (pos != glm::ivec3 { 0, 0, 0 })
                continue;

            size_t index = positionToIndex(pos);

            SuperBrickStruct superBrick = m_SuperBricks[pos].getStruct();

            bool isEmpty = true;
            for (size_t i = 0; i < superBrick.data.size(); i++) {
                if (superBrick.data[i].empty_flag == 0) {
                    isEmpty = false;
                    break;
                }
            }

            m_ChunkStruct.data[index].loaded = 1;
            m_ChunkStruct.data[index].empty_flag = isEmpty;
            m_ChunkStruct.data[index].pointer = 0;
            if (!isEmpty) {
                std::vector<SuperBrickStruct> temp { superBrick };
                m_Staging.copyFromData_CPUOnly<SuperBrickStruct>(temp);
                m_SuperBrickLocations.copyFromBuffer(m_Staging, sizeof(SuperBrickStruct) * 1);
            }
        }
        m_ToBeLoaded.clear();

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
