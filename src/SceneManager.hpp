#pragma once

#include <glm/glm.hpp>
#include <string>

#include <spdlog/fmt/bin_to_hex.h>

#include "Buffer.hpp"
#include "PaletteManager.hpp"
#include "Voxel.hpp"

enum SVONodeFlags {
    SVONODE_IS_SOLID = 1 << 0,  // All Smaller nodes are equal
    SVONODE_IS_PARENT = 1 << 1, // Has Smaller Nodes
    SVONODE_IS_AIR = 1 << 2     // Is air
};

struct SVONode {
    uint32_t childPointer;
    uint8_t flags;
    uint8_t materialIndex;
    uint8_t validMask;
    uint8_t leafMask;
} __attribute__((packed));

struct VoxelGenerationPushConstants {
    uint32_t dimension;
    float size;
    uint32_t seed;
    int _;
    VkDeviceAddress targetBuffer;
};

class SceneManager
{
  public:
    SceneManager() {}
    ~SceneManager() { freeBuffers(); }
    SceneManager(uint32_t voxelDimension, PaletteManager* paletteManager);
    SceneManager(SceneManager& other);

    SceneManager operator=(const SceneManager& other);

    void initResources(VkDevice device, VmaAllocator allocator);
    void freeResources();

    int getSeed() { return m_GenerationPushConstants.seed; }
    void setSeed(int seed) { m_GenerationPushConstants.seed = seed; }

    void setDimensions(uint32_t dimension);
    uint32_t getDimension() { return m_Dimension; }

    void setVoxel(glm::uvec3 position, Voxel data) { m_Voxels.at(mortenEncode(position)) = data; }
    Voxel getVoxel(glm::uvec3 position) { return m_Voxels.at(mortenEncode(position)); }

    void generateWorld();

    VkDeviceAddress getBufferAddress(VkDevice device) { return m_SVO.getDeviceAddress(device); }
    uint32_t updateBuffers();

    std::vector<SVONode> serializeScene();

  private:
    bool m_Initialized = false;

    uint32_t m_Dimension;
    std::vector<Voxel> m_Voxels;
    PaletteManager* m_PaletteManager;

    VkDevice m_Device;
    VmaAllocator m_Allocator;
    Buffer m_SVO;
    Buffer m_Staging;

    VoxelGenerationPushConstants m_GenerationPushConstants;

    VkPipelineLayout m_GenerationPipelineLayout;
    VkPipeline m_GenerationPipeline;
    Buffer m_GeneratedVoxels;

  private:
    void createBuffers(size_t size);
    void freeBuffers();

    int64_t splitBy3(uint32_t a);
    int64_t mortenEncode(glm::uvec3 position);

    uint32_t compactBy3(int64_t a);
    glm::uvec3 mortenDecode(int64_t code);
};
