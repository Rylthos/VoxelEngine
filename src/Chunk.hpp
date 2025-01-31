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

    void loadSuperBrick(glm::ivec3 index);

    void loadBrick(std::tuple<glm::ivec3, glm::ivec3, glm::ivec3>, Brick& brick);

    ChunkStruct getStruct();

  private:
    std::unordered_map<glm::ivec3, SuperBrick> m_SuperBricks;
    ChunkStruct m_ChunkStruct;

    VmaAllocator m_Allocator;
    VkDevice m_Device;

    Buffer m_SuperBrickLocations;
    Buffer m_Staging;

    std::unordered_set<glm::ivec3> m_ToBeLoaded;
    PROF_LOCKABLE_MUTEX(std::mutex, m_BufferLock, "Buffer Lock");
    PROF_LOCKABLE_MUTEX(std::mutex, m_LoadedLock, "ToBeLoaded Lock");

  private:
    size_t positionToIndex(glm::ivec3 index)
    {
        return index.x + index.z * SUPERBRICK_SIZE + index.y * SUPERBRICK_SIZE * SUPERBRICK_SIZE;
    }
    void createStaging(size_t size);
};
