#pragma once

#include <glm/glm.hpp>
#include <vk_mem_alloc.h>

#include "SuperBrick.hpp"

#define CHUNK_SIZE 16

struct ChunkEntry {
    uint32_t loaded : 1;
    uint32_t requested : 1;
    uint32_t empty_flag : 1;
    uint32_t unused : 1;
    uint32_t pointer : 12;
    uint32_t _ : 16;
};

struct ChunkStruct {
    std::array<ChunkEntry, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> data;
    VkDeviceAddress pointers;
};

class Chunk {
  public:
    Chunk();

    void init(VkDevice device, VmaAllocator allocator, Queue* computeQueue);

    void free();

    bool hasGenerated(glm::ivec3 chunkIndex, glm::ivec3 brickIndex);

    void loadSuperBrick(glm::ivec3 index);

    void loadBrick(std::tuple<glm::ivec3, glm::ivec3, glm::ivec3>, Brick& brick);

    ChunkStruct getStruct();

  private:
    std::unordered_map<glm::ivec3, SuperBrick> m_SuperBricks;
    std::unordered_map<glm::ivec3, uint16_t> m_SuperBrickIndices;
    std::unordered_set<glm::ivec3> m_ToBeLoaded;
    std::unordered_set<glm::ivec3> m_ToBeUpdated;
    std::set<uint16_t> m_FreeIndices;

    ChunkStruct m_ChunkStruct;

    VmaAllocator m_Allocator;
    VkDevice m_Device;

    Buffer m_SuperBrickLocations;
    Buffer m_Staging;

    size_t m_CurrentPoolSize = 256;

    PROF_LOCKABLE_MUTEX(std::mutex, m_BufferLock, "Buffer Lock");
    PROF_LOCKABLE_MUTEX(std::mutex, m_LoadedLock, "ToBeLoaded Lock");
    PROF_LOCKABLE_MUTEX(std::mutex, m_UpdatedLock, "ToBeUpdated Lock");

  private:
    size_t positionToIndex(glm::ivec3 index)
    {
        return index.x + index.z * CHUNK_SIZE + index.y * CHUNK_SIZE * CHUNK_SIZE;
    }

    void growPool(bool preserveStaging);

    void createStaging(size_t size);
};
