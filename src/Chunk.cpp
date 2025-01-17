#include "Chunk.hpp"

#include "MortenEncode.hpp"

Chunk::Chunk() { }

Chunk::Chunk(glm::ivec3 chunkPosition, uint32_t dimension)
    : m_ChunkPosition(chunkPosition)
    , m_Dimension(dimension)
{
    m_Voxels.assign(m_Dimension * m_Dimension * m_Dimension, { .colourIndex = 1 });
}

Chunk::Chunk(Chunk& other)
{
    m_Allocator = other.m_Allocator;
    m_ChunkPosition = other.m_ChunkPosition;
    m_Dimension = other.m_Dimension;
}

Chunk::Chunk(Chunk&& other)
{
    m_Allocator = std::move(other.m_Allocator);
    m_ChunkPosition = std::move(other.m_ChunkPosition);
    m_Dimension = std::move(other.m_Dimension);
}

Chunk& Chunk::operator=(const Chunk& other)
{
    m_Allocator = other.m_Allocator;
    m_ChunkPosition = other.m_ChunkPosition;
    m_Dimension = other.m_Dimension;

    return *this;
}

void Chunk::setVoxel(glm::uvec3 position, Voxel data)
{
    const uint64_t code = Morten::encode(position);
    assert(code < m_Voxels.size() && "Position exceeds voxel limits");

    m_Voxels.at(code) = data;
}

Voxel Chunk::getVoxel(glm::uvec3 position)
{
    const uint64_t code = Morten::encode(position);
    assert(code < m_Voxels.size() && "Position exceeds voxel limits");

    return m_Voxels.at(code);
}
