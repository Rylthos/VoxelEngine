#pragma once

#include <glm/gtx/hash.hpp>

#include <thread>
#include <unordered_map>

#include <glm/glm.hpp>
#include <spdlog/fmt/bin_to_hex.h>

#include "Buffer.hpp"
#include "Camera.hpp"
#include "Constants.hpp"
#include "PaletteManager.hpp"
#include "Profilling.hpp"
#include "Queue.hpp"

#include "Chunk.hpp"

enum VoxelPushConstantFlags { PCF_SHOW_HEAT_MAP = 1 << 0 };

struct Brick {
    uint64_t solidMask[8];
    uint8_t colourPtr;
    uint8_t lodR;
    uint8_t lodG;
    uint8_t lodB;
};

struct SuperBrick {
    std::array<uint32_t, 16 * 16 * 16> data;
    VkDeviceAddress bricks;
};

struct VoxelPushConstants {
    glm::vec3 cameraPosition;
    float aspectRatio;

    glm::vec3 cameraForward;
    uint32_t _3;

    glm::vec3 cameraRight;
    uint32_t _1;

    glm::vec3 cameraUp;
    uint32_t _2;

    uint32_t _4;
    float size;
    uint32_t maxDepthShown = 5;
    uint32_t lod;

    uint32_t maxHeatShown;
    uint32_t flags;
    uint32_t maxIterations;
    uint32_t initialParent;

    VkDeviceAddress toBeLoaded;
    VkDeviceAddress brickGrid;
};

class SceneManager : public EventReceiver
{
  public:
    SceneManager() {}
    ~SceneManager() { freeBuffers(); }
    SceneManager(PaletteManager* paletteManager, Camera* camera);
    SceneManager(SceneManager& other);

    SceneManager operator=(const SceneManager& other);

    void initResources(VkDevice device, VmaAllocator allocator, Queue* computeQueue);
    void freeResources();

    void receive(const Event* event);

    VoxelPushConstants& getVoxelPushConstants(uint32_t currentFrame);

  private:
    bool m_Initialized = false;
    bool m_AnimateCutoff = false;
    bool m_PauseRegeneration = false;

    uint32_t m_Dimension = 0;
    PaletteManager* m_PaletteManager;
    Camera* m_Camera;

    SuperBrick m_SuperBrick;

    VkDevice m_Device;
    VmaAllocator m_Allocator;

    std::array<Buffer, FRAMES_IN_FLIGHT> m_BrickGridStaging;
    std::array<Buffer, FRAMES_IN_FLIGHT> m_ToBeLoadedStaging;
    Buffer m_BricksStaging;

    uint32_t m_MaxLoaded = 0;
    std::array<Buffer, FRAMES_IN_FLIGHT> m_ToBeLoaded;

    VoxelPushConstants m_VoxelPushConstants;

    std::array<Buffer, FRAMES_IN_FLIGHT> m_BrickGridBuffer;
    Buffer m_BricksBuffer;

    glm::ivec3 m_CurrentChunk{ -10, -10, -10 };
    int m_ChunkRange = 2;

  private:
    glm::ivec3 worldToChunkPos(glm::vec3 position);
    void checkChunks(uint32_t currentFrame);
    void createBrickGridStaging(Buffer& stagingBuffer, size_t size);
    void freeBuffers();
};
