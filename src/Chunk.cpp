#include "Chunk.hpp"
#include "SuperBrick.hpp"
#include "VkBootstrap.h"

#include <glm/gtx/string_cast.hpp>

#include <mutex>
#include <spdlog/spdlog.h>

#include <vulkan/vulkan_core.h>

Chunk::Chunk() { }

void Chunk::init(VkDevice device, VmaAllocator allocator, Queue* computeQueue)
{
    m_Allocator = allocator;
    m_Device = device;

    for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE; i++) {
        m_ChunkStruct.data[i] = {
            .loaded = 0,
            .empty_flag = 0,
        };
    }

    for (uint16_t i = 0; i < m_CurrentPoolSize; i++) {
        m_FreeIndices.insert(i);
    }

    loadSuperBrick({ 0, 0, 0 });

    m_SuperBrickLocations.create(allocator, sizeof(SuperBrickStruct) * m_CurrentPoolSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
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

bool Chunk::hasGenerated(glm::ivec3 chunkIndex, glm::ivec3 brickIndex)
{
    if (!m_SuperBricks.contains(chunkIndex))
        return false;

    return m_SuperBricks[chunkIndex].hasGenerated(brickIndex);
}

void Chunk::loadSuperBrick(glm::ivec3 index)
{
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_UpdatedLock);

    if (m_SuperBrickIndices.contains(index))
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
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_UpdatedLock);

    glm::ivec3 superBrickPosition = std::get<1>(position);
    if (!m_SuperBricks.contains(superBrickPosition)) {
        spdlog::error("Loading brick before loaded: {}", glm::to_string(superBrickPosition));

        return;
    }

    m_SuperBricks[superBrickPosition].loadBrick(position, brick);
    m_ToBeUpdated.insert(superBrickPosition);
}

ChunkStruct Chunk::getStruct()
{
    if (m_ToBeLoaded.size() != 0) {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_LoadedLock);

        createStaging(sizeof(SuperBrickStruct));
        for (const auto& pos : m_ToBeLoaded) {
            size_t index = positionToIndex(pos);

            const SuperBrickStruct& superBrick = m_SuperBricks[pos].getStruct();

            bool isEmpty = true;
            for (size_t i = 0; i < superBrick.data.size(); i++) {
                if (superBrick.data[i].empty_flag == 0) {
                    isEmpty = false;
                    break;
                }

                if (superBrick.data[i].loaded == 0) {
                    isEmpty = false;
                    break;
                }
            }

            m_ChunkStruct.data[index].loaded = 1;
            if (isEmpty) {
                m_ChunkStruct.data[index].empty_flag = 1;
                continue;
            }

            if (m_FreeIndices.size() == 0) {
                growPool(false);
            }

            uint16_t chosenIndex = *m_FreeIndices.begin();
            m_FreeIndices.erase(chosenIndex);
            m_SuperBrickIndices[pos] = chosenIndex;

            m_ChunkStruct.data[index].pointer = chosenIndex;
            m_ChunkStruct.data[index].requested = 0;

            std::vector<SuperBrickStruct> temp { superBrick };
            m_Staging.copyFromData_CPUOnly<SuperBrickStruct>(temp);
            m_SuperBrickLocations.copyFromBuffer(
                m_Staging, sizeof(SuperBrickStruct), 0, chosenIndex * sizeof(SuperBrickStruct));
        }
        m_ToBeLoaded.clear();
    }

    if (m_ToBeUpdated.size() != 0) {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_UpdatedLock);

        createStaging(sizeof(SuperBrickStruct));
        for (const auto& pos : m_ToBeUpdated) {
            const SuperBrickStruct& superBrick = m_SuperBricks[pos].getStruct();

            bool isEmpty = true;
            for (size_t i = 0; i < superBrick.data.size(); i++) {
                if (superBrick.data[i].empty_flag == 0 || superBrick.data[i].loaded == 0) {
                    isEmpty = false;
                    break;
                }
            }

            size_t index = positionToIndex(pos);

            m_ChunkStruct.data[index].loaded = 1;

            if (isEmpty) {
                m_ChunkStruct.data[index].empty_flag = 1;
                if (m_SuperBrickIndices.contains(pos)) {
                    m_FreeIndices.insert(m_SuperBrickIndices[pos]);
                    m_SuperBrickIndices.erase(pos);
                }
                continue;
            }

            uint16_t location = m_SuperBrickIndices[pos];

            m_ChunkStruct.data[index].requested = 0;
            m_ChunkStruct.data[index].pointer = location;
            m_ChunkStruct.data[index].empty_flag = 0;

            std::vector<SuperBrickStruct> temp { superBrick };
            m_Staging.copyFromData_CPUOnly<SuperBrickStruct>(temp);
            m_SuperBrickLocations.copyFromBuffer(
                m_Staging, sizeof(SuperBrickStruct), 0, location * sizeof(SuperBrickStruct));
        }

        m_ToBeUpdated.clear();
    }

    m_ChunkStruct.pointers = m_SuperBrickLocations.getDeviceAddress(m_Device);

    return m_ChunkStruct;
}

void Chunk::growPool(bool preserveStaging)
{
    size_t previous = m_CurrentPoolSize;
    m_CurrentPoolSize *= 2;
    assert(m_CurrentPoolSize <= 4096);
    spdlog::info("Growing superbrick Pool -> {}", m_CurrentPoolSize);

    for (size_t i = previous; i < m_CurrentPoolSize; i++) {
        m_FreeIndices.insert(i);
    }

    size_t size = m_SuperBrickLocations.getSize();

    size_t stagingSize = m_Staging.getSize();

    char* copy;
    if (preserveStaging) {
        copy = (char*)malloc(stagingSize);
        memcpy(copy, m_Staging.getAllocationInfo().pMappedData, stagingSize);
    }

    createStaging(size);

    m_Staging.copyFromBuffer(m_SuperBrickLocations, size);

    m_SuperBrickLocations.free();
    m_SuperBrickLocations.create(m_Allocator, m_CurrentPoolSize * sizeof(SuperBrickStruct),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    m_SuperBrickLocations.copyFromBuffer(m_Staging, size);

    if (preserveStaging) {
        memcpy(m_Staging.getAllocationInfo().pMappedData, copy, stagingSize);
        std::free(copy);
    }
}

void Chunk::createStaging(size_t size)
{
    if (m_Staging.getSize() >= size) {
        return;
    }

    m_Staging.free();

    m_Staging.create(m_Allocator, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
}
