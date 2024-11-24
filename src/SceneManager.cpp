#include "SceneManager.hpp"

#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>

#include "ShaderModule.hpp"
#include "VkCheck.hpp"

SceneManager::SceneManager(uint32_t voxelDimension, PaletteManager* paletteManager)
    : m_Dimension(voxelDimension), m_PaletteManager(paletteManager)
{
}

SceneManager::SceneManager(SceneManager& other)
{
    m_Device = other.m_Device;
    m_Allocator = other.m_Allocator;
    m_Dimension = other.m_Dimension;
    m_PaletteManager = other.m_PaletteManager;
}

SceneManager SceneManager::operator=(const SceneManager& other)
{
    m_Allocator = other.m_Allocator;
    m_Dimension = other.m_Dimension;
    m_PaletteManager = other.m_PaletteManager;

    return *this;
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

void SceneManager::setDimensions(uint32_t dimension)
{
    m_Dimension = dimension;

    spdlog::info("Resized to {}x{}x{}", dimension, dimension, dimension);
    m_Voxels.assign(dimension * dimension * dimension, { .colourIndex = -1 });
}

void SceneManager::generateWorld()
{
    m_GenerationPushConstants.dimension = m_Dimension;
    m_GenerationPushConstants.size = 1.0f;
    m_GenerationPushConstants.targetBuffer = m_GeneratedVoxels.getDeviceAddress(m_Device);

    ImmediateSubmit::submit([&](VkCommandBuffer buffer) {
        vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_GenerationPipeline);

        vkCmdPushConstants(buffer, m_GenerationPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(VoxelGenerationPushConstants), &m_GenerationPushConstants);

        vkCmdDispatch(buffer, m_Dimension / 4, m_Dimension / 4, m_Dimension / 4);
    });

    m_GeneratedVoxels.copyToVector<Voxel>(m_Voxels);
}

uint32_t SceneManager::updateBuffers()
{
    freeBuffers();
    std::vector<SVONode> svo = serializeScene();
    size_t size = sizeof(SVONode) * svo.size();
    createBuffers(size);
    m_Staging.copyFromData_CPUOnly<SVONode>(svo);
    m_SVO.copyFromBuffer(m_Staging, size);
    return 0;
}

std::string toBits(uint8_t x)
{
    std::string returnStr = "";
    for (int i = 7; i >= 0; i--)
        returnStr += ((x >> i) & 0x1) ? "1" : "0";
    return returnStr;
}

std::vector<SVONode> SceneManager::serializeScene()
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
    size_t voxelSize = m_Voxels.size();
    for (size_t i = 0; i < voxelSize; ++i)
    {
        const Voxel& v = m_Voxels.at(i);
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
                 reversed.size(), m_Voxels.size(), bytes, bytes / 1024, bytes / (1024 * 1024),
                 after - before);
    spdlog::info("~{} bytes per voxel", (float)bytes / (float)m_Voxels.size());

    return reversed;
}

void SceneManager::createBuffers(size_t size)
{
    m_Staging.create(m_Allocator, size * sizeof(SVONode), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VMA_MEMORY_USAGE_CPU_TO_GPU);

    m_SVO.create(m_Allocator, size * sizeof(SVONode),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                 VMA_MEMORY_USAGE_GPU_ONLY);
}

void SceneManager::freeBuffers()
{
    m_Staging.free();
    m_SVO.free();
}

// https://www.forceflow.be/2013/10/07/morton-encodingdecoding-through-bit-interleaving-implementations/
int64_t SceneManager::splitBy3(uint32_t a)
{
    int64_t x = a;
    x &= 0x000003ff;                  // x = ---- ---- ---- ---- ---- --98 7654 3210
    x = (x ^ (x << 16)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x << 8)) & 0x0300f00f;  // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x << 4)) & 0x030c30c3;  // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x << 2)) & 0x09249249;  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    return x;
}

int64_t SceneManager::mortenEncode(glm::uvec3 position)
{
    return (splitBy3(position.x) | (splitBy3(position.y) << 2) | splitBy3(position.z) << 1);
}

// https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
uint32_t SceneManager::compactBy3(int64_t a)
{
    size_t x = a;
    x &= 0x09249249;                  // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    x = (x ^ (x >> 2)) & 0x030c30c3;  // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x >> 4)) & 0x0300f00f;  // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x >> 8)) & 0xff0000ff;  // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x >> 16)) & 0x000003ff; // x = ---- ---- ---- ---- ---- --98 7654 3210
    return x;
}

glm::uvec3 SceneManager::mortenDecode(int64_t code)
{
    glm::uvec3 position;
    position.x = compactBy3(code >> 0);
    position.y = compactBy3(code >> 2);
    position.z = compactBy3(code >> 1);

    return position;
}
