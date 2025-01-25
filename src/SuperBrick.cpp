#include "SuperBrick.hpp"

#include <pthread.h>
#include <unordered_map>
#include <variant>
#include <vulkan/vulkan_core.h>

#include <glm/gtx/string_cast.hpp>

#include "Brick.hpp"
#include "Buffer.hpp"
#include "Profilling.hpp"
#include "SceneManager.hpp"
#include "ShaderModule.hpp"
#include "Timer.hpp"
#include "VkCheck.hpp"
#include "imgui.h"

SuperBrick::SuperBrick() { }

void SuperBrick::init(VkDevice device, VmaAllocator allocator, Queue* computeQueue)
{
    m_Device = device;
    m_Allocator = allocator;
    m_ComputeQueue = computeQueue;

    for (size_t i = 0; i < m_CurrentPoolSize; i++) {
        m_FreeIndices.insert(i);
    }

    for (size_t i = 0; i < m_Struct.data.size(); i++) {
        m_Struct.data[i] = {
            .loaded = 0,
            .empty_flag = 0,
        };
    }

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
            vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr, &m_GeneratePipelineLayout));

        ShaderModule voxelShader;
        voxelShader.create("res/shaders/GenerateBrickmap.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI {};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = voxelShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI {};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_GeneratePipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(
            m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr, &m_GeneratePipeline));
    }

    m_Colours.create(m_Allocator, m_MaxColours * sizeof(glm::vec4),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    m_AvailableColourIndices.addInterval(0, m_MaxColours - 1);

    m_BrickPool.create(m_Allocator, m_CurrentPoolSize * sizeof(BrickStruct),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    m_Running = true;

    m_GenerationThreads.resize(m_NumGenerationThreads);
    for (size_t i = 0; i < m_NumGenerationThreads; i++) {
        m_GenerationThreads[i] = std::thread(&SuperBrick::generateBrickLoop, this, i);
    }

    m_Initialized = true;
}

void SuperBrick::free()
{
    if (!m_Initialized)
        return;

    m_Running = false;
    m_CanGenerate.notify_all();
    for (auto& thread : m_GenerationThreads) {
        thread.join();
    }

    m_BrickPool.free();
    m_Staging.free();

    m_Colours.free();

    vkDestroyPipeline(m_Device, m_GeneratePipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_GeneratePipelineLayout, nullptr);
}

void SuperBrick::addBrickToQueue(glm::ivec3 position)
{
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_GeneratedQueueLock);
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_EnqueuedLock);

    if (m_Bricks.contains(position))
        return;

    if (m_Enqueued.contains(position))
        return;

    if (m_ToBeLoaded.contains(position))
        return;

    m_Enqueued.insert(position);
    m_ToBeGenerated.push_back(position);

    m_CanGenerate.notify_one();
}

void SuperBrick::addBrickToQueue(uint32_t index)
{
    glm::ivec3 position { 0 };
    position.x = index % SUPERBRICK_SIZE;
    position.z = (index / SUPERBRICK_SIZE) % SUPERBRICK_SIZE;
    position.y = (index / (SUPERBRICK_SIZE * SUPERBRICK_SIZE)) % SUPERBRICK_SIZE;

    addBrickToQueue(position);
}

void SuperBrick::placeVoxel(
    glm::ivec3 brickIndex, glm::ivec3 voxelIndex, glm::vec4 colour, bool replace)
{
    std::vector<VoxelChange> temp = { std::make_tuple(brickIndex, voxelIndex, colour) };
    setVoxels(temp, replace);
}

void SuperBrick::eraseVoxel(glm::ivec3 brickIndex, glm::ivec3 voxelIndex, bool replace)
{
    std::vector<VoxelChange> temp = { std::make_tuple(brickIndex, voxelIndex, 0) };
    setVoxels(temp, true);
}

void SuperBrick::changeVoxels(const std::vector<VoxelChange>& voxels, bool replace)
{
    setVoxels(voxels, replace);
}

