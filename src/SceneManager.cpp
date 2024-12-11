#include "SceneManager.hpp"

#include <glm/gtx/string_cast.hpp>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "tracy/Tracy.hpp"

#include "imgui.h"
#include <GLFW/glfw3.h>

// #include "ChunkGenerator.hpp"

SceneManager::SceneManager(PaletteManager* paletteManager, Camera* camera)
    : m_Dimension(1 << 8), m_PaletteManager(paletteManager), m_Camera(camera)
{
    m_VoxelPushConstants.maxIterations = 1024;
    m_VoxelPushConstants.maxDepthShown = std::log2(m_Dimension);
    m_VoxelPushConstants.maxHeatShown = m_VoxelPushConstants.maxIterations;
    m_VoxelPushConstants.lod = m_VoxelPushConstants.maxDepthShown;

    m_VoxelPushConstants.flags = 0;
    m_VoxelPushConstants.flags |= PCF_SHOW_HEAT_MAP;
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
    if (m_Initialized) return;

    m_Device = device;
    m_Allocator = allocator;

    // ChunkGenerator::init(m_Dimension, m_Allocator, m_Device, computeQueue, &m_Chunks);

    m_Initialized = true;
    spdlog::info("Created Background Pipeline and Pipeline Layout");

    m_BrickGridBuffer.create(m_Allocator, sizeof(BrickGrid),
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VMA_MEMORY_USAGE_GPU_ONLY);

    m_BricksBuffer.create(m_Allocator, sizeof(Brick),
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VMA_MEMORY_USAGE_GPU_ONLY);

    // 4 Corners
    Brick testBrick{};
    testBrick.solidMask[0] |= (1 << 0) | (1 << 7) | (1l << 56) | (1l << 63);
    testBrick.solidMask[7] |= (1 << 0) | (1 << 7) | (1l << 56) | (1l << 63);
    {
        std::vector<Brick> temp{ testBrick };
        m_Staging.create(m_Allocator, sizeof(Brick), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_Staging.copyFromData_CPUOnly<Brick>(temp);
        m_BricksBuffer.copyFromBuffer(m_Staging, sizeof(Brick));

        m_BrickGrid.bricks = m_BricksBuffer.getDeviceAddress(m_Device);
    }

    for (int i = 0; i < 16 * 16 * 16; i++)
    {
        // Full grid pointing to above grid
        m_BrickGrid.data[i] = 0b00000000000000000000000000000001;
    }

    // m_BrickGrid.data[0] = 0b00000000000000000000000000000001;
    // m_BrickGrid.data[1] = 0b00000000000000000000000000000001;
    // m_BrickGrid.data[2] = 0b00000000000000000000000000000001;
    // m_BrickGrid.data[3] = 0b00000000000000000000000000000001;
    //
    {
        m_Staging.free();
        m_Staging.create(m_Allocator, sizeof(BrickGrid), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU);
        std::vector<BrickGrid> temp{ m_BrickGrid };
        m_Staging.copyFromData_CPUOnly<BrickGrid>(temp);
        m_BrickGridBuffer.copyFromBuffer(m_Staging, sizeof(BrickGrid));
    }

    // checkChunks();

    // m_ChunkGeneration = std::thread([&]() { ChunkGenerator::getInstance().generationLoop(); });
}

void SceneManager::freeResources()
{
    if (!m_Initialized) return;
    // ChunkGenerator::getInstance().stopRunning();
    // m_ChunkGeneration.join();

    // ChunkGenerator::getInstance().free();
    freeBuffers();
    m_Initialized = false;
}

void SceneManager::receive(const Event* event)
{
    static float currentT = 0.0;

    static float previousState = m_AnimateCutoff;
    // static float previousCutoff = m_GenerationPushConstants.cutoff;

    switch (event->getType())
    {
    case EventType::GameUpdate:
        {
            const GameUpdate* gu = static_cast<const GameUpdate*>(event);

            // checkChunks();

            if (!previousState && m_AnimateCutoff) // Started
            {
                // previousCutoff = m_GenerationPushConstants.cutoff;
            }
            else if (previousState && !m_AnimateCutoff) // Ended
            {
                // m_GenerationPushConstants.cutoff = previousCutoff;
                // generateWorld();
            }

            if (m_AnimateCutoff)
            {
                currentT += gu->frameDelta / 10.0f;
                // m_GenerationPushConstants.cutoff = -1.0f + (2.f * currentT);
                // generateWorld();
            }
            else
                currentT = 0.f;

            previousState = m_AnimateCutoff;

            if (currentT >= 1.0f) m_AnimateCutoff = false;

            break;
        }
    case EventType::ImGuiRender:
        {
            if (ImGui::Begin("Scene"))
            {
                enum SceneType { WorldGeneration = 0, ModelLoading = 1 };
                const char* names[] = { "World Generation", "Load Model" };

                const int modelInputSize = 100;
                static char currentModel[modelInputSize] = "res/models/doom.vox";

                static SceneType currentGeneration = WorldGeneration;

                static int powerOf2 = std::log2(m_Dimension);

                ImGui::Text("Current Scene");

                if (ImGui::BeginCombo("##CurrentScene", names[currentGeneration], 0))
                {
                    bool hasChanged = false;
                    for (size_t i = 0; i < 2; i++)
                    {
                        bool isSelected = (i == currentGeneration);
                        if (ImGui::Selectable(names[i], isSelected))
                        {
                            currentGeneration = (SceneType)i;
                            hasChanged = true;
                        }
                    }

                    if (hasChanged)
                    {
                        switch (currentGeneration)
                        {
                        case WorldGeneration:
                            // setDimensions(1 << powerOf2);
                            // generateWorld();
                            break;
                        case ModelLoading:
                            {
                                // VoxLoader loader(&m_Chunk, m_PaletteManager);
                                // loader.loadModel(currentModel);
                                break;
                            }
                        }

                        // m_HasUpdated = true;
                    }

                    ImGui::EndCombo();
                }

                ImGui::Text("Max Iterations");
                int maxIterations = m_VoxelPushConstants.maxIterations;
                if (ImGui::SliderInt("##MaxIterations", &maxIterations, 1, 2048))
                {
                    m_VoxelPushConstants.maxIterations = maxIterations;
                }

                bool showHeatMap = (m_VoxelPushConstants.flags & PCF_SHOW_HEAT_MAP) != 0;
                if (ImGui::Checkbox("Show Heat Map", &showHeatMap))
                {
                    m_VoxelPushConstants.flags &= ~(PCF_SHOW_HEAT_MAP); // Unset flag
                    m_VoxelPushConstants.flags |=
                        (showHeatMap * PCF_SHOW_HEAT_MAP); // Set with correct value
                }

                if (showHeatMap)
                {
                    ImGui::Text("Max Heat Shown");
                    int maxHeat = m_VoxelPushConstants.maxHeatShown;
                    if (ImGui::SliderInt("##MaxHeat", &maxHeat, 1, 2048))
                        m_VoxelPushConstants.maxHeatShown = maxHeat;
                }
                else
                {
                    ImGui::Text("Max Depth Shown");
                    int maxDepth = m_VoxelPushConstants.maxDepthShown;
                    if (ImGui::SliderInt("##MaxDepth", &maxDepth, 1, std::log2(m_Dimension)))
                        m_VoxelPushConstants.maxDepthShown = maxDepth;
                }

                ImGui::Text("Max LOD");
                int LOD = m_VoxelPushConstants.lod;
                if (ImGui::SliderInt("##MaxLOD", &LOD, 0, std::log2(m_Dimension)))
                    m_VoxelPushConstants.lod = LOD;

                ImGui::Checkbox("Pause regeneration of Chunks", &m_PauseRegeneration);

                // ChunkGenerator& chunkGenerator = ChunkGenerator::getInstance();
                // ImGui::Text("Generation Queue: %ld", chunkGenerator.getGenerationQueueSize());
                // ImGui::Text("Removal Queue: %ld", chunkGenerator.getRemovalQueueSize());

                /*
                switch (currentGeneration)
                {
                case WorldGeneration:
                    {
                        ImGui::Text("Seed");
                        int seed = getSeed();
                        if (ImGui::SliderInt("##Seed", &seed, 0, 1000000))
                        {
                            // setSeed(seed);
                            // generateWorld();
                        }

                        ImGui::Text("Cutoff");
                        if (ImGui::SliderFloat("##Cutoff", &m_GenerationPushConstants.cutoff, -1.0,
                                               1.0))
                        {
                            // generateWorld();
                        }

                        ImGui::Checkbox("Animate Cutoff", &m_AnimateCutoff);

                        ImGui::Text("10th percentile");
                        if (ImGui::SliderInt("##p10", &m_GenerationPushConstants.p10, 0, 255))
                        {
                            generateWorld();
                        }

                        ImGui::Text("50th percentile");
                        if (ImGui::SliderInt("##p50", &m_GenerationPushConstants.p50, 0, 255))
                        {
                            generateWorld();
                        }

                        ImGui::Text("100th percentile");
                        if (ImGui::SliderInt("##p100", &m_GenerationPushConstants.p100, 0, 255))
                        {
                            generateWorld();
                        }

                        ImGui::Text("Chunk Size");
                        if (ImGui::Button("Regenerate World"))
                        {
                            generateWorld();
                        }
                        break;
                    }
                case ModelLoading:
                    {
                        ImGui::Text("Mode to load");
                        ImGui::InputText("##Model", currentModel, modelInputSize);

                        if (ImGui::Button("Load Model"))
                        {
                            // VoxLoader loader(&m_Chunk, m_PaletteManager);
                            // if (loader.loadModel(currentModel))
                            // {
                            //     m_HasUpdated = true;
                            // }
                        }
                    }
                }
                */
            }
            ImGui::End();
            break;
        }
    default:
        break;
    }
}

VoxelPushConstants& SceneManager::getVoxelPushConstants()
{
    PROF_ZONE_SCOPED;

    if (m_PauseRegeneration) return m_VoxelPushConstants;

    // m_VoxelPushConstants.dimension = m_Dimension;
    m_VoxelPushConstants.size = Voxel::VOXEL_SIZE;

    // createBufferChunks();

    // std::vector<ChunkData> chunkData;
    // for (auto& chunkPair : m_Chunks.chunks)
    // {
    //     if (!chunkPair.second.isGenerated() || chunkPair.second.getSVOBuffer() == VK_NULL_HANDLE)
    //         continue;
    //
    //     chunkData.push_back({ .chunkPosition = glm::vec4(chunkPair.second.getPosition(), 0),
    //                           .chunkData = chunkPair.second.getBufferAddress(m_Device) });
    // }
    // m_VoxelPushConstants.chunkCount = chunkData.size();

    // if (chunkData.size() > 0)
    // {
    //     m_Staging.copyFromData_CPUOnly<ChunkData>(chunkData);
    //     m_ChunkDataAddress.copyFromBuffer(m_Staging, chunkData.size() * sizeof(ChunkData));
    //
    //     m_VoxelPushConstants.chunks = m_ChunkDataAddress.getDeviceAddress(m_Device);
    // }
    // else
    // {
    //     m_VoxelPushConstants.chunks = 0;
    // }

    m_VoxelPushConstants.brickGrid = m_BrickGridBuffer.getDeviceAddress(m_Device);

    return m_VoxelPushConstants;
}

glm::ivec3 SceneManager::worldToChunkPos(glm::vec3 position)
{
    float chunkSize = m_Dimension * Voxel::VOXEL_SIZE;

    glm::ivec3 chunkIndex = glm::floor(position / chunkSize);

    return chunkIndex;
}

void SceneManager::checkChunks()
{
    ZoneScoped;
    if (m_PauseRegeneration) return;

    glm::ivec3 chunkPosition = worldToChunkPos(m_Camera->getPosition());
    glm::vec3 newPos = { chunkPosition.x, chunkPosition.y + 1, chunkPosition.z };
    glm::vec3 oldPos = { m_CurrentChunk.x, m_CurrentChunk.y, m_CurrentChunk.z };

    if (newPos == oldPos) return;
    spdlog::info("Current camera chunk position: {}", glm::to_string(newPos));

    glm::ivec3 previousChunk = m_CurrentChunk;
    m_CurrentChunk = newPos;

    // std::unordered_set<glm::ivec3> toRemove;
    // std::unordered_set<glm::ivec3> kept;
    // for (auto& pair : m_Chunks.chunks)
    // {
    //     glm::ivec3 currentPos = pair.first;
    //     glm::ivec3 diff = glm::abs(currentPos - m_CurrentChunk);
    //
    //     if (diff.x > m_ChunkRange || diff.y > m_ChunkRange || diff.z > m_ChunkRange)
    //     {
    //         toRemove.emplace(pair.first);
    //     }
    //     else
    //     {
    //         kept.emplace(pair.first);
    //     }
    // }

    {
        // for (glm::ivec3 pos : toRemove)
        // {
        //     ChunkGenerator::getInstance().removeChunk(pos);
        // }

        // std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(m_Chunks.mutex);
        // for (const glm::ivec3& pos : toRemove)
        // {
        //     m_Chunks.chunks.at(pos).getSVOBuffer()->free();
        //     m_Chunks.chunks.erase(pos);
        // }

        // for (int x = -m_ChunkRange; x <= m_ChunkRange; x++)
        // {
        //     for (int y = -1; y <= 1; y++)
        //     {
        //         for (int z = -m_ChunkRange; z <= m_ChunkRange; z++)
        //         {
        //             glm::ivec3 pos = { newPos.x + x, newPos.y + y, newPos.z + z };
        //
        //             if (!kept.contains(pos))
        //             {
        //                 m_Chunks.chunks.emplace(pos, Chunk{ pos, m_Dimension });
        //                 ChunkGenerator::getInstance().addChunkToQueue(pos);
        //             }
        //         }
        //     }
        // }
    }
}

// void SceneManager::createBufferChunks()
// {
//     PROF_ZONE_SCOPED;
//     size_t size = m_Chunks.chunks.size() * sizeof(ChunkData);
//
//     if (m_Staging.getSize() < size)
//     {
//         m_Staging.free();
//         m_Staging.create(m_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//                          VMA_MEMORY_USAGE_CPU_TO_GPU);
//     }

// if (m_ChunkDataAddress.getSize() < size)
// {
//     m_ChunkDataAddress.free();
//     m_ChunkDataAddress.create(m_Allocator, size,
//                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
//                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
//                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
//                               VMA_MEMORY_USAGE_GPU_ONLY);
// }
// }

void SceneManager::freeBuffers()
{
    // std::lock_guard<PROF_LOCKABLE_BASE(std::mutex)> lk(m_Chunks.mutex);

    m_Staging.free();

    // for (auto& chunkPair : m_Chunks.chunks)
    // {
    //     chunkPair.second.getSVOBuffer()->free();
    // }

    m_BricksBuffer.free();
    m_BrickGridBuffer.free();
    // m_ChunkDataAddress.free();
}
