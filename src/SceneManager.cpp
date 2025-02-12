#include "SceneManager.hpp"

#include <algorithm>

#include <concepts>
#include <glm/gtx/string_cast.hpp>
#include <iterator>
#include <memory>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "Brick.hpp"
#include "Buffer.hpp"
#include "Chunk.hpp"
#include "ChunkGenerator.hpp"
#include "Constants.hpp"
#include "SuperBrick.hpp"
#include "Timer.hpp"

#include "Events.hpp"
#include "imgui.h"
#include "spdlog/fmt/bundled/core.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <vulkan/vulkan_core.h>

// #include "ChunkGenerator.hpp"

SceneManager::SceneManager(PaletteManager* paletteManager, Camera* camera)
    : m_Dimension(1 << 8), m_PaletteManager(paletteManager), m_Camera(camera)

{
    m_VoxelPushConstants.maxIterations = 1024;
    m_VoxelPushConstants.maxHeatShown = m_VoxelPushConstants.maxIterations;
}

SceneManager::SceneManager(SceneManager& other)
{
    m_Device = other.m_Device;
    m_Allocator = other.m_Allocator;
    m_Dimension = other.m_Dimension;
    m_Camera = other.m_Camera;
    m_PaletteManager = other.m_PaletteManager;
    m_VoxelPushConstants = other.m_VoxelPushConstants;
}

SceneManager SceneManager::operator=(const SceneManager& other)
{
    m_Device = other.m_Device;
    m_Allocator = other.m_Allocator;
    m_Dimension = other.m_Dimension;
    m_Camera = other.m_Camera;
    m_PaletteManager = other.m_PaletteManager;
    m_VoxelPushConstants = other.m_VoxelPushConstants;

    return *this;
}

