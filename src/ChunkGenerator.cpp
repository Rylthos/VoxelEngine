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

static ChunkGenerator* m_Instance = nullptr;

ChunkGenerator& ChunkGenerator::getInstance()
{
    assert(m_Instance);
    return *m_Instance;
}

void ChunkGenerator::init(uint32_t chunkSize, VmaAllocator allocator, VkDevice device,
                          VkQueue computeQueue, uint32_t computeQueueFamily, Chunks* chunks)
{
    assert(!m_Instance);

    m_Instance = new ChunkGenerator();
    getInstance().initResources(chunkSize, allocator, device, computeQueue, computeQueueFamily,
                                chunks);
}

void ChunkGenerator::free()
{
    assert(m_Instance);

    getInstance().freeResources();
    delete m_Instance;
}

void ChunkGenerator::addChunkToQueue(glm::ivec3 chunkPosition)
{
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(m_GenerateQueueMutex);
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(m_RemoveQueueMutex);

    m_ToBeGenerated.insert(chunkPosition);
    m_GenerateCondition.notify_one();

    m_ToBeRemoved.erase(chunkPosition);
}

void ChunkGenerator::removeChunk(glm::ivec3 pos)
{
    {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(m_SerializeQueueMutex);
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(m_GenerateQueueMutex);
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk1(m_RemoveQueueMutex);
        m_ToBeRemoved.emplace(pos);
        {
            std::unordered_set<glm::ivec3> copy = m_ToBeRemoved;
            for (glm::ivec3 pos : copy)
            {
                m_ToBeGenerated.erase(pos);
                m_ToBeRemoved.erase(pos);
            }
        }

        {
            std::deque<glm::ivec3> newList;
            for (auto itr = m_ToBeSerialized.rbegin(); itr != m_ToBeSerialized.rend(); itr++)
            {
                if (m_ToBeRemoved.contains(*itr))
                {
                    m_ToBeRemoved.erase(pos);
                }
                else
                {
                    newList.emplace_back(*itr);
                }
            }

            m_ToBeSerialized = newList;
        }
    }
}

void ChunkGenerator::generateChunkLoop()
{
    assert(!m_Running && "Already started loop");

    m_Running = true;

    PROF_THREAD_NAME("Chunk Generation");

    // size_t id = 0;
    // for (auto& thread : m_SerialisationThreads)
    // {
    //     thread = std::thread([&id]() { serializeChunk(id++); });
    // }

    spdlog::info("Started Chunk Generation");

    while (m_Running)
    {
        generateNextChunk();
    }
}

