#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <glm/gtx/hash.hpp>

#include "Brick.hpp"
#include "Buffer.hpp"
#include "Queue.hpp"

#define SUPERBRICK_SIZE 16

struct SuperBrickEntry {
    uint32_t loaded : 1;
    uint32_t empty_flag : 1;
    uint32_t flags : 2;
    uint32_t pointer : 12;
    uint32_t _ : 16;
};

struct SuperBrickStruct {
    std::array<SuperBrickEntry, 16 * 16 * 16> data;
    VkDeviceAddress bricks;
    VkDeviceAddress colour;
};

struct GenerationPushConstants {
    // uint32_t seed;
    glm::ivec3 worldPosition;
    int _;
    VkDeviceAddress data;
    VkDeviceAddress colours;
};

class SuperBrick
{
  public:
    SuperBrick();

    void init(VkDevice device, VmaAllocator allocator, Queue* computeQueue);
    void free();

    void generateBrick(glm::ivec3 position);
    void generateBrickFromIndex(uint32_t index);
    VkDeviceAddress getBrickmap() { return m_Brickmap.getDeviceAddress(m_Device); }

    SuperBrickStruct getStruct();

  private:
    bool m_Initialized = false;
    std::unordered_map<glm::ivec3, Brick> m_Bricks;
    Buffer m_Brickmap;
    Buffer m_Staging;

    std::vector<Buffer> m_Colours;
    Buffer m_ColourMap;

    SuperBrickStruct m_Struct;
    bool m_HasChanged = true;

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

  private:
    void generateStaging(size_t size);
};