void SceneManager::initResources(VkDevice device, VmaAllocator allocator, Queue* computeQueue)
{
    if (m_Initialized)
        return;

    m_Device = device;
    m_Allocator = allocator;

    m_Initialized = true;
    spdlog::info("Initliazing Scene Manager");

    m_Chunks[{ 0, 0, 0 }].init(device, allocator, computeQueue);

    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        m_ToBeLoaded[i].create(m_Allocator, sizeof(uint32_t) * 4 + sizeof(LoadedData) * MAX_LOADED,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    }

    m_ChunkBuffer.create(m_Allocator, sizeof(ChunkStruct),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

    m_FeedbackBuffer.create(m_Allocator, sizeof(Feedback),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);

    m_CurrentColour = glm::vec3(1.);

    m_VoxelPushConstants.sunDirection = glm::vec4(0, -1, 0, 1);

    m_VoxelPushConstants.brickLODDistance = 100.f;
    m_VoxelPushConstants.superBrickLODDistance = 200.f;

    ChunkGenerator::addChunks(&m_Chunks);
}

void SceneManager::freeResources()
{
    if (!m_Initialized)
        return;

    freeBuffers();
    m_Initialized = false;
}

void SceneManager::receive(const Event* event)
{
    static float currentT = 0.0;

    static float previousState = m_AnimateCutoff;

    switch (event->getType()) {
    case EventType::GameUpdate: {
        const GameUpdate* gu = static_cast<const GameUpdate*>(event);

        reedbackFeedback();

        int leftLength = 0;
        int rightLength = 0;

        if (glm::dot(glm::vec3(m_Feedback.voxelNormal), glm::vec3(1.)) < 0.) {
            leftLength = m_PlacementSize / 2;
            rightLength
                = (m_PlacementSize % 2 == 0) ? ((m_PlacementSize - 1) / 2) : (m_PlacementSize / 2);
        } else {
            leftLength
                = (m_PlacementSize % 2 == 0) ? ((m_PlacementSize - 1) / 2) : (m_PlacementSize / 2);
            rightLength = m_PlacementSize / 2;
        }

        // glm::ivec3 offset = glm::ivec3(
        //     glm::vec3(m_Feedback.voxelNormal) * ((float)(m_PlacementSize + 1.f) / 2.f));
        glm::ivec3 center = m_Feedback.voxelIndex;

        if (m_Feedback.hasHitBrick && m_Feedback.hasHitVoxel && (m_PlaceVoxel || m_EraseVoxel)) {
            PROF_ZONE_SCOPED;
            Timer::startTimer("Modify Voxels");
            static std::vector<VoxelChange> changes;
            changes.clear();
            changes.reserve(m_PlacementSize * m_PlacementSize * m_PlacementSize);

            for (int y = -leftLength; y <= rightLength; y++) {
                for (int z = -leftLength; z <= rightLength; z++) {
                    for (int x = -leftLength; x <= rightLength; x++) {
                        glm::ivec3 newIndex = center + glm::ivec3(x, y, z);
                        bool canPlace = true;
                        switch (m_CurrentPlacement) {
                        case PlacementType::Cube:
                            break;
                        case PlacementType::Sphere: {
                            if (glm::length(glm::vec3(newIndex - center))
                                > (float)(m_PlacementSize / 2.)) {
                                canPlace = false;
                            }
                            break;
                        }
                        default:
                            break;
                        }

                        if (!canPlace) {
                            continue;
                        }

                        VoxelOp op;
                        if (m_PlaceVoxel && !m_EraseVoxel) {
                            op = glm::vec4(m_CurrentColour, 1.);
                        }
                        if (m_EraseVoxel && !m_PlaceVoxel) {
                            op = 0;
                        }

                        changes.push_back({
                            { m_Feedback.chunkIndex, m_Feedback.superBrickIndex,
                             m_Feedback.brickIndex, newIndex },
                            op
                        });
                    }
                }
            }

            setVoxels(changes, m_ReplaceVoxels);
            Timer::stopTimer("Modify Voxels");
        }

        if (!m_InfinitePlace) {
            m_PlaceVoxel = false;
            m_EraseVoxel = false;
        }

        break;
    }
    case EventType::MouseButton: {
        const MouseButton* mv = static_cast<const MouseButton*>(event);

        m_PlaceVoxel = mv->leftMousePressed && !mv->leftMouseReleased;
        m_EraseVoxel = mv->rightMousePressed && !mv->rightMouseReleased;

        break;
    }

    case EventType::MouseScroll: {
        const MouseScroll* ms = static_cast<const MouseScroll*>(event);
        int sign = (ms->yOffset < 0) ? -1 : 1;

        m_PlacementSize
            = std::clamp((int)m_PlacementSize + sign, MIN_PLACEMENT_SIZE, MAX_PLACEMENT_SIZE);

        break;
    }
    case EventType::ImGuiRender: {
        if (ImGui::Begin("Scene")) {
            if (ImGui::Button("Reset")) {
                // m_SuperBrick.reset();
            }

            ImGui::Text("Super brick LOD Distance");
            ImGui::SliderFloat("##SuperBrickLODDistance",
                &m_VoxelPushConstants.superBrickLODDistance, 10.f, 1000.f);

            ImGui::Text("Brick LOD Distance");
            ImGui::SliderFloat(
                "##BrickLODDistance", &m_VoxelPushConstants.brickLODDistance, 10.f, 1000.f);

            ImGui::Checkbox("Load Voxels", (bool*)&m_VoxelPushConstants.shouldLoadVoxels);

            ImGui::Text("Generation Queue: %ld", ChunkGenerator::getQueueSize());

            ImGui::Text("Max Heat");
            ImGui::SliderInt("##Heat", (int*)&m_VoxelPushConstants.maxHeatShown, 1, 1024);

            ImGui::Text("Hit Data");
            ImGui::Text("Hitting Chunk: %d", m_Feedback.hasHitChunk);
            ImGui::Text("Hitting Super Brick: %d", m_Feedback.hasHitSuperBrick);
            ImGui::Text("Hitting Brick: %d", m_Feedback.hasHitBrick);
            ImGui::Text("Hitting Voxel: %d", m_Feedback.hasHitVoxel);
            ImGui::Text("");
            ImGui::Text("Chunk Index: %s", glm::to_string(m_Feedback.chunkIndex).c_str());
            ImGui::Text(
                "Super brick Index: %s", glm::to_string(m_Feedback.superBrickIndex).c_str());
            ImGui::Text("Brick Index: %s", glm::to_string(m_Feedback.brickIndex).c_str());
            ImGui::Text("Voxel Index: %s", glm::to_string(m_Feedback.voxelIndex).c_str());
            ImGui::Text("");
            ImGui::Text("Voxel Normal: %s", glm::to_string(m_Feedback.voxelNormal).c_str());
        }
        ImGui::End();

        if (ImGui::Begin("Voxel Placement")) {

            float data[] = { m_CurrentColour.r, m_CurrentColour.g, m_CurrentColour.b };
            ImGui::Text("Placement Colour");
            ImGui::ColorEdit3("Placement Colour", (float*)&data,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            m_CurrentColour.r = data[0];
            m_CurrentColour.g = data[1];
            m_CurrentColour.b = data[2];

            int selected_idx = static_cast<int>(m_CurrentPlacement);
            const char* preview = PlacementTypeToString[selected_idx];
            int len = static_cast<int>(PlacementType::NUM_TYPES);

            if (ImGui::BeginCombo("##PlacementType", preview)) {
                for (int i = 0; i < len; i++) {
                    const bool is_selected = (selected_idx == i);
                    if (ImGui::Selectable(PlacementTypeToString[i], is_selected))
                        m_CurrentPlacement = static_cast<PlacementType>(i);

                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }

            ImGui::Checkbox("Infinite Place", &m_InfinitePlace);
            ImGui::Checkbox("Replace existing", &m_ReplaceVoxels);

            ImGui::Text("Placement Size");
            ImGui::SliderInt(
                "##PlacementSize", (int*)&m_PlacementSize, MIN_PLACEMENT_SIZE, MAX_PLACEMENT_SIZE);

            ImGui::Text("Placing: %d", m_PlaceVoxel);
            ImGui::Text("Erasing: %d", m_EraseVoxel);
        }
        ImGui::End();

        break;
    }
    default:
        break;
    }
}

VoxelPushConstants& SceneManager::getVoxelPushConstants(uint32_t currentFrame)
{
    PROF_ZONE_SCOPED;

    if (m_PauseRegeneration)
        return m_VoxelPushConstants;

    checkChunks(currentFrame);

    {
        std::vector<uint32_t> temp = { MAX_LOADED, 0 };
        createStaging(sizeof(uint32_t) * 2);
        m_Staging.copyFromData_CPUOnly<uint32_t>(temp);
        m_ToBeLoaded[currentFrame].copyFromBuffer(m_Staging, sizeof(uint32_t) * 2);
    }

    Buffer::startCopyFromBuffer();

    std::vector<ChunkStruct> temp = { m_Chunks[{ 0, 0, 0 }].getStruct() };
    memcpy(m_ChunkBuffer.getAllocationInfo().pMappedData, temp.data(), sizeof(ChunkStruct));

    Buffer::endCopyFromBuffer();

    m_VoxelPushConstants.toBeLoaded = m_ToBeLoaded[currentFrame].getDeviceAddress(m_Device);
    m_VoxelPushConstants.chunk = m_ChunkBuffer.getDeviceAddress(m_Device);
    m_VoxelPushConstants.feedbackBuffer = m_FeedbackBuffer.getDeviceAddress(m_Device);

    return m_VoxelPushConstants;
}

void SceneManager::checkChunks(uint32_t currentFrame)
{
    PROF_ZONE_SCOPED;
    if (m_PauseRegeneration)
        return;

    const uint32_t* data
        = (const uint32_t*)m_ToBeLoaded[currentFrame].getAllocationInfo().pMappedData;

    const LoadedData* loaded = (const LoadedData*)(data + 4);

    glm::ivec3 chunkPos = { 0, 0, 0 };

    uint32_t length = std::min(data[0], data[1]);
    if (length != 0) {
        for (uint32_t i = 0; i < length; i++) {
            const LoadedData l = loaded[i];
            if (l.brickIndex.a != 0) {
                auto localPosition = WorldBrickPosition { chunkPos, glm::ivec3(l.superBrickIndex),
                    glm::ivec3(l.brickIndex) };

                m_Chunks[chunkPos].setRequestedBrick(localPosition);

                ChunkGenerator::requestBrick(localPosition, m_Camera->getPosition());
            } else if (l.superBrickIndex.a != 0) {
                m_Chunks[chunkPos].setRequested({
                    chunkPos, glm::ivec3(l.superBrickIndex), { 0, 0, 0 }
                });

                m_Chunks[chunkPos].loadSuperBrick(glm::ivec3(l.superBrickIndex));
            }
        }
    }
}

void SceneManager::freeBuffers()
{
    m_ChunkBuffer.free();
    for (auto& chunk : m_Chunks) {
        chunk.second.free();
    }

    m_FeedbackBuffer.free();

    m_Staging.free();

    for (int i = 0; i < FRAMES_IN_FLIGHT; i++) {
        m_ToBeLoaded[i].free();
    }
}

void SceneManager::transformChange(VoxelChange change, WorldVoxelPosition& position, VoxelOp& op)
{
    auto pos = std::get<0>(change);
    glm::ivec3 chunkIndex = std::get<0>(pos);
    glm::ivec3 superBrickIndex = std::get<1>(pos);
    glm::ivec3 brickIndex = std::get<2>(pos);
    glm::ivec3 voxelIndex = std::get<3>(pos);

    op = std::get<1>(change);

    for (int i = 0; i < 3; i++) {
        bool notChanged = true;
        do {
            notChanged = true;
            if (voxelIndex[i] > BRICK_SIZE) {
                brickIndex[i] += 1;
                voxelIndex[i] -= BRICK_SIZE;
                notChanged = false;
            }
            if (voxelIndex[i] < 0) {
                brickIndex[i] -= 1;
                voxelIndex[i] += BRICK_SIZE;
                notChanged = false;
            }

            if (brickIndex[i] > SUPERBRICK_SIZE) {
                brickIndex[i] -= SUPERBRICK_SIZE;
                superBrickIndex[i] += 1;
                notChanged = false;
            }
            if (brickIndex[i] < 0) {
                brickIndex[i] += SUPERBRICK_SIZE;
                superBrickIndex[i] -= 1;
                notChanged = false;
            }

            if (superBrickIndex[i] > CHUNK_SIZE) {
                superBrickIndex[i] -= SUPERBRICK_SIZE;
                chunkIndex[i] += 1;
                notChanged = false;
            }
            if (superBrickIndex[i] < 0) {
                superBrickIndex[i] += SUPERBRICK_SIZE;
                chunkIndex[i] -= 1;
                notChanged = false;
            }
        } while (!notChanged);
    }

    position = { chunkIndex, superBrickIndex, brickIndex, voxelIndex };
}

void SceneManager::transformChanges(const std::vector<VoxelChange> changes,
    std::unordered_map<WorldBrickPosition, std::vector<std::pair<glm::ivec3, VoxelOp>>,
        tuple_3_hash>& groupedChanges)
{
    PROF_ZONE_SCOPED;
    for (const VoxelChange& change : changes) {
        WorldVoxelPosition pos;
        VoxelOp op;

        transformChange(change, pos, op);

        glm::ivec3 chunkIndex = std::get<0>(pos);

        bool shouldSkip = false;
        for (int i = 0; i < 3; i++) {
            if (chunkIndex[i] > 0 || chunkIndex[i] < 0) {
                shouldSkip = true;
                break;
            }
        }
        if (shouldSkip)
            continue;

        WorldBrickPosition pos2 = { std::get<0>(pos), std::get<1>(pos), std::get<2>(pos) };
        glm::ivec3 voxelIndex = std::get<3>(pos);

        groupedChanges[pos2].emplace_back(voxelIndex, op);
    }
}

void SceneManager::setVoxels(const std::vector<VoxelChange>& voxels, bool replace)
{
    PROF_ZONE_SCOPED;
    std::unordered_map<WorldBrickPosition, std::vector<std::pair<glm::ivec3, VoxelOp>>,
        tuple_3_hash>
        groupedChanges;

    transformChanges(voxels, groupedChanges);

    for (const auto& brickChanges : groupedChanges) {
        WorldBrickPosition position = brickChanges.first;

        // if (!m_Bricks.contains(brickIndex)) {
        //     std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock(m_QueuedChangesLock);
        //     for (const auto& change : brickChanges.second) {
        //         m_QueuedChanges[brickIndex][change.first] = { change.second, replace };
        //     }
        //
        //     continue;
        // }

        // std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock(m_BufferLock);
        // for (const auto& change : brickChanges.second) {
        //     if (std::holds_alternative<ERASE_OP>(change.second)) {
        //         m_Bricks.at(brickIndex).setAir(change.first);
        //     } else {
        //         m_Bricks.at(brickIndex)
        //             .setVoxel(change.first, std::get<PLACE_OP>(change.second), replace);
        //     }
        // }

        //     std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lock2(m_LoadedLock);
        //     m_ToBeLoaded.insert(brickIndex);
        //     if (m_GeneratedBricks.contains(brickIndex)) {
        //         uint16_t lookup = m_GeneratedBricks[brickIndex];
        //         m_GeneratedBricks.erase(brickIndex);
        //         m_FreeIndices.insert(lookup);
        //
        //         auto colourAllocation = m_AllocatedColourSizes[brickIndex];
        //         m_AvailableColourIndices.addInterval(
        //             colourAllocation.first, colourAllocation.first + colourAllocation.second -
        //             1);
        //         m_AllocatedColourSizes.erase(brickIndex);
        //         m_CurrentColourCount -= colourAllocation.second;
        //     }
    }
}

void SceneManager::reedbackFeedback()
{
    Feedback* data = (Feedback*)m_FeedbackBuffer.getAllocationInfo().pMappedData;
    m_Feedback = *data;
}

void SceneManager::createStaging(size_t size)
{
    if (m_Staging.getSize() >= size) {
        return;
    }

    m_Staging.free();

    m_Staging.create(m_Allocator, size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
}