void ChunkGenerator::initResources(uint32_t chunkSize, VmaAllocator allocator, VkDevice device,
                                   VkQueue computeQueue, uint32_t computeQueueFamily,
                                   Chunks* chunks)
{
    m_Allocator = allocator;
    m_Device = device;
    m_ComputeQueue = computeQueue;
    m_ActiveChunks = chunks;
    m_Depth = std::log2(chunkSize) + 1;

    VkCommandPoolCreateInfo commandPoolCI{};
    commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCI.pNext = nullptr;
    commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCI.queueFamilyIndex = computeQueueFamily;

    VK_CHECK(vkCreateCommandPool(m_Device, &commandPoolCI, nullptr, &m_CommandPool));

    VkCommandBufferAllocateInfo commandBufferAI{};
    commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAI.pNext = nullptr;
    commandBufferAI.commandPool = m_CommandPool;
    commandBufferAI.commandBufferCount = 1;
    commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(m_Device, &commandBufferAI, &m_CommandBuffer));
    VK_CHECK(vkAllocateCommandBuffers(m_Device, &commandBufferAI, &m_CopyCommandBuffer));

    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.pNext = nullptr;
    fenceCI.flags = 0;

    VK_CHECK(vkCreateFence(m_Device, &fenceCI, nullptr, &m_GeneratedFence));
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(m_Device, &fenceCI, nullptr, &m_CopyFence));

    std::vector<VkDescriptorPoolSize> poolSizes = {
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  .descriptorCount = m_Depth },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1       }
    };

    VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.pNext = nullptr;
    descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = 2;
    descriptorPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    VK_CHECK(vkCreateDescriptorPool(m_Device, &descriptorPoolCI, nullptr, &m_DescriptorPool));

    m_MipmapImageSetLayout = DescriptorLayoutBuilder::start(m_Device)
                                 .addStorageImageArray(0, m_Depth, VK_SHADER_STAGE_COMPUTE_BIT)
                                 // .addStorageImage(0, VK_SHADER_STAGE_COMPUTE_BIT)
                                 .build();

    m_MipmapDataSetLayout = DescriptorLayoutBuilder::start(m_Device)
                                .addStorageBuffer(0, VK_SHADER_STAGE_COMPUTE_BIT)
                                .build();

    m_GeneratedVoxels.create(m_Allocator, VK_FORMAT_R16G16B16A16_UINT,
                             { chunkSize, chunkSize, chunkSize }, VK_IMAGE_TYPE_3D,
                             VK_IMAGE_USAGE_STORAGE_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, std::log2(chunkSize) + 1);

    m_SerializeBuffer.create(m_Allocator, sizeof(VoxelMipmapBuffer),
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VMA_MEMORY_USAGE_GPU_TO_CPU);

    for (uint32_t i = 0; i < m_Depth; i++)
    {
        VkImageViewCreateInfo imageViewCI{};
        imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCI.pNext = nullptr;
        imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_3D;
        imageViewCI.image = m_GeneratedVoxels.getImage();
        imageViewCI.format = m_GeneratedVoxels.getFormat();
        imageViewCI.subresourceRange.baseMipLevel = i;
        imageViewCI.subresourceRange.levelCount = 1;
        imageViewCI.subresourceRange.baseArrayLayer = 0;
        imageViewCI.subresourceRange.layerCount = 1;
        imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        VkImageView view;

        VK_CHECK(vkCreateImageView(m_Device, &imageViewCI, nullptr, &view));
        m_GeneratedImageViews.push_back(view);
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
        generationLayoutCI.pSetLayouts = &m_MipmapImageSetLayout;
        generationLayoutCI.pushConstantRangeCount = 1;
        generationLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(vkCreatePipelineLayout(m_Device, &generationLayoutCI, nullptr,
                                        &m_GenerationPipelineLayout));

        ShaderModule voxelShader;
        voxelShader.create("res/shaders/Generation.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = voxelShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_GenerationPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &m_GenerationPipeline));
    }

    { // Mipmapping
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VoxelMipmapPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        std::vector<VkDescriptorSetLayout> layouts = { m_MipmapImageSetLayout,
                                                       m_MipmapDataSetLayout };
        VkPipelineLayoutCreateInfo generationLayoutCI{};
        generationLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        generationLayoutCI.pNext = nullptr;
        generationLayoutCI.setLayoutCount = layouts.size();
        generationLayoutCI.pSetLayouts = layouts.data();
        generationLayoutCI.pushConstantRangeCount = 1;
        generationLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(vkCreatePipelineLayout(m_Device, &generationLayoutCI, nullptr,
                                        &m_MipmapPipelineLayout));

        ShaderModule mipmapShader;
        mipmapShader.create("res/shaders/MipmapOctree.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = mipmapShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_MipmapPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &m_MipmapPipeline));
    }

    { // Serializing
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VoxelSerializePushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        std::vector<VkDescriptorSetLayout> layouts = { m_MipmapImageSetLayout,
                                                       m_MipmapDataSetLayout };
        VkPipelineLayoutCreateInfo generationLayoutCI{};
        generationLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        generationLayoutCI.pNext = nullptr;
        generationLayoutCI.setLayoutCount = layouts.size();
        generationLayoutCI.pSetLayouts = layouts.data();
        generationLayoutCI.pushConstantRangeCount = 1;
        generationLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(vkCreatePipelineLayout(m_Device, &generationLayoutCI, nullptr,
                                        &m_SerializePipelineLayout));

        ShaderModule serializeShader;
        serializeShader.create("res/shaders/SerializeOctree.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = serializeShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_SerializePipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &m_SerializePipeline));
    }

    m_MipmapImageSet =
        DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_MipmapImageSetLayout)

            .addStorageImageArray(0, VK_IMAGE_LAYOUT_GENERAL, m_GeneratedImageViews)
            .build()
            .at(0);

    m_MipmapDataSet =
        DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_MipmapDataSetLayout)
            .addStorageBuffer(0, m_SerializeBuffer.getBuffer(), 0, sizeof(VoxelMipmapBuffer))
            .build()
            .at(0);

    m_GenerationPushConstants.seed = m_Seed;
    m_GenerationPushConstants.cutoff = 0.0;
    m_GenerationPushConstants.p10 = 245;
    m_GenerationPushConstants.p50 = 205;
    m_GenerationPushConstants.p100 = 155;

    transitionImages();
}

