#include "ChunkGenerator.hpp"

#include <glm/gtx/hash.hpp>
#include <glm/gtx/string_cast.hpp>

#include <format>

#include "Chunk.hpp"
#include "Descriptors.hpp"
#include "Profilling.hpp"
#include "SceneManager.hpp"
#include "ShaderModule.hpp"
#include "SuperBrick.hpp"
#include "Timer.hpp"
#include "VkCheck.hpp"

void ChunkGenerator::init(VkDevice device, VmaAllocator allocator, Queue* computeQueue)
{
    s_Device = device;
    s_Allocator = allocator;
    s_ComputeQueue = computeQueue;

    {
        VkPushConstantRange pushConstant {};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(GenerationPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkPipelineLayoutCreateInfo computeLayoutCI {};
        computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayoutCI.pNext = nullptr;
        computeLayoutCI.setLayoutCount = 0;
        computeLayoutCI.pSetLayouts = nullptr;
        computeLayoutCI.pushConstantRangeCount = 1;
        computeLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(
            vkCreatePipelineLayout(s_Device, &computeLayoutCI, nullptr, &s_GeneratePipelineLayout));

        ShaderModule voxelShader;
        voxelShader.create("res/shaders/GenerateBrickmap.comp.spv", s_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI {};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = voxelShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI {};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = s_GeneratePipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(
            s_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr, &s_GeneratePipeline));
    }

    s_Running = true;

    s_GenerationThreads.resize(s_NumGenerationThreads);
    for (size_t i = 0; i < s_NumGenerationThreads; i++) {
        s_GenerationThreads[i] = std::thread(&ChunkGenerator::generationLoop, i);
    }
}

void ChunkGenerator::free()
{
    s_Running = false;
    s_CanGenerate.notify_all();
    for (auto& thread : s_GenerationThreads) {
        thread.join();
    }

    vkDestroyPipeline(s_Device, s_GeneratePipeline, nullptr);
    vkDestroyPipelineLayout(s_Device, s_GeneratePipelineLayout, nullptr);
}

void ChunkGenerator::addChunks(std::unordered_map<glm::ivec3, Chunk>* chunks) { s_Chunks = chunks; }

void ChunkGenerator::requestBrick(
    glm::ivec3 chunkIndex, glm::ivec3 superBrickIndex, glm::ivec3 brickIndex)
{
    glm::ivec3 worldIndex = localToWorldIndex(chunkIndex, superBrickIndex, brickIndex);

    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(s_GeneratedQueueLock);
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(s_EnqueuedLock);

    if (s_Enqueued.contains(worldIndex))
        return;

    s_Enqueued.insert(worldIndex);
    s_ToBeGenerated.push_back(worldIndex);

    s_CanGenerate.notify_one();
}

glm::ivec3 ChunkGenerator::localToWorldIndex(
    glm::ivec3 chunkIndex, glm::ivec3 superBrickIndex, glm::ivec3 brickIndex)
{
    return chunkIndex * CHUNK_SIZE * SUPERBRICK_SIZE + superBrickIndex * SUPERBRICK_SIZE
        + brickIndex;
}

std::tuple<glm::ivec3, glm::ivec3, glm::ivec3> ChunkGenerator::worldToLocalIndex(
    glm::ivec3 worldIndex)
{
    glm::ivec3 brickIndex = worldIndex % SUPERBRICK_SIZE;
    glm::ivec3 superBrickIndex = (worldIndex / SUPERBRICK_SIZE) % CHUNK_SIZE;
    glm::ivec3 chunkIndex = (worldIndex / (SUPERBRICK_SIZE * CHUNK_SIZE));

    return { chunkIndex, superBrickIndex, brickIndex };
}

void ChunkGenerator::generationLoop(size_t id)
{
    Buffer generatedData;
    Buffer generatedColour;
    generatedData.create(s_Allocator, sizeof(GenerationData),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

    generatedColour.create(s_Allocator, sizeof(glm::vec4) * BRICK_SIZE * BRICK_SIZE * BRICK_SIZE,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

    VkCommandPool commandPool;
    VkCommandPoolCreateInfo commandPoolCI {};
    commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCI.pNext = nullptr;
    commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCI.queueFamilyIndex = s_ComputeQueue->queueFamily;

    VK_CHECK(vkCreateCommandPool(s_Device, &commandPoolCI, nullptr, &commandPool));

    VkCommandBuffer commandBuffer;

    VkCommandBufferAllocateInfo commandBufferAI {};
    commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAI.pNext = nullptr;
    commandBufferAI.commandPool = commandPool;
    commandBufferAI.commandBufferCount = 1;
    commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(s_Device, &commandBufferAI, &commandBuffer));

    std::string timerString = std::format("Brick Generate: {}", id);

    VkFence generationFence;
    {
        VkFenceCreateInfo fenceCI {};
        fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCI.pNext = nullptr;
        fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateFence(s_Device, &fenceCI, nullptr, &generationFence));
    }
    while (s_Running) {
        glm::ivec3 position;
        {
            std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lock(s_GeneratedQueueLock);
            while (s_ToBeGenerated.size() == 0) {
                s_CanGenerate.wait(lock, [&] { return !s_ToBeGenerated.empty() || !s_Running; });

                if (!s_Running)
                    break;
            }

            position = s_ToBeGenerated.front();
            s_ToBeGenerated.pop_front();
        }
        if (!s_Running)
            break;

        VK_CHECK(vkResetFences(s_Device, 1, &generationFence));
        VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

        Timer::startTimer(timerString);
        VkCommandBufferBeginInfo commandBufferBI {};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBI.pNext = nullptr;
        commandBufferBI.pInheritanceInfo = nullptr;
        commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        {
            std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk(s_ComputeQueue->queueMutex);
            VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBI));
            {
                vkCmdBindPipeline(
                    commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s_GeneratePipeline);

                GenerationPushConstants pushConstants;
                pushConstants.brickIndex = position;
                pushConstants.worldPosition = position * BRICK_SIZE;
                pushConstants.data = generatedData.getDeviceAddress(s_Device);
                pushConstants.colours = generatedColour.getDeviceAddress(s_Device);

                vkCmdPushConstants(commandBuffer, s_GeneratePipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

                vkCmdDispatch(commandBuffer, 1, 1, 1);
            }
            VK_CHECK(vkEndCommandBuffer(commandBuffer));

            VkCommandBufferSubmitInfo commandBufferSI {};
            commandBufferSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            commandBufferSI.pNext = nullptr;
            commandBufferSI.commandBuffer = commandBuffer;
            commandBufferSI.deviceMask = 0;

            VkSubmitInfo2 submitInfo {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            submitInfo.pNext = nullptr;
            submitInfo.commandBufferInfoCount = 1;
            submitInfo.pCommandBufferInfos = &commandBufferSI;

            VK_CHECK(vkQueueSubmit2(s_ComputeQueue->queue, 1, &submitInfo, generationFence));
        }
        VK_CHECK(vkWaitForFences(s_Device, 1, &generationFence, true, 1e10));

        const GenerationData* data
            = (const GenerationData*)(generatedData.getAllocationInfo().pMappedData);

        const glm::vec4* colour_data
            = (const glm::vec4*)(generatedColour.getAllocationInfo().pMappedData);

        Brick brick;
        if (data->solidVoxels != 0) {
            for (int y = 0; y < BRICK_SIZE; y++) {
                for (int z = 0; z < BRICK_SIZE; z++) {
                    for (int x = 0; x < BRICK_SIZE; x++) {
                        uint32_t index = x + z * BRICK_SIZE + y * BRICK_SIZE * BRICK_SIZE;
                        glm::ivec3 voxelIndex = { x, y, z };
                        if (colour_data[index].a >= 0) {
                            brick.setVoxel(voxelIndex, colour_data[index], true);
                        }
                    }
                }
            }
        }

        {
            // if (s_QueuedChanges.contains(position)) {
            //     auto copy = m_QueuedChanges[position];
            //     for (auto p : copy) {
            //         if (std::holds_alternative<ERASE_OP>(p.second.first)) {
            //             brick.setAir(p.first);
            //         } else if (std::holds_alternative<PLACE_OP>(p.second.first)) {
            //             brick.setVoxel(
            //                 p.first, std::get<PLACE_OP>(p.second.first), p.second.second);
            //         }
            //     }
            //
            //     m_QueuedChanges.erase(position);
            // }
        }
        Timer::stopTimer(timerString);

        // {
        //     std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock(s_BufferLock);
        //     m_Bricks[position] = brick;
        // }

        {
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(s_EnqueuedLock);
            s_Enqueued.erase(position);
        }

        auto localPosition = worldToLocalIndex(position);
        (*s_Chunks)[std::get<0>(localPosition)].loadBrick(localPosition, brick);
    }

    vkDestroyFence(s_Device, generationFence, nullptr);

    generatedData.free();
    generatedColour.free();

    vkDestroyCommandPool(s_Device, commandPool, nullptr);
}
