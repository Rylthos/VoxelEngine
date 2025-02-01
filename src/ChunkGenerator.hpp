#pragma once

#include <unordered_map>
#include <vulkan/vulkan.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_set>

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include <vulkan/vulkan.h>

#include "Chunk.hpp"
#include "Profilling.hpp"
#include "Queue.hpp"

struct GenerationPushConstants {
    glm::ivec3 brickIndex;
    int _1;
    glm::ivec3 worldPosition;
    int _2;
    VkDeviceAddress data;
    VkDeviceAddress colours;
};

struct GenerationData {
    int colourSumR;
    int colourSumG;
    int colourSumB;
};

class ChunkGenerator {
  public:
    ChunkGenerator() = delete;

    static void init(VkDevice m_Device, VmaAllocator allocator, Queue* computeQueue);
    static void free();

    static void addChunks(std::unordered_map<glm::ivec3, Chunk>* chunks);

    static void requestBrick(
        glm::ivec3 chunkIndex, glm::ivec3 superBrickIndex, glm::ivec3 brickIndex);

    static size_t getQueueSize() { return s_ToBeGenerated.size(); }

  private:
    inline static VkDevice s_Device;
    inline static VmaAllocator s_Allocator;

    inline static std::unordered_map<glm::ivec3, Chunk>* s_Chunks;

    inline static Queue* s_ComputeQueue;
    inline static VkPipeline s_GeneratePipeline;
    inline static VkPipelineLayout s_GeneratePipelineLayout;

    inline static bool s_Running = false;
    inline static std::vector<std::thread> s_GenerationThreads;
    inline static size_t s_NumGenerationThreads = 4;

    inline static std::condition_variable_any s_CanGenerate;
    inline static PROF_LOCKABLE_MUTEX(std::mutex, s_GeneratedQueueLock, "Generated Queue Lock");
    inline static PROF_LOCKABLE_MUTEX(std::mutex, s_EnqueuedLock, "Enqueued Lock");

    inline static std::deque<glm::ivec3> s_ToBeGenerated;
    inline static std::unordered_set<glm::ivec3> s_Enqueued;

  private:
    static glm::ivec3 localToWorldIndex(
        glm::ivec3 chunkIndex, glm::ivec3 superBrickIndex, glm::ivec3 brickIndex);

    static std::tuple<glm::ivec3, glm::ivec3, glm::ivec3> worldToLocalIndex(glm::ivec3 worldIndex);

    static void generationLoop(size_t id);
};
