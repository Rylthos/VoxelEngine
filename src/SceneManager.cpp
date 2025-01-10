#include "SceneManager.hpp"

#include <glm/gtx/string_cast.hpp>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "Buffer.hpp"
#include "Constants.hpp"
#include "tracy/Tracy.hpp"

#include "imgui.h"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

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

    m_Initialized = true;
    spdlog::info("Created Background Pipeline and Pipeline Layout");

    for (size_t i = 0; i < m_BrickGridBuffer.size(); i++)
    {
        m_BrickGridBuffer[i].create(m_Allocator, sizeof(SuperBrick),
                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VMA_MEMORY_USAGE_GPU_ONLY);
    }

    m_BricksBuffer.create(m_Allocator, sizeof(Brick),
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VMA_MEMORY_USAGE_GPU_ONLY);

    m_Colour.create(m_Allocator, sizeof(glm::vec4) * 8,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_MEMORY_USAGE_GPU_ONLY);

    m_ColourStaging.create(
        m_Allocator, m_Colour.getSize(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

    std::vector<glm::vec4> colours = {
        { 0., 0., 0., 1. },
        { 1., 0., 0., 1. },
        { 0., 1., 0., 1. },
        { 0., 0., 1., 1. },
        { 1., 1., 0., 1. },
        { 1., 0., 1., 1. },
        { 0., 1., 1., 1. },
        { 1., 1., 1., 1. },
    };

    m_ColourStaging.copyFromData_CPUOnly<glm::vec4>(colours);
    m_Colour.copyFromBuffer(m_ColourStaging, colours.size() * sizeof(glm::vec4));

    // 4 Corners
    Brick testBrick{};
    testBrick.solidMask[0] |= (1 << 0) | (1 << 7) | (1l << 56) | (1l << 63);
    testBrick.solidMask[7] |= (1 << 0) | (1 << 7) | (1l << 56) | (1l << 63);

    testBrick.lodR = 255;
    testBrick.lodG = 255;
    testBrick.lodB = 255;

    {
        std::vector<Brick> temp{ testBrick };
        m_BricksStaging.create(m_Allocator, sizeof(Brick), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_AUTO,
                               VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                   VMA_ALLOCATION_CREATE_MAPPED_BIT);
        m_BricksStaging.copyFromData_CPUOnly<Brick>(temp);
        m_BricksBuffer.copyFromBuffer(m_BricksStaging, sizeof(Brick));

        m_SuperBrick.bricks = m_BricksBuffer.getDeviceAddress(m_Device);
        m_SuperBrick.colour = m_Colour.getDeviceAddress(m_Device);
    }

    for (int i = 0; i < 16 * 16 * 16; i++)
    {
        m_SuperBrick.data[i] = 0;
    }

    {
        std::vector<SuperBrick> temp{ m_SuperBrick };

        for (size_t i = 0; i < m_BrickGridBuffer.size(); i++)
        {
            m_BrickGridStaging[i].create(m_Allocator, sizeof(SuperBrick),
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                                         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                             VMA_ALLOCATION_CREATE_MAPPED_BIT);
            m_BrickGridStaging[i].copyFromData_CPUOnly<SuperBrick>(temp);
            m_BrickGridBuffer[i].copyFromBuffer(m_BrickGridStaging[i], sizeof(SuperBrick));
        }
    }

    m_MaxLoaded = 64;
    size_t loadedSize = sizeof(uint32_t) * 2 + sizeof(uint32_t) * m_MaxLoaded;
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        m_ToBeLoadedStaging[i].create(m_Allocator, sizeof(loadedSize),
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                          VMA_ALLOCATION_CREATE_MAPPED_BIT);

        m_ToBeLoaded[i].create(
            m_Allocator, sizeof(uint32_t) * 2 + sizeof(uint32_t) * m_MaxLoaded,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    }
}

void SceneManager::freeResources()
{
    if (!m_Initialized) return;

    freeBuffers();
    m_Initialized = false;
}

void SceneManager::receive(const Event* event)
{
    static float currentT = 0.0;

    static float previousState = m_AnimateCutoff;

    switch (event->getType())
    {
    case EventType::GameUpdate:
        {
            const GameUpdate* gu = static_cast<const GameUpdate*>(event);

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
                            break;
                        case ModelLoading:
                            {
                                break;
                            }
                        }
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

    if (m_PauseRegeneration) return m_VoxelPushConstants;

    checkChunks(currentFrame);

    m_VoxelPushConstants.size = Voxel::VOXEL_SIZE;

    std::vector<uint32_t> data = { m_MaxLoaded, 0 };
    m_ToBeLoadedStaging[currentFrame].copyFromData_CPUOnly<uint32_t>(data);
    m_ToBeLoaded[currentFrame].copyFromBuffer(m_ToBeLoadedStaging[currentFrame],
                                              sizeof(uint32_t) * 2, 0);

    m_VoxelPushConstants.toBeLoaded = m_ToBeLoaded[currentFrame].getDeviceAddress(m_Device);
    m_VoxelPushConstants.brickGrid = m_BrickGridBuffer[currentFrame].getDeviceAddress(m_Device);

    return m_VoxelPushConstants;
}

glm::ivec3 SceneManager::worldToChunkPos(glm::vec3 position)
{
    float chunkSize = m_Dimension * Voxel::VOXEL_SIZE;

    glm::ivec3 chunkIndex = glm::floor(position / chunkSize);

    return chunkIndex;
}

void SceneManager::checkChunks(uint32_t currentFrame)
{
    ZoneScoped;
    if (m_PauseRegeneration) return;

    const uint32_t* data =
        (const uint32_t*)m_ToBeLoaded[currentFrame].getAllocationInfo().pMappedData;

    uint32_t length = std::min((uint32_t)data[0], data[1] + 2);
    if (data[1] != 0)
    {
        for (uint32_t i = 2; i < length; i++)
        {
            uint32_t index = data[i];
            if (index < m_SuperBrick.data.size())
            {
                m_SuperBrick.data[index] = 1;
            }
        }

        {
            std::vector<SuperBrick> temp{ m_SuperBrick };
            m_BrickGridStaging[currentFrame].copyFromData_CPUOnly<SuperBrick>(temp);
            m_BrickGridBuffer[currentFrame].copyFromBuffer(m_BrickGridStaging[currentFrame],
                                                           sizeof(SuperBrick));
        }
    }
}

void SceneManager::freeBuffers()
{
    m_BricksBuffer.free();
    m_BricksStaging.free();

    for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        m_BrickGridStaging[i].free();
        m_BrickGridBuffer[i].free();
        m_ToBeLoadedStaging[i].free();
        m_ToBeLoaded[i].free();
    }

    m_Colour.free();
    m_ColourStaging.free();
}
