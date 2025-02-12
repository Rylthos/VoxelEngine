#include "SuperBrick.hpp"

#include <cstdio>
#include <cstdlib>
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

void SuperBrick::init(VkDevice device, VmaAllocator allocator)
{
    m_Device = device;
    m_Allocator = allocator;

    for (size_t i = 0; i < m_CurrentPoolSize; i++) {
        m_FreeIndices.insert(i);
    }

    for (size_t i = 0; i < m_Struct.data.size(); i++) {
        m_Struct.data[i] = {
            .loaded = 0,
            .empty_flag = 0,
        };
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

    m_Initialized = true;
}

void SuperBrick::free()
{
    if (!m_Initialized)
        return;

    m_BrickPool.free();
    m_Staging.free();

    m_Colours.free();
}

bool SuperBrick::hasGenerated(glm::ivec3 brickIndex)
{
    if (!m_Bricks.contains(brickIndex)) {
        return false;
    }

    return true;
}

void SuperBrick::setRequested(std::tuple<glm::ivec3, glm::ivec3, glm::ivec3> position)
{
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);

    glm::ivec3 brickPosition = std::get<2>(position);
    size_t index = brickPosition.x + brickPosition.z * SUPERBRICK_SIZE
        + brickPosition.y * SUPERBRICK_SIZE * SUPERBRICK_SIZE;

    m_Struct.data[index].requested_flag = 1;
}

void SuperBrick::loadBrick(std::tuple<glm::ivec3, glm::ivec3, glm::ivec3> position, Brick& brick)
{
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_LoadedLock);

    glm::ivec3 brickIndex = std::get<2>(position);

    if (m_Bricks.contains(brickIndex)) {
        return;
    }

    m_Bricks[brickIndex] = brick;
    m_ToBeLoaded.insert(brickIndex);
}