void ChunkGenerator::freeResources()
{
    m_Running = false;

    m_SerializeCondition.notify_all();
    // for (auto& thread : m_SerialisationThreads)
    //     thread.join();

    m_StagingBuffer.free();
    for (size_t i = 0; i < m_GeneratedImageViews.size(); i++)
    {
        vkDestroyImageView(m_Device, m_GeneratedImageViews.at(i), nullptr);
    }

    m_GeneratedVoxels.free();
    m_SerializeBuffer.free();

    vkDestroyDescriptorSetLayout(m_Device, m_MipmapImageSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, m_MipmapDataSetLayout, nullptr);
    vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);

    vkDestroyPipeline(m_Device, m_SerializePipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_SerializePipelineLayout, nullptr);
    vkDestroyPipeline(m_Device, m_MipmapPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_MipmapPipelineLayout, nullptr);
    vkDestroyPipeline(m_Device, m_GenerationPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_GenerationPipelineLayout, nullptr);

    vkDestroyFence(m_Device, m_GeneratedFence, nullptr);
    vkDestroyFence(m_Device, m_CopyFence, nullptr);
    vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
}

void ChunkGenerator::generateNextChunk()
{
    PROF_ZONE_SCOPED;
    {
        std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk(m_GenerateQueueMutex);
        m_GenerateCondition.wait(lk, [this] { return !m_ToBeGenerated.empty() || !m_Running; });
    }

    if (!m_Running) return;

    glm::ivec3 chunkPosition;
    {
        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(m_GenerateQueueMutex);
        auto itr = m_ToBeGenerated.begin();
        chunkPosition = *itr;

        m_ToBeGenerated.erase(itr);
    }

    Timer::startTimer("Chunk Generation");

    generateChunk(chunkPosition);

    Timer::stopTimer("Chunk Generation");
}

