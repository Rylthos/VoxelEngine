#include "ChunkGenerator.hpp"

#include <glm/gtx/hash.hpp>
#include <glm/gtx/string_cast.hpp>

#include <format>

#include "Descriptors.hpp"
#include "Profilling.hpp"
#include "SceneManager.hpp"
#include "ShaderModule.hpp"
#include "Timer.hpp"
#include "VkCheck.hpp"

PROF_LOCKABLE_MUTEX(std::mutex, ChunkGenerator::s_GenerateQueueMutex, "Generate Queue");
PROF_LOCKABLE_MUTEX(std::mutex, ChunkGenerator::s_RemoveQueueMutex, "Removal Queue");
PROF_LOCKABLE_MUTEX(std::mutex, ChunkGenerator::s_SerializeQueueMutex, "Serialize Queue");
PROF_LOCKABLE_MUTEX(std::mutex, ChunkGenerator::s_ComputeQueueAccess, "VkAccess ompute Queue");

std::condition_variable_any ChunkGenerator::s_GenerateCondition;
std::condition_variable_any ChunkGenerator::s_SerializeCondition;

std::unordered_set<glm::ivec3> ChunkGenerator::s_ToBeGenerated;
std::unordered_set<glm::ivec3> ChunkGenerator::s_ToBeRemoved;
std::deque<glm::ivec3> ChunkGenerator::s_ToBeSerialized;

Chunks* ChunkGenerator::s_ActiveChunks;

std::array<std::thread, SERIALISATION_THREADS> ChunkGenerator::s_SerialisationThreads;

int ChunkGenerator::s_Seed = 0;
uint32_t ChunkGenerator::s_Depth;
VoxelGenerationPushConstants ChunkGenerator::s_GenerationPushConstants;

VkDescriptorPool ChunkGenerator::s_DescriptorPool;

VkDescriptorSetLayout ChunkGenerator::s_MipmapImageSetLayout;
VkDescriptorSet ChunkGenerator::s_MipmapImageSet;

VkDescriptorSetLayout ChunkGenerator::s_MipmapDataSetLayout;
VkDescriptorSet ChunkGenerator::s_MipmapDataSet;

Image ChunkGenerator::s_GeneratedVoxels;
Buffer ChunkGenerator::s_SerializeBuffer;
std::vector<VkImageView> ChunkGenerator::s_GeneratedImageViews;

Buffer ChunkGenerator::s_StagingBuffer;
VkPipeline ChunkGenerator::s_GenerationPipeline;
VkPipelineLayout ChunkGenerator::s_GenerationPipelineLayout;

VkPipeline ChunkGenerator::s_MipmapPipeline;
VkPipelineLayout ChunkGenerator::s_MipmapPipelineLayout;