void SuperBrick::setVoxels(WorldBrickPosition pos,
    const std::vector<std::pair<glm::ivec3, VoxelOp>>& changes, bool replace)
{
    std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock(m_BufferLock);

    glm::ivec3 brickIndex = std::get<2>(pos);
    for (size_t i = 0; i < changes.size(); i++) {
        const auto& change = changes[i];
        if (std::holds_alternative<PLACE_OP>(change.second))
            m_Bricks[brickIndex].setVoxel(change.first, std::get<PLACE_OP>(change.second), replace);
        if (std::holds_alternative<ERASE_OP>(change.second))
            m_Bricks[brickIndex].setAir(change.first);
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

SuperBrickStruct SuperBrick::getStruct()
{
    if (m_ToBeLoaded.size() != 0) {
        std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lock1(m_BufferLock);
        std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_LoadedLock);

        size_t totalColourNeeded = 0;
        for (const auto& p : m_ToBeLoaded) {
            totalColourNeeded += m_Bricks[p].getColoursSize();
        }

        size_t stagingSize = sizeof(BrickStruct) * m_ToBeLoaded.size();
        generateStaging(stagingSize);

        std::vector<glm::vec4> newColours;
        newColours.reserve(totalColourNeeded);
        size_t offset = 0;
        std::unordered_map<size_t, std::pair<size_t, size_t>> mapping;

        IntervalList<int> stagingCommit;
        std::unordered_map<int, size_t> stagingMapping;
        size_t colourOffset = 0;

        Buffer::startCopyFromBuffer();
        for (glm::ivec3 p : m_ToBeLoaded) {
            Brick& brick = m_Bricks[p];
            size_t index = p.x + p.z * SUPERBRICK_SIZE + p.y * SUPERBRICK_SIZE * SUPERBRICK_SIZE;

            auto brickStruct = brick.getStruct();

            m_Struct.data[index].loaded = 1;

            if (!brickStruct.has_value()) {
                m_Struct.data[index].empty_flag = 1;
                continue;
            }

            const auto& colours = brick.getColours();

            auto colourInterval = m_AvailableColourIndices.getFirstGreater(colours.size());
            if (!colourInterval.has_value()) {
                resizeColours(true);
                colourInterval = m_AvailableColourIndices.getFirstGreater(colours.size());
            }
            brickStruct->colourPtr = colourInterval->first;

            if (m_ToBeLoaded.size() > m_FreeIndices.size()) {
                resizeBricks(true);
            }

            size_t chosenIndex = *m_FreeIndices.begin();
            m_GeneratedBricks[p] = chosenIndex;

            m_Struct.data[index].pointer = chosenIndex;
            m_Struct.data[index].empty_flag = 0;

            m_FreeIndices.erase(chosenIndex);

            std::memcpy(
                ((char*)m_Staging.getAllocationInfo().pMappedData) + offset * sizeof(BrickStruct),
                &brickStruct.value(), sizeof(BrickStruct));

            stagingCommit.addInterval(chosenIndex);
            stagingMapping[chosenIndex] = offset;
            offset += 1;

            mapping.insert({
                colourOffset, { colourInterval->first, colours.size() }
            });
            m_AvailableColourIndices.removeInterval(
                colourInterval->first, colourInterval->first + colours.size() - 1);

            newColours.insert(newColours.end(), colours.begin(), colours.end());
            colourOffset += colours.size();
            m_AllocatedColourSizes[p] = { colourInterval->first, colours.size() };
        }

        for (const auto& m : stagingCommit.getIntervals()) {
            size_t size = stagingCommit.sizeOfInterval(m);
            size_t srcOffset = stagingMapping[m.first];
            size_t dstOffset = m.first;
            m_BrickPool.copyFromBuffer(m_Staging, sizeof(BrickStruct) * size,
                srcOffset * sizeof(BrickStruct), dstOffset * sizeof(BrickStruct));
        }
        Buffer::endCopyFromBuffer();

        generateStaging(newColours.size() * sizeof(glm::vec4));
        m_Staging.copyFromData_CPUOnly<glm::vec4>(newColours);

        Buffer::startCopyFromBuffer();
        for (const auto& m : mapping) {
            size_t srcOffset = m.first;
            size_t dstOffset = m.second.first;
            size_t size = m.second.second;
            m_Colours.copyFromBuffer(m_Staging, size * sizeof(glm::vec4),
                srcOffset * sizeof(glm::vec4), dstOffset * sizeof(glm::vec4));
        }
        m_CurrentColourCount += newColours.size();
        Buffer::endCopyFromBuffer();

        m_ToBeLoaded.clear();
    }

    m_Struct.bricks = m_BrickPool.getDeviceAddress(m_Device);
    m_Struct.colour = m_Colours.getDeviceAddress(m_Device);

    return m_Struct;
}

void SuperBrick::generateStaging(size_t size)
{
    if (m_Staging.getSize() >= size) {
        return;
    }

    bool wasEnabled = Buffer::endCopyFromBuffer();

    m_Staging.free();
    m_Staging.create(m_Allocator, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

    if (wasEnabled)
        Buffer::startCopyFromBuffer();
}

void SuperBrick::resizeColours(bool preserveStaging)
{
    spdlog::info("Resizing Colours");
    size_t previous = m_MaxColours;
    m_MaxColours *= 2;
    m_AvailableColourIndices.addInterval(previous, m_MaxColours - 1);

    bool wasEnabled = Buffer::endCopyFromBuffer();

    size_t size = m_Colours.getSize();

    size_t stagingSize = m_Staging.getSize();
    char* copy;
    if (preserveStaging) {
        copy = (char*)malloc(stagingSize);
        memcpy(copy, m_Staging.getAllocationInfo().pMappedData, stagingSize);
    }

    generateStaging(size);
    m_Staging.copyFromBuffer(m_Colours, size);

    m_Colours.free();
    m_Colours.create(m_Allocator, m_MaxColours * sizeof(glm::vec4),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    m_Colours.copyFromBuffer(m_Staging, size);

    if (preserveStaging) {
        memcpy(m_Staging.getAllocationInfo().pMappedData, copy, stagingSize);
        std::free(copy);
    }

    if (wasEnabled)
        Buffer::startCopyFromBuffer();
}

void SuperBrick::resizeBricks(bool preserveStaging)
{
    spdlog::info("Resizing Bricks");
    size_t previous = m_CurrentPoolSize;
    m_CurrentPoolSize *= 2;

    bool wasEnabled = Buffer::endCopyFromBuffer();

    for (size_t i = previous; i < m_CurrentPoolSize; i++) {
        m_FreeIndices.insert(i);
    }

    size_t size = m_BrickPool.getSize();

    size_t stagingSize = m_Staging.getSize();
    char* copy;
    if (preserveStaging) {
        copy = (char*)malloc(stagingSize);
        memcpy(copy, m_Staging.getAllocationInfo().pMappedData, stagingSize);
    }
    generateStaging(size);

    m_Staging.copyFromBuffer(m_BrickPool, size);

    m_BrickPool.free();
    m_BrickPool.create(m_Allocator, m_CurrentPoolSize * sizeof(BrickStruct),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    m_BrickPool.copyFromBuffer(m_Staging, size);

    if (preserveStaging) {
        memcpy(m_Staging.getAllocationInfo().pMappedData, copy, stagingSize);
        std::free(copy);
    }

    if (wasEnabled)
        Buffer::startCopyFromBuffer();
}