void ChunkGenerator::generateChunk(glm::ivec3 chunkPosition)
{
    PROF_ZONE_SCOPED;
    spdlog::info("Generating chunk: {}", glm::to_string(chunkPosition));

    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(m_ActiveChunks->mutex);
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(m_ComputeQueueAccess);
    {
        if (m_ToBeRemoved.contains(chunkPosition))
        {
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(m_RemoveQueueMutex);
            m_ToBeRemoved.erase(chunkPosition);
            return;
        }

        Timer::startTimer("Chunk Compute");
        VK_CHECK(vkResetCommandBuffer(m_CommandBuffer, 0));

        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBI.pNext = nullptr;
        commandBufferBI.pInheritanceInfo = nullptr;
        commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        size_t dimension = m_ActiveChunks->chunks.at(chunkPosition).getDimensions();

        std::vector<VoxelMipmapBuffer> data;
        data.push_back({ .counter = 1 });

        createStaging(1, sizeof(VoxelMipmapBuffer));
        m_StagingBuffer.copyFromData_CPUOnly<VoxelMipmapBuffer>(data);
        copyStagingToBuffer(&m_SerializeBuffer);

        VK_CHECK(vkBeginCommandBuffer(m_CommandBuffer, &commandBufferBI));
        {
            PROF_VK_ZONE(m_CommandBuffer, "Chunk Compute Generation");

            m_GenerationPushConstants.dimension = dimension;
            m_GenerationPushConstants.size = Voxel::VOXEL_SIZE;
            m_GenerationPushConstants.origin = glm::vec4(chunkPosition, 0.);

            vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                              m_GenerationPipeline);

            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_GenerationPipelineLayout, 0, 1, &m_MipmapImageSet, 0,
                                    nullptr);

            vkCmdPushConstants(m_CommandBuffer, m_GenerationPipelineLayout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelGenerationPushConstants),
                               &m_GenerationPushConstants);

            size_t dispatchSize = std::ceil(dimension / 4.);
            vkCmdDispatch(m_CommandBuffer, dispatchSize, dispatchSize, dispatchSize);

            VkMemoryBarrier memBarrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                           .pNext = nullptr,
                                           .srcAccessMask = 0,
                                           .dstAccessMask = 0 };

            vkCmdPipelineBarrier(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 VK_PIPELINE_BIND_POINT_COMPUTE, 0, 1, &memBarrier, 0, nullptr, 0,
                                 nullptr);

            vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_MipmapPipeline);

            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_MipmapPipelineLayout, 1, 1, &m_MipmapDataSet, 0, nullptr);

            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_MipmapPipelineLayout, 0, 1, &m_MipmapImageSet, 0, nullptr);

            for (uint32_t i = 0; i < m_GeneratedVoxels.getMiplevels() - 1; i++)
            {
                if (i != 0)
                {
                    vkCmdPipelineBarrier(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                         VK_PIPELINE_BIND_POINT_COMPUTE, 0, 1, &memBarrier, 0,
                                         nullptr, 0, nullptr);
                }

                VoxelMipmapPushConstants pushConstant;
                pushConstant.sourceLevel = i;

                vkCmdPushConstants(m_CommandBuffer, m_MipmapPipelineLayout,
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelMipmapPushConstants),
                                   &pushConstant);

                uint32_t size = std::ceil((dimension >> (i + 1)) / 2.);
                vkCmdDispatch(m_CommandBuffer, size, size, size);
            }
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

        VK_CHECK(vkQueueSubmit2(m_ComputeQueue, 1, &submitInfo, m_GeneratedFence));
        VK_CHECK(vkWaitForFences(m_Device, 1, &m_GeneratedFence, VK_TRUE, 1e10));
        VK_CHECK(vkResetFences(m_Device, 1, &m_GeneratedFence));
    }
    Timer::stopTimer("Chunk Compute");

    // Timer::startTimer("Chunk Copy");
    std::vector<VoxelMipmapBuffer> returnData;
    m_SerializeBuffer.copyToVector(returnData);

    Timer::startTimer("Chunk Serialize");
    {
        int32_t numNodes = returnData.at(0).counter;
        spdlog::info("Generated {} Nodes in mipmap", numNodes);

        createSVO(m_ActiveChunks->chunks.at(chunkPosition).getSVOBuffer(), numNodes);

        std::vector<VoxelMipmapBuffer> data;
        data.push_back({ .counter = 1 });

        createStaging(1, sizeof(VoxelMipmapBuffer));
        m_StagingBuffer.copyFromData_CPUOnly<VoxelMipmapBuffer>(data);
        copyStagingToBuffer(&m_SerializeBuffer);

        VK_CHECK(vkResetCommandBuffer(m_CommandBuffer, 0));

        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBI.pNext = nullptr;
        commandBufferBI.pInheritanceInfo = nullptr;
        commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        size_t dimension = m_ActiveChunks->chunks.at(chunkPosition).getDimensions();

        VK_CHECK(vkBeginCommandBuffer(m_CommandBuffer, &commandBufferBI));
        {
            PROF_VK_ZONE(m_CommandBuffer, "Chunk Compute Serialization");

            vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_SerializePipeline);

            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_SerializePipelineLayout, 0, 1, &m_MipmapImageSet, 0, nullptr);

            vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    m_SerializePipelineLayout, 1, 1, &m_MipmapDataSet, 0, nullptr);

            VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                        .pNext = nullptr,
                                        .srcAccessMask = 0,
                                        .dstAccessMask = 0 };

            for (uint32_t i = m_GeneratedVoxels.getMiplevels() - 1; i > 0; i--)
            {
                if (i < m_GeneratedVoxels.getMiplevels() - 1)
                {
                    vkCmdPipelineBarrier(m_CommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0,
                                         nullptr, 0, nullptr);
                }

                VoxelSerializePushConstants pushConstant;
                pushConstant.numNodes = numNodes;
                pushConstant.currentLevel = i;
                pushConstant.targetBuffer =
                    m_ActiveChunks->chunks.at(chunkPosition).getBufferAddress(m_Device);

                vkCmdPushConstants(m_CommandBuffer, m_SerializePipelineLayout,
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                   sizeof(VoxelSerializePushConstants), &pushConstant);

                uint32_t size = std::ceil((dimension >> i) / 2.);
                vkCmdDispatch(m_CommandBuffer, size, size, size);
            }
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

        VK_CHECK(vkQueueSubmit2(m_ComputeQueue, 1, &submitInfo, m_GeneratedFence));
        VK_CHECK(vkWaitForFences(m_Device, 1, &m_GeneratedFence, VK_TRUE, 1e10));
        VK_CHECK(vkResetFences(m_Device, 1, &m_GeneratedFence));

        m_ActiveChunks->chunks.at(chunkPosition).setIsGenerated(true);
    }
    Timer::stopTimer("Chunk Serialize");
}

