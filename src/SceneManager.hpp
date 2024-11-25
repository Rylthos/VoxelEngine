#pragma once

#include <glm/glm.hpp>
#include <string>

#include <spdlog/fmt/bin_to_hex.h>

#include "Buffer.hpp"
#include "PaletteManager.hpp"
#include "Voxel.hpp"

#include "Chunk.hpp"

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
    float cutoff;
    int32_t p10;
    int32_t p50;
    int32_t p100;
};

enum VoxelPushConstantFlags { PCF_SHOW_HEAT_MAP = 1 << 0 };

struct VoxelPushConstants {
    glm::vec3 cameraPosition;
    float aspectRatio;
    glm::vec4 cameraForward;
    glm::vec4 cameraRight;
    glm::vec4 cameraUp;
    uint32_t dimension;
    float size;
    uint32_t maxDepthShown = 5;
    uint32_t lod;
    uint32_t maxHeatShown;
    uint32_t flags;
    uint32_t maxIterations;
    uint32_t initialParent;
    VkDeviceAddress voxelAddress;
};

class SceneManager : public EventReceiver
{
  public:
    SceneManager() {}
    ~SceneManager() { freeBuffers(); }
    SceneManager(PaletteManager* paletteManager);
    SceneManager(SceneManager& other);

    SceneManager operator=(const SceneManager& other);

    void receive(const Event* event);

    void initResources(VkDevice device, VmaAllocator allocator);
    void freeResources();

    int getSeed() { return m_GenerationPushConstants.seed; }
    void setSeed(int seed) { m_GenerationPushConstants.seed = seed; }

    VoxelPushConstants& getVoxelPushConstants();

    void generateWorld();

    bool hasUpdated()
    {
        if (m_HasUpdated)
        {
            m_HasUpdated = false;
            return true;
        }
        return false;
    }

    VkDeviceAddress getBufferAddress(VkDevice device) { return m_Chunk.getBufferAddress(device); }
    void updateBuffers();

    std::vector<SVONode> serializeScene();

  private:
    bool m_Initialized = false;
    bool m_HasUpdated = false;
    bool m_AnimateCutoff = false;

    Chunk m_Chunk;

    uint32_t m_Dimension = 1 << 7;
    PaletteManager* m_PaletteManager;

    VkDevice m_Device;
    VmaAllocator m_Allocator;
    Buffer m_Staging;

    VoxelPushConstants m_VoxelPushConstants;
    VoxelGenerationPushConstants m_GenerationPushConstants;

    VkPipelineLayout m_GenerationPipelineLayout;
    VkPipeline m_GenerationPipeline;
    Buffer m_GeneratedVoxels;

  private:
    void createBuffers(size_t size);
    void freeBuffers();
};
