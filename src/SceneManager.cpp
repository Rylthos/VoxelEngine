#include "SceneManager.hpp"

#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "imgui.h"
#include <GLFW/glfw3.h>

#include "ShaderModule.hpp"
#include "VkCheck.hpp"
#include "VoxLoader.hpp"

SceneManager::SceneManager(PaletteManager* paletteManager)
    : m_Dimension(1 << 7), m_PaletteManager(paletteManager)
{
    m_VoxelPushConstants.maxIterations = 1024;
    m_VoxelPushConstants.maxDepthShown = std::log2(m_Dimension);
    m_VoxelPushConstants.maxHeatShown = m_VoxelPushConstants.maxIterations;
    m_VoxelPushConstants.lod = m_VoxelPushConstants.maxDepthShown;

    m_VoxelPushConstants.flags = 0;
    m_VoxelPushConstants.flags |= PCF_SHOW_HEAT_MAP;

    m_GenerationPushConstants.cutoff = 0.0;
    m_GenerationPushConstants.p10 = 10;
    m_GenerationPushConstants.p50 = 50;
    m_GenerationPushConstants.p100 = 100;
}

SceneManager::SceneManager(SceneManager& other)
{
    m_Device = other.m_Device;
    m_Allocator = other.m_Allocator;
    m_Dimension = other.m_Dimension;
    m_PaletteManager = other.m_PaletteManager;
    m_VoxelPushConstants = other.m_VoxelPushConstants;
    m_GenerationPushConstants = other.m_GenerationPushConstants;
}

SceneManager SceneManager::operator=(const SceneManager& other)
{
    m_Device = other.m_Device;
    m_Allocator = other.m_Allocator;
    m_Dimension = other.m_Dimension;
    m_PaletteManager = other.m_PaletteManager;
    m_VoxelPushConstants = other.m_VoxelPushConstants;
    m_GenerationPushConstants = other.m_GenerationPushConstants;

    return *this;
}

