#include "SceneManager.hpp"

#include <glm/gtx/string_cast.hpp>
#include <iterator>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "Buffer.hpp"
#include "Constants.hpp"

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
    spdlog::info("Initliazing Scene Manager");

    m_SuperBrick.init(device, allocator, computeQueue);

    m_SuperBrick.addBrickToQueue({ 0, 0, 0 });

    m_MaxLoaded = 64;
    size_t loadedSize = sizeof(uint32_t) * 2 + sizeof(uint32_t) * m_MaxLoaded;
    for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        createStaging(loadedSize);

        m_ToBeLoaded[i].create(
            m_Allocator, sizeof(uint32_t) * 2 + sizeof(uint32_t) * m_MaxLoaded,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
    }

    m_SuperBrickBuffer.create(
        m_Allocator, sizeof(SuperBrickStruct),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT);
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
                if (ImGui::Button("Reset"))
                {
                    m_SuperBrick.reset();
                }

                ImGui::Text("Currently Generated: %ld", m_SuperBrick.getBricksSize());
                ImGui::Text("To be Generated: %ld", m_SuperBrick.getQueued());
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
    createStaging(sizeof(uint32_t) * 2);
    m_Staging.copyFromData_CPUOnly<uint32_t>(data);
    m_ToBeLoaded[currentFrame].copyFromBuffer(m_Staging, sizeof(uint32_t) * 2, 0);

    createStaging(sizeof(SuperBrickStruct));

    std::vector<SuperBrickStruct> temp = { m_SuperBrick.getStruct() };
    m_Staging.copyFromData_CPUOnly<SuperBrickStruct>(temp);
    m_SuperBrickBuffer.copyFromBuffer(m_Staging, sizeof(SuperBrickStruct));

    m_VoxelPushConstants.toBeLoaded = m_ToBeLoaded[currentFrame].getDeviceAddress(m_Device);
    m_VoxelPushConstants.superBrick = m_SuperBrickBuffer.getDeviceAddress(m_Device);

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
    PROF_ZONE_SCOPED;
    if (m_PauseRegeneration) return;

    const uint32_t* data =
        (const uint32_t*)m_ToBeLoaded[currentFrame].getAllocationInfo().pMappedData;

    uint32_t length = std::min((uint32_t)data[0], data[1] + 2);
    if (data[1] != 0)
    {
        for (uint32_t i = 2; i < length; i++)
        {
            uint32_t index = data[i];
            m_SuperBrick.addBrickToQueue(index);
        }
    }
}

void SceneManager::freeBuffers()
{
    m_SuperBrickBuffer.free();
    m_SuperBrick.free();

    m_Staging.free();

    for (int i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        m_ToBeLoaded[i].free();
    }
}

void SceneManager::createStaging(size_t size)
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
