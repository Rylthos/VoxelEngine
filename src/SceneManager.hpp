#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <spdlog/fmt/bin_to_hex.h>

#include "Buffer.hpp"
#include "PaletteManager.hpp"
#include "Voxel.hpp"

#include "Chunk.hpp"

enum VoxelPushConstantFlags { PCF_SHOW_HEAT_MAP = 1 << 0 };

struct ChunkData {
    glm::vec4 chunkPosition;
    glm::vec2 _;
    VkDeviceAddress chunkData; // ChunkSVOData[]
};

struct VoxelPushConstants {
    glm::vec3 cameraPosition;
    float aspectRatio;

    glm::vec3 cameraForward;
    uint32_t chunkCount;

    glm::vec3 cameraRight;
    uint32_t _1;

    glm::vec3 cameraUp;
    uint32_t _2;

    uint32_t dimension;
    float size;
    uint32_t maxDepthShown = 5;
    uint32_t lod;

    uint32_t maxHeatShown;
    uint32_t flags;
    uint32_t maxIterations;
    uint32_t initialParent;

    VkDeviceAddress chunks; // ChunkData[]
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

    VoxelPushConstants& getVoxelPushConstants();

    bool hasUpdated()
    {
        if (m_HasUpdated)
        {
            m_HasUpdated = false;
            return true;
        }
        return false;
    }

    void updateBuffers();

  private:
    bool m_Initialized = false;
    bool m_HasUpdated = false;
    bool m_AnimateCutoff = false;

    std::vector<Chunk> m_Chunks;

    uint32_t m_Dimension = 1 << 7;
    PaletteManager* m_PaletteManager;

    VkDevice m_Device;
    VmaAllocator m_Allocator;
    Buffer m_Staging;

    VoxelPushConstants m_VoxelPushConstants;

    Buffer m_ChunkDataAddress;

  private:
    void createBufferChunks();
    void freeBuffers();
};
