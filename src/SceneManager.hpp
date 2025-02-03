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

#define MAX_PLACEMENT_SIZE 64
#define MIN_PLACEMENT_SIZE 1

#define MAX_LOADED 256

enum class PlacementType : int { Cube = 0, Sphere = 1, NUM_TYPES };

static const char* PlacementTypeToString[] = {
    "Cube",
    "Sphere",
};

enum VoxelPushConstantFlags { PCF_SHOW_HEAT_MAP = 1 << 0 };

struct Feedback {
    glm::ivec3 chunkIndex;
    bool hasHitChunk;
    glm::ivec3 superBrickIndex;
    bool hasHitSuperBrick;
    glm::ivec3 brickIndex;
    bool hasHitBrick;
    glm::ivec3 voxelIndex;
    bool hasHitVoxel;
    glm::ivec3 voxelNormal;
};

struct LoadedData {
    glm::ivec4 superBrickIndex;
    glm::ivec4 brickIndex;
};

struct VoxelPushConstants {
    glm::vec3 cameraPosition;
    float aspectRatio;

    glm::vec3 cameraForward;
    int shouldLoadVoxels;

    glm::vec4 cameraRight;
    glm::vec4 cameraUp;

    glm::vec4 sunDirection;

    float superBrickLODDistance;
    float brickLODDistance;
    uint32_t maxHeatShown;
    uint32_t maxIterations;

    VkDeviceAddress toBeLoaded;
    VkDeviceAddress chunk;
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

    std::array<Buffer, FRAMES_IN_FLIGHT> m_ToBeLoaded;
    Buffer m_Staging;

    VoxelPushConstants m_VoxelPushConstants;

    std::unordered_map<glm::ivec3, Chunk> m_Chunks;
    Buffer m_ChunkBuffer;
    // SuperBrick m_SuperBrick;
    // Buffer m_SuperBrickBuffer;

    Buffer m_FeedbackBuffer;
    Feedback m_Feedback;

    glm::vec3 m_CurrentColour;

    bool m_InfinitePlace = false;
    bool m_PlaceVoxel = false;
    bool m_EraseVoxel = false;
    bool m_ReplaceVoxels = false;
    PlacementType m_CurrentPlacement = PlacementType::Sphere;
    uint32_t m_PlacementSize = MIN_PLACEMENT_SIZE;

  private:
    void checkChunks(uint32_t currentFrame);
    void freeBuffers();

    void reedbackFeedback();

    void createStaging(size_t size);
};