SuperBrickStruct SuperBrick::getStruct()
{
    if (m_ToBeLoaded.size() != 0) {
        std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);
        std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_LoadedLock);

        if (m_FreeIndices.size() < m_ToBeLoaded.size()) {
            size_t previous = m_CurrentPoolSize;
            m_CurrentPoolSize *= 2;

            for (size_t i = previous; i < m_CurrentPoolSize; i++) {
                m_FreeIndices.insert(i);
            }

            size_t size = m_BrickPool.getSize();
            generateStaging(size);
            m_Staging.copyFromBuffer(m_BrickPool, size);

            m_BrickPool.free();
            m_BrickPool.create(m_Allocator, m_CurrentPoolSize * sizeof(BrickStruct),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            m_BrickPool.copyFromBuffer(m_Staging, size);
        }

        size_t coloursSize = 0;
        for (const auto& p : m_ToBeLoaded) {
            size_t size = m_Bricks[p].getColoursSize();
            coloursSize += size;
        }

        if (m_CurrentColourCount + coloursSize > m_MaxColours) {
            size_t sum = m_CurrentColourCount + coloursSize;
            size_t previous = m_MaxColours;
            while (sum > m_MaxColours) {
                m_MaxColours *= 2;
            }

            m_AvailableColourIndices.addInterval(previous, m_MaxColours - 1);

            size_t size = m_Colours.getSize();
            generateStaging(size);
            m_Staging.copyFromBuffer(m_Colours, size);

            m_Colours.free();
            m_Colours.create(m_Allocator, m_MaxColours * sizeof(glm::vec4),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            m_Colours.copyFromBuffer(m_Staging, size);
        }

        size_t stagingSize = sizeof(BrickStruct) * m_ToBeLoaded.size();
        generateStaging(stagingSize);

        std::vector<glm::vec4> newColours;
        size_t offset = 0;
        std::unordered_map<size_t, std::pair<size_t, size_t>> mapping;

        IntervalList<int> stagingCommit;
        std::unordered_map<int, size_t> stagingMapping;

        for (glm::ivec3 p : m_ToBeLoaded) {
            if (m_GeneratedBricks.contains(p))
                continue;

            Brick& brick = m_Bricks[p];
            size_t index = p.x + p.z * SUPERBRICK_SIZE + p.y * SUPERBRICK_SIZE * SUPERBRICK_SIZE;

            auto brickStruct = brick.getStruct();

            if (!brickStruct.has_value()) {
                m_Struct.data[index].loaded = 1;
                m_Struct.data[index].empty_flag = 1;
                continue;
            }

            const auto& colours = brick.getColours();

            auto colourInterval = m_AvailableColourIndices.getFirstGreater(colours.size());
            brickStruct->colourPtr = colourInterval->first;

            size_t chosenIndex = *m_FreeIndices.begin();
            m_GeneratedBricks[p] = chosenIndex;

            m_Struct.data[index].pointer = chosenIndex;
            m_Struct.data[index].loaded = 1;
            m_Struct.data[index].empty_flag = 0;

            m_FreeIndices.erase(chosenIndex);

            std::vector<BrickStruct> temp { brickStruct.value() };

            std::memcpy(
                (char*)m_Staging.getAllocationInfo().pMappedData + offset * sizeof(BrickStruct),
                &brickStruct.value(), sizeof(BrickStruct));

            stagingCommit.addInterval(chosenIndex);
            stagingMapping[chosenIndex] = offset;
            offset += 1;

            mapping.insert({ newColours.size(), { colourInterval->first, colours.size() } });
            m_AvailableColourIndices.removeInterval(
                colourInterval->first, colourInterval->first + colours.size() - 1);

            newColours.insert(newColours.end(), colours.begin(), colours.end());
            m_AllocatedColourSizes[p] = { colourInterval->first, colours.size() };
        }

        m_BrickPool.startCopyFromBuffer();
        for (const auto& m : stagingCommit.getIntervals()) {
            size_t size = stagingCommit.sizeOfInterval(m);
            size_t srcOffset = stagingMapping[m.first];
            size_t dstOffset = m.first;
            m_BrickPool.copyData(m_Staging, sizeof(BrickStruct) * size,
                srcOffset * sizeof(BrickStruct), dstOffset * sizeof(BrickStruct));
        }
        m_BrickPool.endCopyFromBuffer();

        generateStaging(newColours.size() * sizeof(glm::vec4));
        m_Staging.copyFromData_CPUOnly<glm::vec4>(newColours);

        m_BrickPool.startCopyFromBuffer();
        for (const auto& m : mapping) {
            size_t srcOffset = m.first;
            size_t dstOffset = m.second.first;
            size_t size = m.second.second;
            m_Colours.copyData(m_Staging, size * sizeof(glm::vec4), srcOffset * sizeof(glm::vec4),
                dstOffset * sizeof(glm::vec4));
        }
        m_BrickPool.endCopyFromBuffer();
        m_CurrentColourCount += newColours.size();

        m_ToBeLoaded.clear();

        m_Struct.bricks = m_BrickPool.getDeviceAddress(m_Device);
        m_Struct.colour = m_Colours.getDeviceAddress(m_Device);
    }
    return m_Struct;
}

