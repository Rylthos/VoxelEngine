#include "ChunkGenerator.hpp"

#include <format>

#include "ShaderModule.hpp"
#include "VkCheck.hpp"

std::mutex ChunkGenerator::s_QueueMutex;
std::mutex ChunkGenerator::s_QueueSubmitMutex;
std::condition_variable ChunkGenerator::s_Condition;
std::queue<Chunk*> ChunkGenerator::s_ToBeGenerated;

int ChunkGenerator::s_Seed = 0;
VoxelGenerationPushConstants ChunkGenerator::s_GenerationPushConstants;

Buffer ChunkGenerator::s_GeneratedVoxels;
Buffer ChunkGenerator::s_StagingBuffer;
VkPipeline ChunkGenerator::s_GenerationPipeline;
VkPipelineLayout ChunkGenerator::s_GenerationPipelineLayout;

bool ChunkGenerator::s_Running;

VmaAllocator ChunkGenerator::s_Allocator;
VkDevice ChunkGenerator::s_Device;
VkQueue ChunkGenerator::s_ComputeQueue;
VkCommandPool ChunkGenerator::s_CommandPool;
VkCommandBuffer ChunkGenerator::s_CommandBuffer;
VkCommandBuffer ChunkGenerator::s_CopyCommandBuffer;

VkFence ChunkGenerator::s_GeneratedFence;
VkFence ChunkGenerator::s_CopyFence;

void ChunkGenerator::initResources(uint32_t chunkSize, VmaAllocator allocator, VkDevice device,
                                   VkQueue computeQueue, uint32_t computeQueueFamily)
{
    s_Allocator = allocator;
    s_Device = device;
    s_ComputeQueue = computeQueue;

    VkCommandPoolCreateInfo commandPoolCI{};
    commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCI.pNext = nullptr;
    commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCI.queueFamilyIndex = computeQueueFamily;

    VK_CHECK(vkCreateCommandPool(s_Device, &commandPoolCI, nullptr, &s_CommandPool));

    VkCommandBufferAllocateInfo commandBufferAI{};
    commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAI.pNext = nullptr;
    commandBufferAI.commandPool = s_CommandPool;
    commandBufferAI.commandBufferCount = 1;
    commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(s_Device, &commandBufferAI, &s_CommandBuffer));
    VK_CHECK(vkAllocateCommandBuffers(s_Device, &commandBufferAI, &s_CopyCommandBuffer));

    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.pNext = nullptr;
    fenceCI.flags = 0;

    VK_CHECK(vkCreateFence(s_Device, &fenceCI, nullptr, &s_GeneratedFence));
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(s_Device, &fenceCI, nullptr, &s_CopyFence));

    s_GeneratedVoxels.create(s_Allocator, chunkSize * chunkSize * chunkSize * sizeof(Voxel),
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             VMA_MEMORY_USAGE_GPU_TO_CPU);

    { // Pipeline Generation
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VoxelGenerationPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkPipelineLayoutCreateInfo generationLayoutCI{};
        generationLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        generationLayoutCI.pNext = nullptr;
        generationLayoutCI.setLayoutCount = 0;
        generationLayoutCI.pSetLayouts = nullptr;
        generationLayoutCI.pushConstantRangeCount = 1;
        generationLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(vkCreatePipelineLayout(s_Device, &generationLayoutCI, nullptr,
                                        &s_GenerationPipelineLayout));

        ShaderModule voxelShader;
        voxelShader.create("res/shaders/Generation.comp.spv", s_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = voxelShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = s_GenerationPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(s_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &s_GenerationPipeline));
    }

    s_GenerationPushConstants.seed = s_Seed;
    s_GenerationPushConstants.cutoff = 0.0;
    s_GenerationPushConstants.p10 = 10;
    s_GenerationPushConstants.p50 = 50;
    s_GenerationPushConstants.p100 = 100;
}

void ChunkGenerator::freeResources()
{
    s_StagingBuffer.free();
    s_GeneratedVoxels.free();

    vkDestroyPipeline(s_Device, s_GenerationPipeline, nullptr);
    vkDestroyPipelineLayout(s_Device, s_GenerationPipelineLayout, nullptr);
    vkDestroyFence(s_Device, s_GeneratedFence, nullptr);
    vkDestroyFence(s_Device, s_CopyFence, nullptr);
    vkDestroyCommandPool(s_Device, s_CommandPool, nullptr);
}

void ChunkGenerator::addChunkToQueue(Chunk* chunk)
{
    std::unique_lock<std::mutex> lk(s_QueueMutex);
    s_ToBeGenerated.push(chunk);
    s_Condition.notify_one();
}