void SceneManager::receive(const Event* event)
{
    static float currentT = 0.0;

    static float previousState = m_AnimateCutoff;
    static float previousCutoff = m_GenerationPushConstants.cutoff;

    switch (event->getType())
    {
    case EventType::GameUpdate:
        {
            const GameUpdate* gu = static_cast<const GameUpdate*>(event);

            if (!previousState && m_AnimateCutoff) // Started
            {
                previousCutoff = m_GenerationPushConstants.cutoff;
            }
            else if (previousState && !m_AnimateCutoff) // Ended
            {
                m_GenerationPushConstants.cutoff = previousCutoff;
                generateWorld();
            }

            if (m_AnimateCutoff)
            {
                currentT += gu->frameDelta / 10.0f;
                m_GenerationPushConstants.cutoff = -1.0f + (2.f * currentT);
                generateWorld();
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
                            generateWorld();
                            break;
                        case ModelLoading:
                            {
                                // VoxLoader loader(&m_Chunk, m_PaletteManager);
                                // loader.loadModel(currentModel);
                                break;
                            }
                        }

                        m_HasUpdated = true;
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
                if (ImGui::SliderInt("##MaxLOD", &LOD, 1, std::log2(m_Dimension)))
                    m_VoxelPushConstants.lod = LOD;

                switch (currentGeneration)
                {
                case WorldGeneration:
                    {
                        ImGui::Text("Seed");
                        int seed = getSeed();
                        if (ImGui::SliderInt("##Seed", &seed, 0, 1000000))
                        {
                            setSeed(seed);
                            generateWorld();
                        }

                        ImGui::Text("Cutoff");
                        if (ImGui::SliderFloat("##Cutoff", &m_GenerationPushConstants.cutoff, -1.0,
                                               1.0))
                        {
                            generateWorld();
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
            }
            ImGui::End();
            break;
        }
    default:
        break;
    }
}

void SceneManager::initResources(VkDevice device, VmaAllocator allocator)
{
    if (m_Initialized) return;

    m_GenerationPushConstants.seed = 0;

    m_Device = device;
    m_Allocator = allocator;
    m_GeneratedVoxels.create(m_Allocator, m_Dimension * m_Dimension * m_Dimension * sizeof(Voxel),
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             VMA_MEMORY_USAGE_GPU_TO_CPU);

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(VoxelGenerationPushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo computeLayoutCI{};
    computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayoutCI.pNext = nullptr;
    computeLayoutCI.setLayoutCount = 0;
    computeLayoutCI.pSetLayouts = nullptr;
    computeLayoutCI.pushConstantRangeCount = 1;
    computeLayoutCI.pPushConstantRanges = &pushConstant;

    VK_CHECK(
        vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr, &m_GenerationPipelineLayout));

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

    m_Initialized = true;
    spdlog::info("Created Background Pipeline and Pipeline Layout");

    // m_Chunks.reserve(m_ChunkCount);

    m_Chunks.emplace_back(glm::ivec3{ 0, 0, 0 }, m_Dimension);
    m_Chunks.emplace_back(glm::ivec3{ 2, 0, 0 }, m_Dimension);
    m_Chunks.emplace_back(glm::ivec3{ 0, 0, 2 }, m_Dimension);
    m_Chunks.emplace_back(glm::ivec3{ 2, 0, 2 }, m_Dimension);
    m_Chunks.emplace_back(glm::ivec3{ 0, 2, 0 }, m_Dimension);
    m_Chunks.emplace_back(glm::ivec3{ 2, 2, 0 }, m_Dimension);
    m_Chunks.emplace_back(glm::ivec3{ 0, 2, 2 }, m_Dimension);
    m_Chunks.emplace_back(glm::ivec3{ 2, 2, 2 }, m_Dimension);
    // m_Chunks.emplace_back(glm::ivec3{ 1, 0, 1 }, m_Dimension);
}

void SceneManager::freeResources()
{
    if (!m_Initialized) return;

    freeBuffers();
    vkDestroyPipeline(m_Device, m_GenerationPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_GenerationPipelineLayout, nullptr);
    m_GeneratedVoxels.free();
    m_Initialized = false;
}

VoxelPushConstants& SceneManager::getVoxelPushConstants()
{
    m_VoxelPushConstants.dimension = m_Dimension;
    m_VoxelPushConstants.size = 1.0f;

    m_VoxelPushConstants.chunkCount = m_Chunks.size();

    createBufferChunks();

    std::vector<ChunkData> chunkData;
    for (Chunk& chunk : m_Chunks)
    {
        chunkData.push_back({ .chunkPosition = glm::vec4(chunk.getPosition(), 0),
                              .chunkData = chunk.getBufferAddress(m_Device) });
    }

    m_Staging.copyFromData_CPUOnly<ChunkData>(chunkData);
    m_ChunkDataAddress.copyFromBuffer(m_Staging, chunkData.size() * sizeof(ChunkData));

    m_VoxelPushConstants.chunks = m_ChunkDataAddress.getDeviceAddress(m_Device);

    return m_VoxelPushConstants;
}

void SceneManager::generateWorld()
{
    m_PaletteManager->defaultPalette();

    m_GenerationPushConstants.dimension = m_Dimension;
    m_GenerationPushConstants.size = 1.0f;
    m_GenerationPushConstants.targetBuffer = m_GeneratedVoxels.getDeviceAddress(m_Device);

    for (Chunk& chunk : m_Chunks)
    {
        m_GenerationPushConstants.origin = glm::ivec4(chunk.getPosition(), 0);
        ImmediateSubmit::submit([&](VkCommandBuffer buffer) {
            vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_GenerationPipeline);

            vkCmdPushConstants(buffer, m_GenerationPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(VoxelGenerationPushConstants), &m_GenerationPushConstants);

            vkCmdDispatch(buffer, m_Dimension / 4, m_Dimension / 4, m_Dimension / 4);
        });
        m_GeneratedVoxels.copyToVector<Voxel>(chunk.getVoxels());
    }

    m_HasUpdated = true;
}

void SceneManager::updateBuffers()
{
    freeBuffers();

    for (Chunk& chunk : m_Chunks)
    {
        std::vector<SVONode> svo = serializeChunk(chunk);
        size_t size = sizeof(SVONode) * svo.size();
        createBuffer(chunk, size);
        m_Staging.copyFromData_CPUOnly<SVONode>(svo);
        chunk.getSVOBuffer()->copyFromBuffer(m_Staging, size);
    }

    m_VoxelPushConstants.initialParent = 0;
}

std::string toBits(uint8_t x)
{
    std::string returnStr = "";
    for (int i = 7; i >= 0; i--)
        returnStr += ((x >> i) & 0x1) ? "1" : "0";
    return returnStr;
}

std::vector<SVONode> SceneManager::serializeChunk(Chunk& chunk)
{
    size_t maxDepth = std::log2(m_Dimension);

    double before = glfwGetTime();

    std::vector<std::vector<SVONode>> queues;

    std::vector<SVONode> finalNodes;

    queues.resize(maxDepth + 1);
    for (size_t i = 0; i < queues.size(); ++i)
    {
        queues[i].reserve(8);
    }

    int depth = maxDepth;
    std::vector<Voxel>& voxels = chunk.getVoxels();
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
    spdlog::trace("Finished Parsing Nodes");

    std::vector<SVONode> reversed;
    reversed.reserve(finalNodes.size());
    for (auto itr = finalNodes.rbegin(); itr != finalNodes.rend(); itr++)
    {
        reversed.push_back(*itr);
    }
    spdlog::trace("Finished Reversing Nodes");

    double after = glfwGetTime();

    size_t bytes = reversed.size() * sizeof(SVONode);
    spdlog::info("Generated {} nodes ({} Voxels) ({} B) ({} KiB) ({} MiB). Took {}s",
                 reversed.size(), chunk.getVoxels().size(), bytes, bytes / 1024,
                 bytes / (1024 * 1024), after - before);
    spdlog::info("~{} bytes per voxel", (float)bytes / (float)chunk.getVoxels().size());

    return reversed;
}

void SceneManager::createBuffer(Chunk& chunk, size_t count)
{
    size_t size = count * sizeof(SVONode);
    if (m_Staging.getSize() < size)
    {
        m_Staging.free();

        m_Staging.create(m_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    chunk.getSVOBuffer()->create(m_Allocator, size,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 VMA_MEMORY_USAGE_GPU_ONLY);
}

void SceneManager::createBufferChunks()
{
    if (m_ChunkDataAddress.getSize() != 0) return;

    size_t size = m_Chunks.size() * sizeof(ChunkData);

    if (m_Staging.getSize() < size)
    {
        m_Staging.free();

        m_Staging.create(m_Allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    m_ChunkDataAddress.create(m_Allocator, size,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                              VMA_MEMORY_USAGE_GPU_ONLY); //
}

void SceneManager::freeBuffers()
{
    m_Staging.free();

    for (Chunk& chunk : m_Chunks)
    {
        chunk.getSVOBuffer()->free();
    }

    m_ChunkDataAddress.free();
}