void SuperBrick::reset()
{
    m_CurrentPoolSize = 512;

    m_FreeIndices.clear();
    for (size_t i = 0; i < m_CurrentPoolSize; i++) {
        m_FreeIndices.insert(i);
    }

    m_BrickPool.free();
    m_BrickPool.create(m_Allocator, m_CurrentPoolSize * sizeof(BrickStruct),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    m_Bricks.clear();
    m_ToBeLoaded.clear();
    m_GeneratedBricks.clear();
    m_ToBeGenerated.clear();
    m_Enqueued.clear();

    for (size_t i = 0; i < m_Struct.data.size(); i++) {
        m_Struct.data[i] = {};
    }
}

void SuperBrick::transformChange(
    VoxelChange change, glm::ivec3& brickIndex, glm::ivec3& voxelIndex, VoxelOp& op)
{
    brickIndex = std::get<0>(change);
    voxelIndex = std::get<1>(change);
    op = std::get<2>(change);

    for (int i = 0; i < 3; i++) {
        int brickOffset = (int)std::floor(voxelIndex[i] / (float)BRICK_SIZE);
        // int brickOffset = voxelIndex[i] >> 4;
        int voxelOffset = (voxelIndex[i] % BRICK_SIZE + BRICK_SIZE) % BRICK_SIZE;

        voxelIndex[i] = voxelOffset;
        brickIndex[i] += brickOffset;
    }
}

void SuperBrick::transformChanges(const std::vector<VoxelChange> changes,
    std::unordered_map<glm::ivec3, std::vector<std::pair<glm::ivec3, VoxelOp>>>& groupedChanges)
{
    PROF_ZONE_SCOPED;
    for (const VoxelChange& change : changes) {
        glm::ivec3 brickIndex;
        glm::ivec3 voxelIndex;
        VoxelOp op;

        transformChange(change, brickIndex, voxelIndex, op);

        if (brickIndex.x < 0 || brickIndex.x >= SUPERBRICK_SIZE || brickIndex.y < 0
            || brickIndex.y >= SUPERBRICK_SIZE || brickIndex.z < 0
            || brickIndex.z >= SUPERBRICK_SIZE) {
            continue;
        }

        groupedChanges[brickIndex].emplace_back(voxelIndex, op);
    }
}

void SuperBrick::setVoxels(const std::vector<VoxelChange>& changes, bool replace)
{
    PROF_ZONE_SCOPED;
    std::unordered_map<glm::ivec3, std::vector<std::pair<glm::ivec3, VoxelOp>>> groupedChanges;
    transformChanges(changes, groupedChanges);

    for (const auto& brickChanges : groupedChanges) {
        glm::ivec3 brickIndex = brickChanges.first;

        if (!m_Bricks.contains(brickIndex)) {
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock(m_QueuedChangesLock);
            for (const auto& change : brickChanges.second) {
                m_QueuedChanges[brickIndex].insert({ change.first, change.second });
            }

            continue;
        }

        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock(m_BufferLock);
        for (const auto& change : brickChanges.second) {
            if (std::holds_alternative<ERASE_OP>(change.second)) {
                m_Bricks.at(brickIndex).setAir(change.first);
            } else {
                m_Bricks.at(brickIndex)
                    .setVoxel(change.first, std::get<PLACE_OP>(change.second), replace);
            }
        }

        std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_LoadedLock);
        m_ToBeLoaded.insert(brickIndex);
        if (m_GeneratedBricks.contains(brickIndex)) {
            uint16_t lookup = m_GeneratedBricks[brickIndex];
            m_GeneratedBricks.erase(brickIndex);
            m_FreeIndices.insert(lookup);

            auto colourAllocation = m_AllocatedColourSizes[brickIndex];
            m_AvailableColourIndices.addInterval(
                colourAllocation.first, colourAllocation.first + colourAllocation.second - 1);
            m_AllocatedColourSizes.erase(brickIndex);
            m_CurrentColourCount -= colourAllocation.second;
        }
    }
}

