#pragma once

#include <glm/glm.hpp>
#include <vk_mem_alloc.h>

#include "Buffer.hpp"
#include "Voxel.hpp"

class Chunk
{
  public:
    Chunk();
    Chunk(glm::ivec3 chunkPosition, uint32_t dimension);
    Chunk(Chunk& chunk);
    Chunk(Chunk&& chunk);

    Chunk& operator=(const Chunk& other);

    VkDeviceAddress getBufferAddress(VkDevice device) { return m_SVO.getDeviceAddress(device); }

    uint32_t getDimensions() { return m_Dimension; }

    Buffer* getSVOBuffer() { return &m_SVO; }
    std::vector<Voxel>& getVoxels() { return m_Voxels; }

    void setVoxel(glm::uvec3 position, Voxel data);
    Voxel getVoxel(glm::uvec3 position);

    void setIsGenerated(bool generated) { m_Generated = generated; }
    bool isGenerated() { return m_Generated; }

    glm::ivec3& getPosition() { return m_ChunkPosition; }

  private:
    bool m_Initialized = false;
    bool m_Generated = false;

    VmaAllocator m_Allocator;

    glm::ivec3 m_ChunkPosition;
    uint32_t m_Dimension;

    std::vector<Voxel> m_Voxels;
    Buffer m_SVO;
};