void ChunkGenerator::serializeChunk(uint32_t id)
{
    return;

    const std::string timerString = std::format("Serialize Chunk: {}", id);
    const std::string threadName = std::format("SerializeThread{}", id);
    PROF_THREAD_NAME(threadName.c_str());

    while (m_Running)
    {
        PROF_ZONE_SCOPED;
        glm::ivec3 chunkPosition;
        {
            std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk(m_SerializeQueueMutex);
            m_SerializeCondition.wait(lk,
                                      [this] { return !m_ToBeSerialized.empty() || !m_Running; });

            if (!m_Running) return;

            chunkPosition = m_ToBeSerialized.front();
            m_ToBeSerialized.pop_front();

            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(m_ActiveChunks->mutex);
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(m_RemoveQueueMutex);
            if (m_ToBeRemoved.contains(chunkPosition))
            {
                m_ToBeRemoved.erase(chunkPosition);
                m_SerializeCondition.notify_one();
                continue;
            }

            if (!m_ActiveChunks->chunks.contains(chunkPosition))
            {
                m_SerializeCondition.notify_one();
                continue;
            }
        }
        spdlog::info("Serializing: {}: {}", id, glm::to_string(chunkPosition));
        Timer::startTimer(timerString);

        std::vector<std::vector<SVONode>> queues;

        std::vector<SVONode> finalNodes;

        queues.resize(m_Depth);
        for (size_t i = 0; i < queues.size(); ++i)
        {
            queues[i].reserve(8);
        }

        int depth = m_Depth;

        std::vector<Voxel> voxels;
        {
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(m_ActiveChunks->mutex);
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(m_RemoveQueueMutex);

            if (m_ToBeRemoved.contains(chunkPosition))
            {
                m_ToBeRemoved.erase(chunkPosition);
                m_SerializeCondition.notify_one();
                Timer::stopTimer(timerString);
                continue;
            }

            if (m_ActiveChunks->chunks.contains(chunkPosition))
            {
                voxels = m_ActiveChunks->chunks.at(chunkPosition).getVoxels();
            }
            else
            {
                m_GenerateCondition.notify_one();
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
            node.flags ^= SVONODE_Im_SOLID;
            node.flags ^= SVONODE_Im_AIR * (v.colourIndex < 0);

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
                parent.flags ^= SVONODE_Im_PARENT;

                bool childrenSolid = true;
                {
                    // PROF_ZONE_NAMED_N(PROF_ZONE_3, "Checking Children", true);
                    for (size_t j = 0; j < 8; ++j)
                    {
                        const SVONode& child = childQueue[j];
                        bool isAir = child.flags & SVONODE_Im_AIR;
                        bool isSolid = child.flags & SVONODE_Im_SOLID;

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
                    parent.flags ^= SVONODE_Im_SOLID;
                }
                if (highestCount == -1) parent.flags ^= SVONODE_Im_AIR; // All Children are air
                parent.materialIndex = colour;

                // Not all Children are the same so create children nodes
                if (!(parent.flags & SVONODE_Im_SOLID) && !(parent.flags & SVONODE_Im_AIR))
                {
                    for (size_t j = 0; j < 8; ++j)
                    {
                        SVONode& child = childQueue[j];

                        if (child.childPointer != 0)
                        {
                            child.childPointer = finalNodes.size() - child.childPointer;
                        }

                        if ((child.flags & SVONODE_Im_AIR) == 0) // Is Not Air
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
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk3(m_ActiveChunks->mutex);

            if (m_ToBeRemoved.contains(chunkPosition) ||
                !m_ActiveChunks->chunks.contains(chunkPosition))
            {
                std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk2(m_RemoveQueueMutex);
                m_ToBeRemoved.erase(chunkPosition);
                Timer::stopTimer(timerString);
                m_SerializeCondition.notify_one();
                continue;
            }

            size_t size = m_ActiveChunks->chunks.at(chunkPosition).getVoxels().size();

            size_t bytes = reversed.size() * sizeof(SVONode);
            spdlog::info("{} Generated {} nodes ({} Voxels) ({} B) ({} KiB) ({} MiB).",
                         glm::to_string(chunkPosition), reversed.size(), size, bytes, bytes / 1024,
                         bytes / (1024 * 1024));

            spdlog::info("~{} bytes per voxel", (float)bytes / (float)size);

            createStaging(reversed.size());
            createSVO(m_ActiveChunks->chunks.at(chunkPosition).getSVOBuffer(), reversed.size());
            m_StagingBuffer.copyFromData_CPUOnly<SVONode>(reversed);

            copyStagingToChunk(chunkPosition, reversed.size() * sizeof(SVONode));

            m_ActiveChunks->chunks.at(chunkPosition).setIsGenerated(true);
        }

        Timer::stopTimer(timerString);
    }
}

void ChunkGenerator::transitionImages()
{
    PROF_ZONE_SCOPED;
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(m_ComputeQueueAccess);

    VK_CHECK(vkResetFences(m_Device, 1, &m_CopyFence));
    VK_CHECK(vkResetCommandBuffer(m_CopyCommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(m_CopyCommandBuffer, &commandBufferBI));

    m_GeneratedVoxels.transition(m_CopyCommandBuffer, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_GENERAL);

    VK_CHECK(vkEndCommandBuffer(m_CopyCommandBuffer));

    VkCommandBufferSubmitInfo commandBufferSI{};
    commandBufferSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandBufferSI.pNext = nullptr;
    commandBufferSI.commandBuffer = m_CopyCommandBuffer;
    commandBufferSI.deviceMask = 0;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.pNext = nullptr;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferSI;

    VK_CHECK(vkQueueSubmit2(m_ComputeQueue, 1, &submitInfo, m_CopyFence));

    VK_CHECK(vkWaitForFences(m_Device, 1, &m_CopyFence, true, 1e10));
}

void ChunkGenerator::copyStagingToBuffer(Buffer* buffer)
{
    VK_CHECK(vkResetFences(m_Device, 1, &m_CopyFence));
    VK_CHECK(vkResetCommandBuffer(m_CopyCommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(m_CopyCommandBuffer, &commandBufferBI));

    buffer->copyFromBuffer(m_CopyCommandBuffer, m_StagingBuffer, buffer->getSize());

    VK_CHECK(vkEndCommandBuffer(m_CopyCommandBuffer));

    VkCommandBufferSubmitInfo commandBufferSI{};
    commandBufferSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandBufferSI.pNext = nullptr;
    commandBufferSI.commandBuffer = m_CopyCommandBuffer;
    commandBufferSI.deviceMask = 0;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.pNext = nullptr;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferSI;

    VK_CHECK(vkQueueSubmit2(m_ComputeQueue, 1, &submitInfo, m_CopyFence));

    VK_CHECK(vkWaitForFences(m_Device, 1, &m_CopyFence, true, 1e10));
}

void ChunkGenerator::copyStagingToChunk(glm::ivec3 chunkPosition, size_t size)
{
    PROF_ZONE_SCOPED;
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(m_ComputeQueueAccess);

    copyStagingToBuffer(m_ActiveChunks->chunks.at(chunkPosition).getSVOBuffer());
}

void ChunkGenerator::createSVO(Buffer* buffer, size_t count)
{
    buffer->create(m_Allocator, count * sizeof(SVONode),
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                   VMA_MEMORY_USAGE_GPU_ONLY);
}

void ChunkGenerator::createStaging(size_t count, size_t elem_size)
{
    size_t size = count * elem_size;
    if (m_StagingBuffer.getSize() < size)
    {
        m_StagingBuffer.free();

        m_StagingBuffer.create(m_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
}
