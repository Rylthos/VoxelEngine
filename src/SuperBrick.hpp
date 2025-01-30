#pragma once
#include <variant>
#include <vector>
#include <vulkan/vulkan.h>

#include <array>
#include <condition_variable>
#include <deque>
#include <glm/gtx/hash.hpp>
#include <set>
#include <unordered_set>

#include "Brick.hpp"
#include "Buffer.hpp"
#include "Queue.hpp"

#include "IntervalList.hpp"

#define SUPERBRICK_SIZE 16

#define ERASE_OP int
#define PLACE_OP glm::vec4
typedef std::variant<ERASE_OP, PLACE_OP> VoxelOp;
typedef std::tuple<glm::ivec3, glm::ivec3, VoxelOp> VoxelChange;

struct SuperBrickEntry {
    uint32_t loaded : 1;
    uint32_t requested_flag : 1;
    uint32_t empty_flag : 1;
    uint32_t unused : 1;
    uint32_t pointer : 12;
    uint32_t _ : 16;
};

struct GenerationData {
    uint32_t solidVoxels;
    int colourSumR;
    int colourSumG;
    int colourSumB;
    uint64_t solidMask[8];
};

struct SuperBrickStruct {
    std::array<SuperBrickEntry, SUPERBRICK_SIZE * SUPERBRICK_SIZE * SUPERBRICK_SIZE> data;
    VkDeviceAddress bricks;
    VkDeviceAddress colour;
};

class SuperBrick {
  public:
    SuperBrick();

    void init(VkDevice device, VmaAllocator allocator, Queue* computeQueue);
    void free();

    void addBrickToQueue(glm::ivec3 position);
    void addBrickToQueue(uint32_t index);

    bool isLoaded(glm::ivec3 position);
    bool isLoaded(uint32_t index);

    VkDeviceAddress getBrickmap() { return m_BrickPool.getDeviceAddress(m_Device); }

    void placeVoxel(
        glm::ivec3 brickIndex, glm::ivec3 voxelIndex, glm::vec4 colour, bool replace = false);
    void eraseVoxel(glm::ivec3 brickIndex, glm::ivec3 voxelIndex, bool replace = false);

    void changeVoxels(const std::vector<VoxelChange>& voxels, bool replace);

    SuperBrickStruct getStruct();

    void reset();

    size_t getBricksSize() { return m_GeneratedBricks.size(); }
    size_t getQueued() { return m_ToBeGenerated.size(); }
    size_t getFreeIndices() { return m_FreeIndices.size(); }
    size_t getCurrentAllocation() { return m_CurrentPoolSize; }
    size_t getCurrentColourAllocation() { return m_CurrentColourCount; }
    size_t getCurrentColourAllocationSize() { return m_MaxColours; }
    size_t getCurrentColourIndexSize() { return m_AvailableColourIndices.totalFree(); }
    size_t getQueuedChanges() { return m_QueuedChanges.size(); }

  private:
    bool m_Initialized = false;
    std::unordered_map<glm::ivec3, Brick> m_Bricks;
    std::unordered_set<glm::ivec3> m_ToBeLoaded;
    std::unordered_map<glm::ivec3, uint16_t> m_GeneratedBricks;
    std::set<uint16_t> m_FreeIndices;

    size_t m_CurrentPoolSize = 256;

    Buffer m_BrickPool;
    Buffer m_Staging;

    size_t m_MaxColours = 512 * 16 * 16;
    size_t m_CurrentColourCount = 0;
    IntervalList<uint32_t> m_AvailableColourIndices;
    std::unordered_map<glm::ivec3, std::pair<uint32_t, uint32_t>> m_AllocatedColourSizes;
    Buffer m_Colours;

    SuperBrickStruct m_Struct;

    VkDevice m_Device;
    VmaAllocator m_Allocator;

    Queue* m_ComputeQueue;
    VkPipeline m_GeneratePipeline;
    VkPipelineLayout m_GeneratePipelineLayout;

    bool m_Running = false;
    std::vector<std::thread> m_GenerationThreads;
    size_t m_NumGenerationThreads = 4;

    std::condition_variable_any m_CanGenerate;
    PROF_LOCKABLE_MUTEX(std::mutex, m_GeneratedQueueLock, "Generated Queue Lock");
    PROF_LOCKABLE_MUTEX(std::mutex, m_BufferLock, "Buffer Lock");
    PROF_LOCKABLE_MUTEX(std::mutex, m_QueuedChangesLock, "Queued Changed Lock");
    PROF_LOCKABLE_MUTEX(std::mutex, m_LoadedLock, "ToBeLoaded Lock");
    PROF_LOCKABLE_MUTEX(std::mutex, m_EnqueuedLock, "Enqueued Lock");

    std::deque<glm::ivec3> m_ToBeGenerated;
    std::unordered_set<glm::ivec3> m_Enqueued;

    std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, std::pair<VoxelOp, bool>>>
        m_QueuedChanges;

  private:
    void transformChange(
        VoxelChange change, glm::ivec3& brickIndex, glm::ivec3& voxelIndex, VoxelOp& op);
    void transformChanges(const std::vector<VoxelChange> changes,
        std::unordered_map<glm::ivec3, std::vector<std::pair<glm::ivec3, VoxelOp>>>&
            groupedChanges);
    void setVoxels(const std::vector<VoxelChange>& voxels, bool replace);

    void generateStaging(size_t size);

    void resizeColours(bool preserveStaging = false);
    void resizeBricks(bool preserveStaging = false);
};