VkPipeline ChunkGenerator::s_SerializePipeline;
VkPipelineLayout ChunkGenerator::s_SerializePipelineLayout;

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
                                   VkQueue computeQueue, uint32_t computeQueueFamily,
                                   Chunks* chunks)
{
    s_Allocator = allocator;
    s_Device = device;
    s_ComputeQueue = computeQueue;
    s_ActiveChunks = chunks;
    s_Depth = std::log2(chunkSize) + 1;

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

    std::vector<VkDescriptorPoolSize> poolSizes = {
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  .descriptorCount = s_Depth },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1       }
    };

    VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.pNext = nullptr;
    descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = 2;
    descriptorPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    VK_CHECK(vkCreateDescriptorPool(s_Device, &descriptorPoolCI, nullptr, &s_DescriptorPool));

    {
        auto builder = DescriptorLayoutBuilder::start(s_Device);
        for (uint32_t i = 0; i < s_Depth; i++)
        {
            builder.addStorageImage(i, VK_SHADER_STAGE_COMPUTE_BIT);
        }
        s_MipmapImageSetLayout = builder.build();
    }

    s_MipmapDataSetLayout = DescriptorLayoutBuilder::start(s_Device)
                                .addStorageBuffer(0, VK_SHADER_STAGE_COMPUTE_BIT)
                                .build();

    s_GeneratedVoxels.create(s_Allocator, VK_FORMAT_R16G16B16A16_UINT,
                             { chunkSize, chunkSize, chunkSize }, VK_IMAGE_TYPE_3D,
                             VK_IMAGE_USAGE_STORAGE_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, std::log2(chunkSize) + 1);

    s_SerializeBuffer.create(s_Allocator, sizeof(VoxelMipmapBuffer),
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VMA_MEMORY_USAGE_GPU_TO_CPU);

    for (uint32_t i = 0; i < s_Depth; i++)
    {
        VkImageViewCreateInfo imageViewCI{};
        imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCI.pNext = nullptr;
        imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_3D;
        imageViewCI.image = s_GeneratedVoxels.getImage();
        imageViewCI.format = s_GeneratedVoxels.getFormat();
        imageViewCI.subresourceRange.baseMipLevel = i;
        imageViewCI.subresourceRange.levelCount = 1;
        imageViewCI.subresourceRange.baseArrayLayer = 0;
        imageViewCI.subresourceRange.layerCount = 1;
        imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        VkImageView view;

        VK_CHECK(vkCreateImageView(s_Device, &imageViewCI, nullptr, &view));
        s_GeneratedImageViews.push_back(view);
    }

    { // Pipeline Generation
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VoxelGenerationPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkPipelineLayoutCreateInfo generationLayoutCI{};
        generationLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        generationLayoutCI.pNext = nullptr;
        generationLayoutCI.setLayoutCount = 1;
        generationLayoutCI.pSetLayouts = &s_MipmapImageSetLayout;
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

    { // Mipmapping
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VoxelMipmapPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        std::vector<VkDescriptorSetLayout> layouts = { s_MipmapImageSetLayout,
                                                       s_MipmapDataSetLayout };
        VkPipelineLayoutCreateInfo generationLayoutCI{};
        generationLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        generationLayoutCI.pNext = nullptr;
        generationLayoutCI.setLayoutCount = layouts.size();
        generationLayoutCI.pSetLayouts = layouts.data();
        generationLayoutCI.pushConstantRangeCount = 1;
        generationLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(vkCreatePipelineLayout(s_Device, &generationLayoutCI, nullptr,
                                        &s_MipmapPipelineLayout));

        ShaderModule mipmapShader;
        mipmapShader.create("res/shaders/MipmapOctree.comp.spv", s_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = mipmapShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = s_MipmapPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(s_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &s_MipmapPipeline));
    }

    { // Serializing
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VoxelSerializePushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        std::vector<VkDescriptorSetLayout> layouts = { s_MipmapImageSetLayout,
                                                       s_MipmapDataSetLayout };
        VkPipelineLayoutCreateInfo generationLayoutCI{};
        generationLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        generationLayoutCI.pNext = nullptr;
        generationLayoutCI.setLayoutCount = layouts.size();
        generationLayoutCI.pSetLayouts = layouts.data();
        generationLayoutCI.pushConstantRangeCount = 1;
        generationLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(vkCreatePipelineLayout(s_Device, &generationLayoutCI, nullptr,
                                        &s_SerializePipelineLayout));

        ShaderModule serializeShader;
        serializeShader.create("res/shaders/SerializeOctree.comp.spv", s_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = serializeShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = s_SerializePipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(s_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &s_SerializePipeline));
    }

    {
        auto builder =
            DescriptorSetBuilder::start(s_Device, s_DescriptorPool, s_MipmapImageSetLayout);
        for (size_t i = 0; i < s_GeneratedImageViews.size(); i++)
        {
            builder.addStorageImage(i, VK_IMAGE_LAYOUT_GENERAL, s_GeneratedImageViews.at(i));
        }
        s_MipmapImageSet = builder.build().at(0);
    }

    s_MipmapDataSet =
        DescriptorSetBuilder::start(s_Device, s_DescriptorPool, s_MipmapDataSetLayout)
            .addStorageBuffer(0, s_SerializeBuffer.getBuffer(), 0, sizeof(VoxelMipmapBuffer))
            .build()
            .at(0);

    s_GenerationPushConstants.seed = s_Seed;
    s_GenerationPushConstants.cutoff = 0.0;
    s_GenerationPushConstants.p10 = 245;
    s_GenerationPushConstants.p50 = 205;
    s_GenerationPushConstants.p100 = 155;

    transitionImages();
}

