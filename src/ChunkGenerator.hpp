#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <unordered_set>

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vulkan/vulkan.h>

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
    float cutoff;
    int32_t p10;
    int32_t p50;
    int32_t p100;
    glm::ivec4 origin;
    VkDeviceAddress targetBuffer;
};

class ChunkGenerator
{
  public:
    static void initResources(uint32_t chunkSize, VmaAllocator allocator, VkDevice device,
                              VkQueue computeQueue, uint32_t computeQueueFamily,
                              std::unordered_map<glm::ivec3, Chunk>* chunks);
    static void freeResources();

    static int getWorldSeed() { return s_Seed; }
    static void setWorldSeed(int seed) { s_Seed = seed; }

    static void stopRunning()
    {
        s_Running = false;
        s_GenerateCondition.notify_all();
        s_SerializeCondition.notify_all();
    }

    static void addChunkToQueue(glm::ivec3 chunk);

    // static void flushChunks();
    static void removeChunk(glm::ivec3 pos);
    static void sync();

    static void generateChunkLoop();

  private:
    static std::mutex s_GenerateQueueMutex;  // Access to s_ToBeGenerated
    static std::mutex s_RemoveQueueMutex;    // Access to s_ToBeRemoved
    static std::mutex s_SerializeQueueMutex; // Access to s_ToBeSerialized
    static std::mutex s_ComputeQueueAccess;  // Access to s_ComputeQueue

    static std::condition_variable s_GenerateCondition;
    static std::condition_variable s_SerializeCondition;

    static std::atomic<int> s_NumReadersActive;
    static std::atomic<int> s_NumWritersActive;

    static std::unordered_set<glm::ivec3> s_ToBeGenerated;
    static std::unordered_set<glm::ivec3> s_ToBeRemoved;
    static std::queue<glm::ivec3> s_ToBeSerialized;

    static std::unordered_map<glm::ivec3, Chunk>* s_ActiveChunks;

    static bool s_Running;

    static int s_Seed;
    static VoxelGenerationPushConstants s_GenerationPushConstants;

    static Buffer s_GeneratedVoxels;
    static Buffer s_StagingBuffer;
    static VkPipeline s_GenerationPipeline;
    static VkPipelineLayout s_GenerationPipelineLayout;

    static VmaAllocator s_Allocator;
    static VkDevice s_Device;
    static VkQueue s_ComputeQueue;
    static VkCommandPool s_CommandPool;

    static VkCommandBuffer s_CommandBuffer;
    static VkCommandBuffer s_CopyCommandBuffer;

    static VkFence s_GeneratedFence;
    static VkFence s_CopyFence;

  private:
    static void generateNextChunk();
    static void generateChunk(glm::ivec3 chunkPosition);
    static void serializeChunk();

    static void copyStagingToChunk(glm::ivec3 chunkPosition, size_t size);

    static void createSVO(Buffer* buffer, size_t count);
    static void createStaging(size_t count);
};
