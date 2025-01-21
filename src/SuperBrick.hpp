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

#define SUPERBRICK_SIZE 16

#define ERASE_OP uint8_t
#define PLACE_OP glm::vec4
typedef std::variant<ERASE_OP, PLACE_OP> VoxelOp;

struct SuperBrickEntry {
    uint32_t loaded : 1;
    uint32_t requested_flag : 1;
    uint32_t empty_flag : 1;
    uint32_t unused : 1;
    uint32_t pointer : 12;
    uint32_t _ : 16;
};

struct SuperBrickStruct {
    std::array<SuperBrickEntry, 16 * 16 * 16> data;
    VkDeviceAddress bricks;
    VkDeviceAddress colour;
};

struct GenerationPushConstants {
    glm::ivec3 brickIndex;
    int _1;
    glm::ivec3 worldPosition;
    int _2;
    VkDeviceAddress data;
    VkDeviceAddress colours;
};

class SuperBrick {
  public:
    SuperBrick();

    void init(VkDevice device, VmaAllocator allocator, Queue* computeQueue);
    void free();

    void addBrickToQueue(glm::ivec3 position);
    void addBrickToQueue(uint32_t index);
    VkDeviceAddress getBrickmap() { return m_BrickPool.getDeviceAddress(m_Device); }

    void placeVoxel(glm::ivec3 brickIndex, glm::ivec3 voxelIndex, glm::vec4 colour);
    void eraseVoxel(glm::ivec3 brickIndex, glm::ivec3 voxelIndex);

    SuperBrickStruct getStruct();

    void reset();

    size_t getBricksSize() { return m_GeneratedBricks.size(); }
    size_t getQueued() { return m_ToBeGenerated.size(); }
    size_t getFreeIndices() { return m_FreeIndices.size(); }
    size_t getCurrentAllocation() { return m_CurrentPoolSize; }

  private:
    bool m_Initialized = false;
    std::unordered_map<glm::ivec3, Brick> m_Bricks;
    std::unordered_set<glm::ivec3> m_ToBeLoaded;
    std::unordered_map<glm::ivec3, uint16_t> m_GeneratedBricks;
    std::set<uint16_t> m_FreeIndices;

    size_t m_CurrentPoolSize;

    Buffer m_BrickPool;
    Buffer m_Staging;

    std::vector<Buffer> m_Colours;
    Buffer m_ColourMap;

    SuperBrickStruct m_Struct;

    VkDevice m_Device;
    VmaAllocator m_Allocator;

    Queue* m_ComputeQueue;
    VkPipeline m_GeneratePipeline;
    VkPipelineLayout m_GeneratePipelineLayout;

    Buffer m_GeneratedData;
    Buffer m_GeneratedColourData;
    VkFence m_GenerationFence;

    VkCommandPool m_CommandPool;
    VkCommandBuffer m_CommandBuffer;

    bool m_Running = false;
    std::thread m_GenerationThread;
    std::condition_variable_any m_CanGenerate;
    std::mutex m_QueueLock;
    std::mutex m_BufferLock;
    std::deque<glm::ivec3> m_ToBeGenerated;
    std::unordered_set<glm::ivec3> m_Enqueued;

    std::unordered_map<glm::ivec3, std::unordered_map<glm::ivec3, VoxelOp>> m_QueuedChanges;

  private:
    void setVoxel(glm::ivec3 brickIndex, glm::ivec3 voxelIndex, bool air,
        glm::vec4 colour = glm::vec4(0.));
    void generateStaging(size_t size);

    void generateBrickLoop();
};
