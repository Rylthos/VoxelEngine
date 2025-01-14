#include "SuperBrick.hpp"
#include "Brick.hpp"
#include "Buffer.hpp"
#include <pthread.h>
#include <vulkan/vulkan_core.h>

#include "SceneManager.hpp"
#include "ShaderModule.hpp"
#include "VkCheck.hpp"
#include "imgui.h"

SuperBrick::SuperBrick() {}

void SuperBrick::init(VkDevice device, VmaAllocator allocator, Queue* computeQueue)
{
    m_Device = device;
    m_Allocator = allocator;
    m_ComputeQueue = computeQueue;

    for (size_t i = 0; i < m_Struct.data.size(); i++)
    {
        m_Struct.data[i] = {
            .loaded = 0,
            .empty_flag = 0,
        };
    }

    m_GeneratedData.create(
        m_Allocator, sizeof(uint32_t) + sizeof(uint64_t) * 8,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    m_GeneratedColourData.create(
        m_Allocator, sizeof(glm::vec4) * BRICK_SIZE * BRICK_SIZE * BRICK_SIZE,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

    {
        VkCommandPoolCreateInfo commandPoolCI{};
        commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolCI.pNext = nullptr;
        commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolCI.queueFamilyIndex = m_ComputeQueue->queueFamily;

        VK_CHECK(vkCreateCommandPool(m_Device, &commandPoolCI, nullptr, &m_CommandPool));

        VkCommandBufferAllocateInfo commandBufferAI{};
        commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAI.pNext = nullptr;
        commandBufferAI.commandPool = m_CommandPool;
        commandBufferAI.commandBufferCount = 1;
        commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(m_Device, &commandBufferAI, &m_CommandBuffer));
    }

    {
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(GenerationPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkPipelineLayoutCreateInfo computeLayoutCI{};
        computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayoutCI.pNext = nullptr;
        computeLayoutCI.setLayoutCount = 0;
        computeLayoutCI.pSetLayouts = nullptr;
        computeLayoutCI.pushConstantRangeCount = 1;
        computeLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(
            vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr, &m_GeneratePipelineLayout));

        ShaderModule voxelShader;
        voxelShader.create("res/shaders/GenerateBrickmap.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = voxelShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_GeneratePipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &m_GeneratePipeline));
    }

    {
        VkFenceCreateInfo fenceCI{};
        fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCI.pNext = nullptr;
        fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateFence(m_Device, &fenceCI, nullptr, &m_GenerationFence));
    }

    m_Running = true;
    m_GenerationThread = std::thread(&SuperBrick::generateBrickLoop, this);

    m_Initialized = true;
}

void SuperBrick::free()
{
    if (!m_Initialized) return;

    m_Running = false;
    m_CanGenerate.notify_one();
    m_GenerationThread.join();

    m_Brickmap.free();
    m_Staging.free();
    m_ColourMap.free();

    for (auto& c : m_Colours)
    {
        c.free();
    }
    m_Colours.clear();

    m_GeneratedColourData.free();
    m_GeneratedData.free();
    vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
    vkDestroyFence(m_Device, m_GenerationFence, nullptr);
    vkDestroyPipeline(m_Device, m_GeneratePipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_GeneratePipelineLayout, nullptr);
}

void SuperBrick::addBrickToQueue(glm::ivec3 position)
{
    std::lock_guard<std::mutex> m_Lock(m_QueueLock);

    if (m_Enqueued.contains(position)) return;

    if (m_Generated.contains(position)) return;

    m_Enqueued.insert(position);
    m_Generated.insert(position);
    m_ToBeGenerated.push_back(position);

    m_CanGenerate.notify_one();
}

void SuperBrick::addBrickToQueue(uint32_t index)
{
    glm::ivec3 position{ 0 };
    position.x = index % SUPERBRICK_SIZE;
    position.z = (index / SUPERBRICK_SIZE) % SUPERBRICK_SIZE;
    position.y = (index / (SUPERBRICK_SIZE * SUPERBRICK_SIZE)) % SUPERBRICK_SIZE;

    addBrickToQueue(position);
}

SuperBrickStruct SuperBrick::getStruct()
{
    if (m_HasChanged && m_Enqueued.size() == 0)
    {
        std::unique_lock<std::mutex> lock(m_BufferLock);

        m_HasChanged = false;

        for (auto& c : m_Colours)
        {
            c.free();
        }
        m_Colours.clear();

        m_Brickmap.free();
        m_ColourMap.free();

        std::vector<BrickStruct> bricks;
        std::vector<VkDeviceAddress> colours;

        m_Colours.reserve(m_Bricks.size());
        bricks.reserve(m_Bricks.size());

        for (auto p : m_Bricks)
        {
            auto brick = p.second.getStruct();
            size_t index = p.first.x + p.first.z * SUPERBRICK_SIZE +
                           p.first.y * SUPERBRICK_SIZE * SUPERBRICK_SIZE;
            if (brick.has_value())
            {
                m_Struct.data[index].pointer = bricks.size();
                m_Struct.data[index].loaded = 1;
                m_Struct.data[index].empty_flag = 0;

                size_t colourIndex = m_Colours.size();
                brick->colourPtr = colourIndex;

                bricks.push_back(brick.value());

                auto c = p.second.getColours();
                size_t size = sizeof(glm::vec4) * c.size();
                m_Colours.emplace_back();
                m_Colours[colourIndex].create(m_Allocator, size,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                              VMA_MEMORY_USAGE_GPU_ONLY);
                generateStaging(size);
                m_Staging.copyFromData_CPUOnly<glm::vec4>(c);
                m_Colours[colourIndex].copyFromBuffer(m_Staging, size);

                colours.push_back(m_Colours[colourIndex].getDeviceAddress(m_Device));
            }
            else
            {
                m_Struct.data[index].loaded = 1;
                m_Struct.data[index].empty_flag = 1;
            }
        }

        {
            size_t size = bricks.size() * sizeof(BrickStruct);
            m_Brickmap.create(m_Allocator, (size > 0) ? size : 1,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                              VMA_MEMORY_USAGE_GPU_ONLY);
            if (size != 0)
            {
                generateStaging(size);
                m_Staging.copyFromData_CPUOnly<BrickStruct>(bricks);
                m_Brickmap.copyFromBuffer(m_Staging, size);
            }
        }

        {
            size_t size = colours.size() * sizeof(VkDeviceAddress);
            m_ColourMap.create(m_Allocator, (size > 0) ? size : 1,
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                               VMA_MEMORY_USAGE_GPU_ONLY);
            if (size != 0)
            {
                generateStaging(size);
                m_Staging.copyFromData_CPUOnly<VkDeviceAddress>(colours);
                m_ColourMap.copyFromBuffer(m_Staging, size);
            }
        }

        m_Struct.bricks = m_Brickmap.getDeviceAddress(m_Device);
        m_Struct.colour = m_ColourMap.getDeviceAddress(m_Device);
    }
    return m_Struct;
}

void SuperBrick::generateStaging(size_t size)
{
    if (m_Staging.getSize() >= size)
    {
        return;
    }

    m_Staging.free();
    m_Staging.create(m_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

void SuperBrick::generateBrickLoop()
{
    while (m_Running)
    {
        glm::ivec3 position;
        {
            std::unique_lock<std::mutex> lock(m_QueueLock);
            if (m_ToBeGenerated.size() == 0)
            {
                spdlog::info("Thread waiting");
                m_CanGenerate.wait(lock, [this] { return !m_ToBeGenerated.empty() || !m_Running; });
            }
            spdlog::info("Thread running");

            position = m_ToBeGenerated.front();
        }

        if (!m_Running) return;

        m_HasChanged = true;
        VK_CHECK(vkResetFences(m_Device, 1, &m_GenerationFence));
        VK_CHECK(vkResetCommandBuffer(m_CommandBuffer, 0));

        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBI.pNext = nullptr;
        commandBufferBI.pInheritanceInfo = nullptr;
        commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(m_CommandBuffer, &commandBufferBI));
        {
            vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_GeneratePipeline);

            GenerationPushConstants pushConstants;
            pushConstants.worldPosition = position * BRICK_SIZE;
            pushConstants.data = m_GeneratedData.getDeviceAddress(m_Device);
            pushConstants.colours = m_GeneratedColourData.getDeviceAddress(m_Device);

            vkCmdPushConstants(m_CommandBuffer, m_GeneratePipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants),
                               &pushConstants);

            vkCmdDispatch(m_CommandBuffer, 1, 1, 1);
        }
        VK_CHECK(vkEndCommandBuffer(m_CommandBuffer));

        VkCommandBufferSubmitInfo commandBufferSI{};
        commandBufferSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferSI.pNext = nullptr;
        commandBufferSI.commandBuffer = m_CommandBuffer;
        commandBufferSI.deviceMask = 0;

        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = nullptr;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferSI;

        VK_CHECK(vkQueueSubmit2(m_ComputeQueue->queue, 1, &submitInfo, m_GenerationFence));
        VK_CHECK(vkWaitForFences(m_Device, 1, &m_GenerationFence, true, 1e10));

        const uint32_t* data = (const uint32_t*)(m_GeneratedData.getAllocationInfo().pMappedData);
        const uint32_t solidVoxels = *data;
        const uint64_t* mask = (const uint64_t*)(data + 1);

        const glm::vec4* colour_data =
            (const glm::vec4*)(m_GeneratedColourData.getAllocationInfo().pMappedData);

        Brick brick;
        if (solidVoxels != 0)
        {
            for (int y = 0; y < BRICK_SIZE; y++)
            {
                for (int z = 0; z < BRICK_SIZE; z++)
                {
                    for (int x = 0; x < BRICK_SIZE; x++)
                    {
                        uint32_t index = x + z * BRICK_SIZE + y * BRICK_SIZE * BRICK_SIZE;
                        if (colour_data[index].a >= 0)
                        {
                            brick.setVoxel({ x, y, z }, colour_data[index]);
                        }
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_BufferLock);
            m_Bricks[position] = brick;
        }

        {
            std::lock_guard<std::mutex> lock(m_QueueLock);
            m_ToBeGenerated.pop_front();
            m_Enqueued.erase(position);
        }
    }
}
