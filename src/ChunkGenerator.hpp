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
    SVONODE_Im_SOLID = 1 << 0,  // All Smaller nodes are equal
    SVONODE_Im_PARENT = 1 << 1, // Has Smaller Nodes
    SVONODE_Im_AIR = 1 << 2     // Is air
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
    static ChunkGenerator& getInstance();
    static void init(uint32_t chunkSize, VmaAllocator allocator, VkDevice device,
                     VkQueue computeQueue, uint32_t computeQueueFamily, Chunks* chunks);
    static void free();

    int getWorldSeed() { return m_Seed; }
    void setWorldSeed(int seed) { m_Seed = seed; }

    void stopRunning()
    {
        m_Running = false;
        m_GenerateCondition.notify_all();
        m_SerializeCondition.notify_all();
    }

    size_t getGenerationQueueSize() { return m_ToBeGenerated.size(); }
    size_t getRemovalQueueSize() { return m_ToBeRemoved.size(); }
    size_t getSerializationQueueSize() { return m_ToBeSerialized.size(); }

    void addChunkToQueue(glm::ivec3 chunk);

    void removeChunk(glm::ivec3 pos);

    void generateChunkLoop();

  private:
    PROF_LOCKABLE_MUTEX(std::mutex, m_GenerateQueueMutex, "Generate Queue");
    PROF_LOCKABLE_MUTEX(std::mutex, m_RemoveQueueMutex, "Removal Queue");
    PROF_LOCKABLE_MUTEX(std::mutex, m_SerializeQueueMutex, "Serialize Queue");
    PROF_LOCKABLE_MUTEX(std::mutex, m_ComputeQueueAccess, "VkAccess compute Queue");

    std::condition_variable_any m_GenerateCondition;
    std::condition_variable_any m_SerializeCondition;

    std::unordered_set<glm::ivec3> m_ToBeGenerated;
    std::unordered_set<glm::ivec3> m_ToBeRemoved;
    std::deque<glm::ivec3> m_ToBeSerialized;

    Chunks* m_ActiveChunks;

    std::array<std::thread, SERIALISATION_THREADS> m_SerialisationThreads;

    bool m_Running;

    int m_Seed = 0;
    uint32_t m_Depth;
    VoxelGenerationPushConstants m_GenerationPushConstants;

    VkDescriptorPool m_DescriptorPool;

    VkDescriptorSetLayout m_MipmapImageSetLayout;
    VkDescriptorSet m_MipmapImageSet;

    VkDescriptorSetLayout m_MipmapDataSetLayout;
    VkDescriptorSet m_MipmapDataSet;

    Image m_GeneratedVoxels;
    Buffer m_SerializeBuffer;
    std::vector<VkImageView> m_GeneratedImageViews;

    Buffer m_StagingBuffer;

    VkPipeline m_GenerationPipeline;
    VkPipelineLayout m_GenerationPipelineLayout;

    VkPipeline m_MipmapPipeline;
    VkPipelineLayout m_MipmapPipelineLayout;

    VkPipeline m_SerializePipeline;
    VkPipelineLayout m_SerializePipelineLayout;

    VmaAllocator m_Allocator;
    VkDevice m_Device;
    VkQueue m_ComputeQueue;
    VkCommandPool m_CommandPool;

    VkCommandBuffer m_CommandBuffer;
    VkCommandBuffer m_CopyCommandBuffer;

    VkFence m_GeneratedFence;
    VkFence m_CopyFence;

  private:
    ChunkGenerator() {}
    ~ChunkGenerator() {}

    void initResources(uint32_t chunkSize, VmaAllocator allocator, VkDevice device,
                       VkQueue computeQueue, uint32_t computeQueueFamily, Chunks* chunks);

    void freeResources();

    void generateNextChunk();
    void generateChunk(glm::ivec3 chunkPosition);
    void serializeChunk(uint32_t id);

    void transitionImages();
    void copyStagingToBuffer(Buffer* buffer);
    void copyStagingToChunk(glm::ivec3 chunkPosition, size_t size);

    void createSVO(Buffer* buffer, size_t count);
    void createStaging(size_t count, size_t elem_size = sizeof(SVONode));
};
