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

#define MAX_LOADED 512

#define ERASE_OP int
#define PLACE_OP glm::vec4
typedef std::variant<ERASE_OP, PLACE_OP> VoxelOp;
typedef std::tuple<WorldVoxelPosition, VoxelOp> VoxelChange;

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

    Buffer m_FeedbackBuffer;
    Feedback m_Feedback;

    glm::vec3 m_CurrentColour;

    bool m_InfinitePlace = false;
    bool m_PlaceVoxel = false;
    bool m_EraseVoxel = false;
    bool m_ReplaceVoxels = false;
    PlacementType m_CurrentPlacement = PlacementType::Sphere;
    uint32_t m_PlacementSize = MIN_PLACEMENT_SIZE;

    std::unordered_map<WorldBrickPosition, std::unordered_map<glm::ivec3, std::pair<VoxelOp, bool>>,
        tuple_3_hash>
        m_QueuedChanges;

  private:
    void checkChunks(uint32_t currentFrame);
    void freeBuffers();

    void transformChange(VoxelChange change, WorldVoxelPosition& position, VoxelOp& op);
    void transformChanges(const std::vector<VoxelChange> changes,
        std::unordered_map<WorldBrickPosition, std::vector<std::pair<glm::ivec3, VoxelOp>>,
            tuple_3_hash>& groupedChanges);

    void setVoxels(const std::vector<VoxelChange>& voxels, bool replace);

    void reedbackFeedback();

    void createStaging(size_t size);
};
