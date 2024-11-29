#pragma once

#include <glm/gtx/hash.hpp>

#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <spdlog/fmt/bin_to_hex.h>

#include "Buffer.hpp"
#include "Camera.hpp"
#include "PaletteManager.hpp"
#include "Voxel.hpp"

#include "Chunk.hpp"

enum VoxelPushConstantFlags { PCF_SHOW_HEAT_MAP = 1 << 0 };

struct Chunks {
    std::mutex mutex;
    std::unordered_map<glm::ivec3, Chunk> chunks;
};

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
    SceneManager(PaletteManager* paletteManager, Camera* camera);
    SceneManager(SceneManager& other);

    SceneManager operator=(const SceneManager& other);

    void initResources(VkDevice device, VmaAllocator allocator, VkQueue computeQueue,
                       uint32_t computeQueueFamily);
    void freeResources();

    void receive(const Event* event);

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

    uint32_t m_Dimension = 1 << 7;
    PaletteManager* m_PaletteManager;
    Camera* m_Camera;

    std::thread m_ChunkGeneration;
    Chunks m_Chunks;

    VkDevice m_Device;
    VmaAllocator m_Allocator;
    Buffer m_Staging;

    VoxelPushConstants m_VoxelPushConstants;

    Buffer m_ChunkDataAddress;

    glm::ivec3 m_CurrentChunk{ -10, -10, -10 };
    int m_ChunkRange = 3;

  private:
    glm::ivec3 worldToChunkPos(glm::vec3 position);
    void checkChunks();
    void createBufferChunks();
    void freeBuffers();
};
