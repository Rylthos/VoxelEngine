#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

#include "Profilling.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vulkan/vulkan.h>

#include "Chunk.hpp"
#include "Image.hpp"

#define SERIALISATION_THREADS 1

struct Chunks;

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
};

struct VoxelMipmapBuffer {
    uint32_t counter;
};

struct VoxelMipmapPushConstants {
    int32_t sourceLevel;
};

struct VoxelSerializePushConstants {
    uint32_t currentLevel;
    uint32_t numNodes;
    VkDeviceAddress targetBuffer;
};

class ChunkGenerator
{
  public:
    static void initResources(uint32_t chunkSize, VmaAllocator allocator, VkDevice device,
                              VkQueue computeQueue, uint32_t computeQueueFamily, Chunks* chunks);
    static void freeResources();

    static int getWorldSeed() { return s_Seed; }
    static void setWorldSeed(int seed) { s_Seed = seed; }

    static void stopRunning()
    {
        s_Running = false;
        s_GenerateCondition.notify_all();
        s_SerializeCondition.notify_all();
    }

    static size_t getGenerationQueueSize() { return s_ToBeGenerated.size(); }
    static size_t getRemovalQueueSize() { return s_ToBeRemoved.size(); }
    static size_t getSerializationQueueSize() { return s_ToBeSerialized.size(); }

    static void addChunkToQueue(glm::ivec3 chunk);

    static void removeChunk(glm::ivec3 pos);

    static void generateChunkLoop();

  private:
    static PROF_lockable_T<std::mutex> s_GenerateQueueMutex;  // Access to s_ToBeGenerated
    static PROF_lockable_T<std::mutex> s_RemoveQueueMutex;    // Access to s_ToBeRemoved
    static PROF_lockable_T<std::mutex> s_SerializeQueueMutex; // Access to s_ToBeSerialized
    static PROF_lockable_T<std::mutex> s_ComputeQueueAccess;  // Access to s_ComputeQueue

    static std::condition_variable_any s_GenerateCondition;
    static std::condition_variable_any s_SerializeCondition;

    static std::unordered_set<glm::ivec3> s_ToBeGenerated;
    static std::unordered_set<glm::ivec3> s_ToBeRemoved;
    static std::deque<glm::ivec3> s_ToBeSerialized;

    static Chunks* s_ActiveChunks;

    static std::array<std::thread, SERIALISATION_THREADS> s_SerialisationThreads;

    static bool s_Running;

    static int s_Seed;
    static uint32_t s_Depth;
    static VoxelGenerationPushConstants s_GenerationPushConstants;

    static VkDescriptorPool s_DescriptorPool;

    static VkDescriptorSetLayout s_MipmapImageSetLayout;
    static VkDescriptorSet s_MipmapImageSet;

    static VkDescriptorSetLayout s_MipmapDataSetLayout;
    static VkDescriptorSet s_MipmapDataSet;

    static Image s_GeneratedVoxels;
    static Buffer s_SerializeBuffer;
    static std::vector<VkImageView> s_GeneratedImageViews;

    static Buffer s_StagingBuffer;

    static VkPipeline s_GenerationPipeline;
    static VkPipelineLayout s_GenerationPipelineLayout;

    static VkPipeline s_MipmapPipeline;
    static VkPipelineLayout s_MipmapPipelineLayout;

    static VkPipeline s_SerializePipeline;
    static VkPipelineLayout s_SerializePipelineLayout;

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
    static void serializeChunk(uint32_t id);

    static void transitionImages();
    static void copyStagingToBuffer(Buffer* buffer);
    static void copyStagingToChunk(glm::ivec3 chunkPosition, size_t size);

    static void createSVO(Buffer* buffer, size_t count);
    static void createStaging(size_t count, size_t elem_size = sizeof(SVONode));
};