void ChunkGenerator::freeResources()
{
    s_Running = false;

    s_SerializeCondition.notify_all();
    // for (auto& thread : s_SerialisationThreads)
    //     thread.join();

    s_StagingBuffer.free();
    for (size_t i = 0; i < s_GeneratedImageViews.size(); i++)
    {
        vkDestroyImageView(s_Device, s_GeneratedImageViews.at(i), nullptr);
    }

    s_GeneratedVoxels.free();
    s_SerializeBuffer.free();

    vkDestroyDescriptorSetLayout(s_Device, s_MipmapImageSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(s_Device, s_MipmapDataSetLayout, nullptr);
    vkDestroyDescriptorPool(s_Device, s_DescriptorPool, nullptr);

    vkDestroyPipeline(s_Device, s_SerializePipeline, nullptr);
    vkDestroyPipelineLayout(s_Device, s_SerializePipelineLayout, nullptr);
    vkDestroyPipeline(s_Device, s_MipmapPipeline, nullptr);
    vkDestroyPipelineLayout(s_Device, s_MipmapPipelineLayout, nullptr);
    vkDestroyPipeline(s_Device, s_GenerationPipeline, nullptr);
    vkDestroyPipelineLayout(s_Device, s_GenerationPipelineLayout, nullptr);

    vkDestroyFence(s_Device, s_GeneratedFence, nullptr);
    vkDestroyFence(s_Device, s_CopyFence, nullptr);
    vkDestroyCommandPool(s_Device, s_CommandPool, nullptr);
}

void ChunkGenerator::addChunkToQueue(glm::ivec3 chunkPosition)
{
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(s_GenerateQueueMutex);
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(s_RemoveQueueMutex);

    s_ToBeGenerated.insert(chunkPosition);
    s_GenerateCondition.notify_one();

    s_ToBeRemoved.erase(chunkPosition);
}

void ChunkGenerator::removeChunk(glm::ivec3 pos)
{
    {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(s_SerializeQueueMutex);
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(s_GenerateQueueMutex);
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk1(s_RemoveQueueMutex);
        s_ToBeRemoved.emplace(pos);
        {
            std::unordered_set<glm::ivec3> copy = s_ToBeRemoved;
            for (glm::ivec3 pos : copy)
            {
                s_ToBeGenerated.erase(pos);
                s_ToBeRemoved.erase(pos);
            }
        }

        {
            std::deque<glm::ivec3> newList;
            for (auto itr = s_ToBeSerialized.rbegin(); itr != s_ToBeSerialized.rend(); itr++)
            {
                if (s_ToBeRemoved.contains(*itr))
                {
                    s_ToBeRemoved.erase(pos);
                }
                else
                {
                    newList.emplace_back(*itr);
                }
            }

            s_ToBeSerialized = newList;
        }
    }
}

void ChunkGenerator::generateChunkLoop()
{
    assert(!s_Running && "Already started loop");

    s_Running = true;

    PROF_THREAD_NAME("Chunk Generation");

    // size_t id = 0;
    // for (auto& thread : s_SerialisationThreads)
    // {
    //     thread = std::thread([&id]() { serializeChunk(id++); });
    // }

    spdlog::info("Started Chunk Generation");

    while (s_Running)
    {
        generateNextChunk();
    }
}

void ChunkGenerator::generateNextChunk()
{
    PROF_ZONE_SCOPED;
    {
        std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk(s_GenerateQueueMutex);
        s_GenerateCondition.wait(lk, [] { return !s_ToBeGenerated.empty() || !s_Running; });
    }

    if (!s_Running) return;

    glm::ivec3 chunkPosition;
    {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(s_GenerateQueueMutex);
        auto itr = s_ToBeGenerated.begin();
        chunkPosition = *itr;

        s_ToBeGenerated.erase(itr);
    }

    Timer::startTimer("Chunk Generation");

    generateChunk(chunkPosition);

    Timer::stopTimer("Chunk Generation");
}

void ChunkGenerator::generateChunk(glm::ivec3 chunkPosition)
{
    PROF_ZONE_SCOPED;
    spdlog::info("Generating chunk: {}", glm::to_string(chunkPosition));
    {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(s_ActiveChunks->mutex);
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(s_ComputeQueueAccess);

        if (s_ToBeRemoved.contains(chunkPosition))
        {
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(s_RemoveQueueMutex);
            s_ToBeRemoved.erase(chunkPosition);
            return;
        }

        Timer::startTimer("Chunk Compute");
        VK_CHECK(vkResetCommandBuffer(s_CommandBuffer, 0));

        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBI.pNext = nullptr;
        commandBufferBI.pInheritanceInfo = nullptr;
        commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        size_t dimension = s_ActiveChunks->chunks.at(chunkPosition).getDimensions();

        std::vector<VoxelMipmapBuffer> data;
        data.push_back({ .counter = 1 });

        createStaging(1, sizeof(VoxelMipmapBuffer));
        s_StagingBuffer.copyFromData_CPUOnly<VoxelMipmapBuffer>(data);
        copyStagingToBuffer(&s_SerializeBuffer);

        VK_CHECK(vkBeginCommandBuffer(s_CommandBuffer, &commandBufferBI));
        {
            PROF_VK_ZONE(s_CommandBuffer, "Chunk Compute Generation");

            s_GenerationPushConstants.dimension = dimension;
            s_GenerationPushConstants.size = Voxel::VOXEL_SIZE;
            s_GenerationPushConstants.origin = glm::vec4(chunkPosition, 0.);

            vkCmdBindPipeline(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                              s_GenerationPipeline);

            vkCmdBindDescriptorSets(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    s_GenerationPipelineLayout, 0, 1, &s_MipmapImageSet, 0,
                                    nullptr);

            vkCmdPushConstants(s_CommandBuffer, s_GenerationPipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelGenerationPushConstants),
                               &s_GenerationPushConstants);

            vkCmdDispatch(s_CommandBuffer, dimension / 4, dimension / 4, dimension / 4);

            vkCmdPipelineBarrier(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 VK_PIPELINE_BIND_POINT_COMPUTE, 0, 0, nullptr, 0, nullptr, 0,
                                 nullptr);

            vkCmdBindPipeline(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s_MipmapPipeline);

            vkCmdBindDescriptorSets(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    s_MipmapPipelineLayout, 1, 1, &s_MipmapDataSet, 0, nullptr);

            vkCmdBindDescriptorSets(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    s_MipmapPipelineLayout, 0, 1, &s_MipmapImageSet, 0, nullptr);

            for (uint32_t i = 0; i < s_GeneratedVoxels.getMiplevels() - 1; i++)
            {
                if (i != 0)
                {
                    vkCmdPipelineBarrier(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                         VK_PIPELINE_BIND_POINT_COMPUTE, 0, 0, nullptr, 0, nullptr,
                                         0, nullptr);
                }

                VoxelMipmapPushConstants pushConstant;
                pushConstant.sourceLevel = i;

                vkCmdPushConstants(s_CommandBuffer, s_MipmapPipelineLayout,
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelMipmapPushConstants),
                                   &pushConstant);

                uint32_t size = std::ceil((dimension >> (i + 1)) / 2.);
                vkCmdDispatch(s_CommandBuffer, size, size, size);
            }
        }
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
        VK_CHECK(vkWaitForFences(s_Device, 1, &s_GeneratedFence, VK_TRUE, 1e10));
        VK_CHECK(vkResetFences(s_Device, 1, &s_GeneratedFence));
        VK_CHECK(vkResetCommandBuffer(s_CommandBuffer, 0));
    }
    Timer::stopTimer("Chunk Compute");

    Timer::startTimer("Chunk Copy");
    std::vector<VoxelMipmapBuffer> returnData;
    s_SerializeBuffer.copyToVector(returnData);

    {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(s_ActiveChunks->mutex);
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(s_ComputeQueueAccess);

        int32_t numNodes = returnData.at(0).counter;
        spdlog::info("Generated {} Nodes in mipmap", numNodes);

        createSVO(s_ActiveChunks->chunks.at(chunkPosition).getSVOBuffer(), numNodes);

        std::vector<VoxelMipmapBuffer> data;
        data.push_back({ .counter = 1 });

        createStaging(1, sizeof(VoxelMipmapBuffer));
        s_StagingBuffer.copyFromData_CPUOnly<VoxelMipmapBuffer>(data);
        copyStagingToBuffer(&s_SerializeBuffer);

        VK_CHECK(vkResetCommandBuffer(s_CommandBuffer, 0));

        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBI.pNext = nullptr;
        commandBufferBI.pInheritanceInfo = nullptr;
        commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        size_t dimension = s_ActiveChunks->chunks.at(chunkPosition).getDimensions();

        VK_CHECK(vkBeginCommandBuffer(s_CommandBuffer, &commandBufferBI));
        {
            PROF_VK_ZONE(s_CommandBuffer, "Chunk Compute Serialization");

            vkCmdBindPipeline(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, s_SerializePipeline);

            vkCmdBindDescriptorSets(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    s_SerializePipelineLayout, 0, 1, &s_MipmapImageSet, 0, nullptr);

            vkCmdBindDescriptorSets(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    s_SerializePipelineLayout, 1, 1, &s_MipmapDataSet, 0, nullptr);

            for (uint32_t i = s_GeneratedVoxels.getMiplevels() - 1; i > 0; i--)
            {
                if (i < s_GeneratedVoxels.getMiplevels() - 1)
                {
                    vkCmdPipelineBarrier(s_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                         VK_PIPELINE_BIND_POINT_COMPUTE, 0, 0, nullptr, 0, nullptr,
                                         0, nullptr);
                }

                VoxelSerializePushConstants pushConstant;
                pushConstant.numNodes = numNodes;
                pushConstant.currentLevel = i;
                pushConstant.targetBuffer =
                    s_ActiveChunks->chunks.at(chunkPosition).getBufferAddress(s_Device);

                vkCmdPushConstants(s_CommandBuffer, s_SerializePipelineLayout,
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                   sizeof(VoxelSerializePushConstants), &pushConstant);

                uint32_t size = std::ceil((dimension >> i) / 2.);
                vkCmdDispatch(s_CommandBuffer, size, size, size);
            }
        }
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
        VK_CHECK(vkWaitForFences(s_Device, 1, &s_GeneratedFence, VK_TRUE, 1e10));
        VK_CHECK(vkResetFences(s_Device, 1, &s_GeneratedFence));
        VK_CHECK(vkResetCommandBuffer(s_CommandBuffer, 0));

        s_ActiveChunks->chunks.at(chunkPosition).setIsGenerated(true);
    }

    // {
    //     std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(s_ActiveChunks->mutex);
    //     if (s_ToBeRemoved.contains(chunkPosition))
    //     {
    //         std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(s_RemoveQueueMutex);
    //         s_ToBeRemoved.erase(chunkPosition);
    //         Timer::stopTimer("Chunk Copy");
    //         return;
    //     }
    //
    //     //
    //     s_GeneratedVoxels.copyToVector<Voxel>(s_ActiveChunks->chunks.at(chunkPosition).getVoxels());
    // }

    Timer::stopTimer("Chunk Copy");

    {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(s_SerializeQueueMutex);
        s_ToBeSerialized.push_back(chunkPosition);
    }
    s_SerializeCondition.notify_all();
}