void SuperBrick::generateStaging(size_t size)
{
    if (m_Staging.getSize() >= size) {
        return;
    }

    m_Staging.free();
    m_Staging.create(m_Allocator, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

void SuperBrick::generateBrickLoop(size_t id)
{
    Buffer generatedData;
    Buffer generatedColour;
    generatedData.create(m_Allocator, sizeof(uint32_t) + sizeof(uint64_t) * 8,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

    generatedColour.create(m_Allocator, sizeof(glm::vec4) * BRICK_SIZE * BRICK_SIZE * BRICK_SIZE,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

    VkCommandPool commandPool;
    VkCommandPoolCreateInfo commandPoolCI {};
    commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCI.pNext = nullptr;
    commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCI.queueFamilyIndex = m_ComputeQueue->queueFamily;

    VK_CHECK(vkCreateCommandPool(m_Device, &commandPoolCI, nullptr, &commandPool));

    VkCommandBuffer commandBuffer;

    VkCommandBufferAllocateInfo commandBufferAI {};
    commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAI.pNext = nullptr;
    commandBufferAI.commandPool = commandPool;
    commandBufferAI.commandBufferCount = 1;
    commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(m_Device, &commandBufferAI, &commandBuffer));

    std::string timerString = std::format("Brick Generate: {}", id);

    VkFence generationFence;
    {
        VkFenceCreateInfo fenceCI {};
        fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCI.pNext = nullptr;
        fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateFence(m_Device, &fenceCI, nullptr, &generationFence));
    }
    while (m_Running) {
        glm::ivec3 position;
        {
            std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lock(m_GeneratedQueueLock);
            while (m_ToBeGenerated.size() == 0) {
                m_CanGenerate.wait(lock, [this] { return !m_ToBeGenerated.empty() || !m_Running; });

                if (!m_Running)
                    break;
            }

            position = m_ToBeGenerated.front();
            m_ToBeGenerated.pop_front();
        }
        if (!m_Running)
            break;

        VK_CHECK(vkResetFences(m_Device, 1, &generationFence));
        VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

        Timer::startTimer(timerString);
        VkCommandBufferBeginInfo commandBufferBI {};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBI.pNext = nullptr;
        commandBufferBI.pInheritanceInfo = nullptr;
        commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        {
            std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk(m_ComputeQueue->queueMutex);
            VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBI));
            {
                vkCmdBindPipeline(
                    commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_GeneratePipeline);

                GenerationPushConstants pushConstants;
                pushConstants.brickIndex = position;
                pushConstants.worldPosition = position * BRICK_SIZE;
                pushConstants.data = generatedData.getDeviceAddress(m_Device);
                pushConstants.colours = generatedColour.getDeviceAddress(m_Device);

                vkCmdPushConstants(commandBuffer, m_GeneratePipelineLayout,
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

            VK_CHECK(vkQueueSubmit2(m_ComputeQueue->queue, 1, &submitInfo, generationFence));
        }
        VK_CHECK(vkWaitForFences(m_Device, 1, &generationFence, true, 1e10));

        const uint32_t* data = (const uint32_t*)(generatedData.getAllocationInfo().pMappedData);
        const uint32_t solidVoxels = *data;
        const uint64_t* mask = (const uint64_t*)(data + 1);

        const glm::vec4* colour_data
            = (const glm::vec4*)(generatedColour.getAllocationInfo().pMappedData);

        Brick brick;
        if (solidVoxels != 0) {
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
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock(m_QueuedChangesLock);
            if (m_QueuedChanges.contains(position)) {
                auto copy = m_QueuedChanges[position];
                for (auto p : copy) {
                    if (std::holds_alternative<ERASE_OP>(p.second)) {
                        brick.setAir(p.first);
                    } else if (std::holds_alternative<PLACE_OP>(p.second)) {
                        brick.setVoxel(p.first, std::get<PLACE_OP>(p.second), true);
                    }
                }

                m_QueuedChanges.erase(position);
            }
        }
        Timer::stopTimer(timerString);

        {
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock(m_BufferLock);
            m_Bricks[position] = brick;
        }

        {
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_LoadedLock);
            std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_EnqueuedLock);
            m_ToBeLoaded.insert(position);
            m_Enqueued.erase(position);
        }
    }

    vkDestroyFence(m_Device, generationFence, nullptr);

    generatedData.free();
    generatedColour.free();

    vkDestroyCommandPool(m_Device, commandPool, nullptr);
}