void ChunkGenerator::generateChunkLoop()
{
    s_Running = true;
    spdlog::trace("Started Chunk Generation");
    while (s_Running)
    {
        generateNextChunk();
    }
}

void ChunkGenerator::generateNextChunk()
{
    std::unique_lock<std::mutex> lk(s_QueueSubmitMutex);

    {
        std::unique_lock<std::mutex> lk(s_QueueMutex);
        if (s_ToBeGenerated.empty())
        {
            s_Condition.wait(lk, [] { return !s_ToBeGenerated.empty() || !s_Running; });
        }
    }
    if (!s_Running) return;

    Chunk* topChunk = s_ToBeGenerated.front();

    generateChunk(topChunk);

    {
        std::unique_lock<std::mutex> lk(s_QueueMutex);
        s_ToBeGenerated.pop();
    }
}

void ChunkGenerator::generateChunk(Chunk* chunk)
{
    VK_CHECK(vkResetCommandBuffer(s_CommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(s_CommandBuffer, &commandBufferBI));

    size_t dimension = chunk->getDimensions();
    s_GenerationPushConstants.dimension = dimension;
    s_GenerationPushConstants.size = 1.0f;
    s_GenerationPushConstants.targetBuffer = s_GeneratedVoxels.getDeviceAddress(s_Device);
    s_GenerationPushConstants.origin = glm::vec4(chunk->getPosition(), 0.);

    vkCmdBindPipeline(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s_GenerationPipeline);

    vkCmdPushConstants(s_CommandBuffer, s_GenerationPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(VoxelGenerationPushConstants), &s_GenerationPushConstants);

    vkCmdDispatch(s_CommandBuffer, dimension / 4, dimension / 4, dimension / 4);

    VK_CHECK(vkEndCommandBuffer(s_CommandBuffer));

    VkCommandBufferSubmitInfo commandBufferSI{};
    commandBufferSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandBufferSI.pNext = nullptr;
    commandBufferSI.commandBuffer = s_CommandBuffer;
    commandBufferSI.deviceMask = 0;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.pNext = nullptr;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferSI;

    VK_CHECK(vkQueueSubmit2(s_ComputeQueue, 1, &submitInfo, s_GeneratedFence));
    spdlog::debug("Submit");
    VK_CHECK(vkWaitForFences(s_Device, 1, &s_GeneratedFence, VK_TRUE, 1e10));
    spdlog::debug("Wait");
    VK_CHECK(vkResetFences(s_Device, 1, &s_GeneratedFence));
    spdlog::debug("Reset");

    s_GeneratedVoxels.copyToVector<Voxel>(chunk->getVoxels());

    serializeChunk(chunk);

    chunk->setIsGenerated(true);
}

void ChunkGenerator::serializeChunk(Chunk* chunk)
{
    size_t maxDepth = std::log2(chunk->getDimensions());

    std::vector<std::vector<SVONode>> queues;

    std::vector<SVONode> finalNodes;

    queues.resize(maxDepth + 1);
    for (size_t i = 0; i < queues.size(); ++i)
    {
        queues[i].reserve(8);
    }

    int depth = maxDepth;
    std::vector<Voxel>& voxels = chunk->getVoxels();
    size_t voxelSize = voxels.size();
    for (size_t i = 0; i < voxelSize; ++i)
    {
        const Voxel& v = voxels.at(i);
        SVONode node;
        node.childPointer = 0;
        node.validMask = 0;
        node.leafMask = 0;

        node.flags = 0;
        node.flags ^= SVONODE_IS_SOLID;
        node.flags ^= SVONODE_IS_AIR * (v.colourIndex < 0);

        node.materialIndex = v.colourIndex;

        queues[depth].push_back(node);
        int d = depth;
        while (d > 0 && queues[d].size() == 8)
        {
            std::unordered_map<uint8_t, int> coloursUsed;

            std::vector<SVONode>& childQueue = queues[d];

            SVONode parent;
            parent.flags = 0;
            parent.childPointer = 0;
            parent.leafMask = 0;
            parent.validMask = 0;
            parent.flags ^= SVONODE_IS_PARENT;

            bool childrenSolid = true;
            for (size_t j = 0; j < 8; ++j)
            {
                const SVONode& child = childQueue[j];
                bool isAir = child.flags & SVONODE_IS_AIR;
                bool isSolid = child.flags & SVONODE_IS_SOLID;

                parent.validMask |= (!isAir << j);

                if (!isAir) // Node is not air
                {
                    if (coloursUsed.find(child.materialIndex) != coloursUsed.end())
                        coloursUsed.at(child.materialIndex) += 1;
                    else
                        coloursUsed[child.materialIndex] = 1;
                }

                childrenSolid &= isSolid;
                parent.leafMask |= (isSolid << j);
            }

            int highestCount = -1;
            uint8_t colour = 0;

            for (auto pair : coloursUsed)
            {
                if (pair.second > highestCount)
                {
                    colour = pair.first;
                    highestCount = pair.second;
                }
            }

            if (childrenSolid && highestCount == 8)
            {
                parent.validMask = 0;
                parent.flags ^= SVONODE_IS_SOLID;
            }
            if (highestCount == -1) parent.flags ^= SVONODE_IS_AIR; // All Children are air
            parent.materialIndex = colour;

            // Not all Children are the same so create children nodes
            if (!(parent.flags & SVONODE_IS_SOLID) && !(parent.flags & SVONODE_IS_AIR))
            {
                for (size_t j = 0; j < 8; ++j)
                {
                    SVONode& child = childQueue[j];

                    if (child.childPointer != 0)
                    {
                        child.childPointer = finalNodes.size() - child.childPointer;
                    }

                    if ((child.flags & SVONODE_IS_AIR) == 0) // Is Not Air
                    {
                        parent.childPointer = finalNodes.size();
                        finalNodes.push_back(child);
                    }
                }
            }

            childQueue.clear();
            queues[d - 1].push_back(parent);
            d--;
        }
    }

    queues[0][0].childPointer = finalNodes.size() - queues[0][0].childPointer;
    finalNodes.push_back(queues[0][0]);
    spdlog::trace("T1: Finished Parsing Nodes");

    std::vector<SVONode> reversed;
    reversed.reserve(finalNodes.size());
    for (auto itr = finalNodes.rbegin(); itr != finalNodes.rend(); itr++)
    {
        reversed.push_back(*itr);
    }
    spdlog::trace("T1: Finished Reversing Nodes");

    size_t bytes = reversed.size() * sizeof(SVONode);
    spdlog::info("T1: Generated {} nodes ({} Voxels) ({} B) ({} KiB) ({} MiB).", reversed.size(),
                 chunk->getVoxels().size(), bytes, bytes / 1024, bytes / (1024 * 1024));

    spdlog::info("T1: ~{} bytes per voxel", (float)bytes / (float)chunk->getVoxels().size());

    createStaging(reversed.size());
    createSVO(chunk->getSVOBuffer(), reversed.size());
    s_StagingBuffer.copyFromData_CPUOnly<SVONode>(reversed);

    copyStagingToChunk(chunk, reversed.size() * sizeof(SVONode));
    // chunk->getSVOBuffer()->copyFromBuffer(s_StagingBuffer, reversed.size() * sizeof(SVONode));
}

void ChunkGenerator::copyStagingToChunk(Chunk* chunk, size_t size)
{
    VK_CHECK(vkResetFences(s_Device, 1, &s_CopyFence));
    VK_CHECK(vkResetCommandBuffer(s_CopyCommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(s_CopyCommandBuffer, &commandBufferBI));

    chunk->getSVOBuffer()->copyFromBuffer(s_CopyCommandBuffer, s_StagingBuffer, size);

    VK_CHECK(vkEndCommandBuffer(s_CopyCommandBuffer));

    VkCommandBufferSubmitInfo commandBufferSI{};
    commandBufferSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandBufferSI.pNext = nullptr;
    commandBufferSI.commandBuffer = s_CopyCommandBuffer;
    commandBufferSI.deviceMask = 0;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.pNext = nullptr;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferSI;

    VK_CHECK(vkQueueSubmit2(s_ComputeQueue, 1, &submitInfo, s_CopyFence));
    VK_CHECK(vkWaitForFences(s_Device, 1, &s_CopyFence, true, 1e10));
}

void ChunkGenerator::createSVO(Buffer* buffer, size_t count)
{
    spdlog::info("T1: Creating Chunk SVO");
    buffer->create(s_Allocator, count * sizeof(SVONode),
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                   VMA_MEMORY_USAGE_GPU_ONLY);
}

void ChunkGenerator::createStaging(size_t count)
{
    size_t size = count * sizeof(SVONode);
    if (s_StagingBuffer.getSize() < size)
    {
        spdlog::info("T1: Resizing Staging Buffer: {}", size);
        s_StagingBuffer.free();

        s_StagingBuffer.create(s_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
}