void ChunkGenerator::serializeChunk(uint32_t id)
{
    const std::string timerString = std::format("Serialize Chunk: {}", id);
    const std::string threadName = std::format("SerializeThread{}", id);
    PROF_THREAD_NAME(threadName.c_str());

    while (s_Running)
    {
        PROF_ZONE_SCOPED;
        glm::ivec3 chunkPosition;
        {
            std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk(s_SerializeQueueMutex);
            s_SerializeCondition.wait(lk, [] { return !s_ToBeSerialized.empty() || !s_Running; });

            if (!s_Running) return;

            chunkPosition = s_ToBeSerialized.front();
            s_ToBeSerialized.pop_front();

            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(s_ActiveChunks->mutex);
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(s_RemoveQueueMutex);
            if (s_ToBeRemoved.contains(chunkPosition))
            {
                s_ToBeRemoved.erase(chunkPosition);
                s_SerializeCondition.notify_one();
                continue;
            }

            if (!s_ActiveChunks->chunks.contains(chunkPosition))
            {
                s_SerializeCondition.notify_one();
                continue;
            }
        }
        spdlog::info("Serializing: {}: {}", id, glm::to_string(chunkPosition));
        Timer::startTimer(timerString);

        std::vector<std::vector<SVONode>> queues;

        std::vector<SVONode> finalNodes;

        queues.resize(s_Depth);
        for (size_t i = 0; i < queues.size(); ++i)
        {
            queues[i].reserve(8);
        }

        int depth = s_Depth;

        std::vector<Voxel> voxels;
        {
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(s_ActiveChunks->mutex);
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(s_RemoveQueueMutex);

            if (s_ToBeRemoved.contains(chunkPosition))
            {
                s_ToBeRemoved.erase(chunkPosition);
                s_SerializeCondition.notify_one();
                Timer::stopTimer(timerString);
                continue;
            }

            if (s_ActiveChunks->chunks.contains(chunkPosition))
            {
                voxels = s_ActiveChunks->chunks.at(chunkPosition).getVoxels();
            }
            else
            {
                s_GenerateCondition.notify_one();
                continue;
            }
        }

        PROF_ZONE_NAMED_N(PROF_ZONE_1, "Serializing", true);
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
                // PROF_ZONE_NAMED_N(PROF_ZONE_2, "Depth Push", true);
                std::unordered_map<uint8_t, int> coloursUsed;

                std::vector<SVONode>& childQueue = queues[d];

                SVONode parent;
                parent.flags = 0;
                parent.childPointer = 0;
                parent.leafMask = 0;
                parent.validMask = 0;
                parent.flags ^= SVONODE_IS_PARENT;

                bool childrenSolid = true;
                {
                    // PROF_ZONE_NAMED_N(PROF_ZONE_3, "Checking Children", true);
                    for (size_t j = 0; j < 8; ++j)
                    {
                        const SVONode& child = childQueue[j];
                        bool isAir = child.flags & SVONODE_IS_AIR;
                        bool isSolid = child.flags & SVONODE_IS_SOLID;

                        parent.validMask |= (!isAir << j);

                        if (!isAir) // Node is not air
                        {
                            if (coloursUsed.contains(child.materialIndex))
                                coloursUsed.at(child.materialIndex) += 1;
                            else
                                coloursUsed[child.materialIndex] = 1;
                        }

                        childrenSolid &= isSolid;
                        parent.leafMask |= (isSolid << j);
                    }
                }

                int highestCount = -1;
                uint8_t colour = 0;

                {
                    // PROF_ZONE_NAMED_N(PROF_ZONE_4, "Colour Check", true);
                    for (auto pair : coloursUsed)
                    {
                        if (pair.second > highestCount)
                        {
                            colour = pair.first;
                            highestCount = pair.second;
                        }
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

        std::vector<SVONode> reversed;
        reversed.reserve(finalNodes.size());
        {
            // PROF_ZONE_NAMED_N(PROF_ZONE_5, "Reversing", true);
            for (auto itr = finalNodes.rbegin(); itr != finalNodes.rend(); itr++)
            {
                reversed.push_back(*itr);
            }
        }

        {
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(s_ActiveChunks->mutex);

            if (s_ToBeRemoved.contains(chunkPosition) ||
                !s_ActiveChunks->chunks.contains(chunkPosition))
            {
                std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(s_RemoveQueueMutex);
                s_ToBeRemoved.erase(chunkPosition);
                Timer::stopTimer(timerString);
                s_SerializeCondition.notify_one();
                continue;
            }

            size_t size = s_ActiveChunks->chunks.at(chunkPosition).getVoxels().size();

            size_t bytes = reversed.size() * sizeof(SVONode);
            spdlog::info("{} Generated {} nodes ({} Voxels) ({} B) ({} KiB) ({} MiB).",
                         glm::to_string(chunkPosition), reversed.size(), size, bytes, bytes / 1024,
                         bytes / (1024 * 1024));

            spdlog::info("~{} bytes per voxel", (float)bytes / (float)size);

            createStaging(reversed.size());
            createSVO(s_ActiveChunks->chunks.at(chunkPosition).getSVOBuffer(), reversed.size());
            s_StagingBuffer.copyFromData_CPUOnly<SVONode>(reversed);

            copyStagingToChunk(chunkPosition, reversed.size() * sizeof(SVONode));

            s_ActiveChunks->chunks.at(chunkPosition).setIsGenerated(true);
        }

        Timer::stopTimer(timerString);
    }
}

void ChunkGenerator::transitionImages()
{
    PROF_ZONE_SCOPED;
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(s_ComputeQueueAccess);

    VK_CHECK(vkResetFences(s_Device, 1, &s_CopyFence));
    VK_CHECK(vkResetCommandBuffer(s_CopyCommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(s_CopyCommandBuffer, &commandBufferBI));

    s_GeneratedVoxels.transition(s_CopyCommandBuffer, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_GENERAL);

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

void ChunkGenerator::copyStagingToBuffer(Buffer* buffer)
{
    VK_CHECK(vkResetFences(s_Device, 1, &s_CopyFence));
    VK_CHECK(vkResetCommandBuffer(s_CopyCommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(s_CopyCommandBuffer, &commandBufferBI));

    buffer->copyFromBuffer(s_CopyCommandBuffer, s_StagingBuffer, buffer->getSize());

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

void ChunkGenerator::copyStagingToChunk(glm::ivec3 chunkPosition, size_t size)
{
    PROF_ZONE_SCOPED;
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(s_ComputeQueueAccess);

    copyStagingToBuffer(s_ActiveChunks->chunks.at(chunkPosition).getSVOBuffer());
}

void ChunkGenerator::createSVO(Buffer* buffer, size_t count)
{
    buffer->create(s_Allocator, count * sizeof(SVONode),
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                   VMA_MEMORY_USAGE_GPU_ONLY);
}

void ChunkGenerator::createStaging(size_t count, size_t elem_size)
{
    size_t size = count * elem_size;
    if (s_StagingBuffer.getSize() < size)
    {
        s_StagingBuffer.free();

        s_StagingBuffer.create(s_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
}
