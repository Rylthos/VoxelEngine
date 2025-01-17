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

#include "SuperBrick.hpp"

#include "Chunk.hpp"

enum VoxelPushConstantFlags { PCF_SHOW_HEAT_MAP = 1 << 0 };

struct Feedback {
    glm::ivec3 superBrickIndex;
    int hasHitBrick;
    glm::ivec3 brickIndex;
    int hasHitVoxel;
    glm::ivec3 voxelIndex;
    int _1;
    glm::ivec3 voxelNormal;
    int _2;
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
    VkDeviceAddress superBrick;
    VkDeviceAddress feedbackBuffer;
};

class SceneManager : public EventReceiver {
  public:
    SceneManager() { }
    ~SceneManager() { freeResources(); }
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

    VkDevice m_Device;
    VmaAllocator m_Allocator;

    uint32_t m_MaxLoaded = 0;
    std::array<Buffer, FRAMES_IN_FLIGHT> m_ToBeLoaded;
    Buffer m_Staging;

    VoxelPushConstants m_VoxelPushConstants;

    SuperBrick m_SuperBrick;
    Buffer m_SuperBrickBuffer;

    Buffer m_FeedbackBuffer;
    Feedback m_Feedback;

    glm::ivec3 m_CurrentChunk { -10, -10, -10 };
    int m_ChunkRange = 2;

    uint32_t m_PlacementSize = 1;

  private:
    glm::ivec3 worldToChunkPos(glm::vec3 position);
    void checkChunks(uint32_t currentFrame);
    void freeBuffers();

    void reedbackFeedback();

    void createStaging(size_t size);
};
