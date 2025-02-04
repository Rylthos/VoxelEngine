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
#include "spdlog/common.h"

#define MAX_BRICKS_PER_DISPATCH 6

struct GenerationPushConstants {
    std::array<glm::vec4, MAX_BRICKS_PER_DISPATCH> brickPosition;
    VkDeviceAddress colours;
};

class ChunkGenerator {
  public:
    ChunkGenerator() = delete;

    static void init(VkDevice m_Device, VmaAllocator allocator, Queue* computeQueue);
    static void free();

    static void addChunks(std::unordered_map<glm::ivec3, Chunk>* chunks);

    static void requestBrick(LocalChunkPosition position);

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
    const inline static size_t s_NumGenerationThreads = 8;

    inline static std::condition_variable_any s_CanGenerate;
    inline static PROF_LOCKABLE_MUTEX(std::mutex, s_GeneratedQueueLock, "Generated Queue Lock");
    inline static PROF_LOCKABLE_MUTEX(std::mutex, s_EnqueuedLock, "Enqueued Lock");

    inline static std::deque<glm::ivec3> s_ToBeGenerated;
    inline static std::unordered_set<glm::ivec3> s_Enqueued;

  private:
    static glm::ivec3 localToWorldIndex(LocalChunkPosition position);

    static LocalChunkPosition worldToLocalIndex(glm::ivec3 worldIndex);

    static void generationLoop(size_t id);
};
