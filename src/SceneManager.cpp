#include "SceneManager.hpp"

#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>

std::string stringOfScene(const Scene& scene)
{
    switch (scene)
    {
    case Scene::SQUARE:
        return "Square";
    case Scene::HOLED_SQUARE:
        return "Holed Square";
    case Scene::RANDOM_OBJECTS:
        return "Random Objects";
    case Scene::SPHERE:
        return "Sphere";
    case Scene::TORUS:
        return "Torus";

    default:
        return "ERROR";
    }
}

SceneManager::SceneManager(uint32_t voxelDimension, PaletteManager* paletteManager)
    : m_Dimension(voxelDimension), m_PaletteManager(paletteManager)
{
    m_Voxels.resize(voxelDimension * voxelDimension * voxelDimension);
}

SceneManager::SceneManager(SceneManager& other)
{
    m_Allocator = other.m_Allocator;
    m_Dimension = other.m_Dimension;
    m_PaletteManager = other.m_PaletteManager;

    m_Voxels.resize(m_Dimension * m_Dimension * m_Dimension);

    loadScene(other.m_CurrentScene);
}

SceneManager SceneManager::operator=(const SceneManager& other)
{
    m_Allocator = other.m_Allocator;
    m_Dimension = other.m_Dimension;
    m_PaletteManager = other.m_PaletteManager;

    m_Voxels.resize(m_Dimension * m_Dimension * m_Dimension);

    loadScene(other.m_CurrentScene);

    return *this;
}

void SceneManager::initResources(VmaAllocator allocator) { m_Allocator = allocator; }

void SceneManager::freeResources() { freeBuffers(); }

void SceneManager::loadScene(Scene newScene)
{
    if (m_CurrentScene == newScene) return;

    if (newScene == Scene::END) return; // Don't change anything

    m_CurrentScene = newScene;

    spdlog::info("Loading Scene: {}", stringOfScene(m_CurrentScene));

    switch (m_CurrentScene)
    {
    case Scene::SQUARE:
        squareScene();
        break;
    case Scene::HOLED_SQUARE:
        holedSquareScene();
        break;
    case Scene::RANDOM_OBJECTS:
        randomObjectsScene();
        break;
    case Scene::SPHERE:
        sphereScene();
        break;
    case Scene::TORUS:
        torusScene();
        break;
    default:
        throw std::runtime_error("Invalid Scene");
    }
}

void SceneManager::updateBuffers()
{
    freeBuffers();
    std::vector<SVONode> svo = serializeScene();
    size_t size = sizeof(SVONode) * svo.size();
    createBuffers(size);
    m_Staging.copyFromData_CPUOnly<SVONode>(svo);
    m_SVO.copyFromBuffer(m_Staging, size);
}

Voxel SceneManager::getVoxel(glm::uvec3 position)
{
    size_t mortenCode = mortenEncode(position);
    assert(mortenCode < m_Voxels.size() && "Position exceeds array size");

    return m_Voxels[mortenCode];
}

void SceneManager::setVoxel(glm::uvec3 position, bool solid, uint8_t materialIndex)
{
    size_t mortenCode = mortenEncode(position);
    assert(mortenCode < m_Voxels.size() && "Position exceeds array size");

    m_Voxels[mortenCode] = { .isSolid = solid, .colourIndex = materialIndex };
}

void SceneManager::setVoxel(glm::uvec3 position, Voxel voxel)
{
    size_t mortenCode = mortenEncode(position);
    assert(mortenCode < m_Voxels.size() && "Position exceeds array size");

    m_Voxels[mortenCode] = voxel;
}

std::vector<SVONode> SceneManager::serializeScene()
{
    size_t maxDepth = std::log2(m_Dimension);

    double before = glfwGetTime();

    std::vector<std::vector<SVOConstructionNode>> queues;

    std::vector<SVOConstructionNode> parsedNodes;

    queues.resize(maxDepth + 1);
    for (size_t i = 0; i < queues.size(); i++)
    {
        queues[i].reserve(8);
    }

    int depth = maxDepth;
    for (size_t i = 0; i < m_Voxels.size(); i++)
    {
        Voxel v = m_Voxels.at(i);
        SVOConstructionNode node = { .mortenCode = (int64_t)i,
                                     .colour = (int16_t)((v.isSolid) ? v.colourIndex : -1) };

        queues[depth].push_back(node);
        int d = depth;
        while (d > 0 && queues[d].size() == 8)
        {
            std::unordered_map<int16_t, int> coloursUsed;

            SVOConstructionNode parent;
            parent.mortenCode = -1;
            bool isEmpty = true;
            for (size_t j = 0; j < 8; j++)
            {
                SVOConstructionNode child = queues[d][j];

                size_t currentSize = parsedNodes.size();
                parent.childrenIndices[j] = -1;

                int64_t childMortenCode = child.mortenCode;

                if (childMortenCode == -1 && child.colour >= 0)
                {
                    isEmpty = false;
                    parent.childrenIndices[j] = currentSize;
                    parsedNodes.push_back(child);

                    if (coloursUsed.find(child.colour) != coloursUsed.end())
                        coloursUsed.at(child.colour) += 1;
                    else
                        coloursUsed[child.colour] = 1;
                }

                if (childMortenCode >= 0)
                {
                    Voxel childVoxel = m_Voxels.at(child.mortenCode);

                    if (child.colour >= 0)
                    {
                        isEmpty = false;
                        parent.childrenIndices[j] = currentSize;
                        parsedNodes.push_back(child);

                        if (coloursUsed.find(child.colour) != coloursUsed.end())
                            coloursUsed.at(child.colour) += 1;
                        else
                            coloursUsed[child.colour] = 1;
                    }
                }
            }

            if (isEmpty)
            {
                parent.colour = -1;
            }
            else
            {
                int most = -1;
                uint8_t colour;
                for (auto pair : coloursUsed)
                {
                    if (pair.second > most)
                    {
                        most = pair.second;
                        colour = pair.first;
                    }
                }

                parent.colour = colour;
            }

            queues[d].clear();
            queues[d - 1].push_back(parent);
            d--;
        }
    }
    parsedNodes.push_back(queues[0][0]);

    std::vector<SVONode> finalNodes;
    size_t i = parsedNodes.size() - 1;
    for (auto itr = parsedNodes.rbegin(); itr != parsedNodes.rend(); itr++)
    {
        SVONode node;

        // spdlog::debug("{} | {} | {} | {}:{}:{}:{}:{}:{}:{}:{}", i, itr->mortenCode, itr->colour,
        //               itr->childrenIndices[0], itr->childrenIndices[1], itr->childrenIndices[2],
        //               itr->childrenIndices[3], itr->childrenIndices[4], itr->childrenIndices[5],
        //               itr->childrenIndices[6], itr->childrenIndices[7]);

        node.validMask = 0;
        node.leafMask = 0;
        // node.materialIndex = itr->colour;

        if (itr->mortenCode == -1 && itr->colour >= 0)
        {
            int childrenStartIndex = -1;
            for (int j = 7; j >= 0; j--)
            {
                if (childrenStartIndex < 0 && (*itr).childrenIndices[j] >= 0)
                {
                    childrenStartIndex = (*itr).childrenIndices[j];
                }

                if ((itr->childrenIndices[j]) >= 0)
                {
                    int mask = 1 << j;
                    node.validMask |= mask;

                    if ((parsedNodes.at(itr->childrenIndices[j]).mortenCode) >= 0)
                        node.leafMask |= mask;
                }
            }

            uint32_t offset = (i - childrenStartIndex);

            node.childPointer = offset;
        }
        else if (itr->mortenCode >= 0)
        {
            node.childPointer = 0x0;
            node.materialIndex = itr->colour;
        }
        finalNodes.push_back(node);

        i--;
    }

    double after = glfwGetTime();

    // for (size_t i = 0; i < finalNodes.size(); i++)
    // {
    //     SVONode node = finalNodes.at(i);
    //     spdlog::debug("{} | {} : {} : {} : {}", i, node.childPointer, node.materialIndex,
    //                   node.validMask, node.leafMask);
    // }

    size_t bytes = finalNodes.size() * sizeof(SVONode);
    spdlog::info("Generated {} nodes ({} Voxels) ({} B) ({} KiB) ({} MiB). Took {}s",
                 finalNodes.size(), m_Voxels.size(), bytes, bytes / 1024, bytes / (1024 * 1024),
                 after - before);

    return finalNodes;
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

void SceneManager::squareScene()
{
    const uint32_t VOXEL_SIZE = m_Dimension;

    // m_PaletteManager->flushColours();
    const uint8_t RED = m_PaletteManager->getColourIndex({ 1.0f, 0.0f, 0.0f, 1.0f });
    const uint8_t GREEN = m_PaletteManager->getColourIndex({ 0.0f, 1.0f, 0.0f, 1.0f });
    const uint8_t BLUE = m_PaletteManager->getColourIndex({ 0.0f, 0.0f, 1.0f, 1.0f });
    const uint8_t AQUA = m_PaletteManager->getColourIndex({ 0.0f, 1.0f, 1.0f, 1.0f });

    for (uint32_t y = 0; y < VOXEL_SIZE; y++)
    {
        for (uint32_t z = 0; z < VOXEL_SIZE; z++)
        {
            for (uint32_t x = 0; x < VOXEL_SIZE; x++)
            {
                uint32_t layerSum = x + z;
                uint32_t sum = x + y + z;
                uint32_t layerIndex = z * VOXEL_SIZE + x;
                uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                glm::vec3 position = glm::vec3(x, y, z);
                glm::vec3 squared = position * position;

                if (y % 2 == 0)
                {
                    if (sum % 2 == 0)
                        setVoxel({ x, y, z }, true, RED);
                    else
                        setVoxel({ x, y, z }, true, GREEN);
                }
                else
                {
                    if (sum % 2 == 0)
                        setVoxel({ x, y, z }, true, BLUE);
                    else
                        setVoxel({ x, y, z }, true, AQUA);
                }
            }
        }
    }
}

void SceneManager::holedSquareScene()
{
    const uint32_t VOXEL_SIZE = m_Dimension;

    // m_PaletteManager->flushColours();
    const uint8_t EMPTY = m_PaletteManager->getEmptyIndex();
    const uint8_t YELLOW = m_PaletteManager->getColourIndex({ 1.0f, 1.0f, 0.0f, 1.0f });
    const uint8_t MAGENTA = m_PaletteManager->getColourIndex({ 1.0f, 0.0f, 1.0f, 1.0f });

    for (uint32_t y = 0; y < VOXEL_SIZE; y++)
    {
        for (uint32_t z = 0; z < VOXEL_SIZE; z++)
        {
            for (uint32_t x = 0; x < VOXEL_SIZE; x++)
            {
                uint32_t layerSum = x + z;
                uint32_t sum = x + y + z;
                uint32_t layerIndex = z * VOXEL_SIZE + x;
                uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                glm::vec3 position = glm::vec3(x, y, z);
                glm::vec3 squared = position * position;

                if (y % 2 == 0)
                {
                    if (sum % 2 == 0)
                        setVoxel({ x, y, z }, true, YELLOW);
                    else
                        setVoxel({ x, y, z }, false);
                }
                else
                {
                    if (sum % 2 == 0)
                        setVoxel({ x, y, z }, true, MAGENTA);
                    else
                        setVoxel({ x, y, z }, false);
                }
            }
        }
    }
}

void SceneManager::randomObjectsScene()
{
    const uint32_t VOXEL_SIZE = m_Dimension;
    const uint32_t HALF_VOXEL_SIZE = m_Dimension / 2;

    // m_PaletteManager->flushColours();
    const uint8_t EMPTY = m_PaletteManager->getEmptyIndex();

    { // Top Left Front
        const float R = HALF_VOXEL_SIZE / 4.f;
        const float r = HALF_VOXEL_SIZE / 6.f;
        glm::vec3 center = glm::vec3(HALF_VOXEL_SIZE / 2.f);

        const uint8_t BLACK = m_PaletteManager->getColourIndex({ 0.0f, 0.0f, 0.0f, 1.0f });
        const uint8_t RED = m_PaletteManager->getColourIndex({ 1.0f, 0.0f, 0.0f, 1.0f });

        for (uint32_t y = 0; y < HALF_VOXEL_SIZE; y++)
        {
            for (uint32_t z = 0; z < HALF_VOXEL_SIZE; z++)
            {
                for (uint32_t x = 0; x < HALF_VOXEL_SIZE; x++)
                {
                    uint32_t layerSum = x + z;
                    uint32_t sum = x + y + z;
                    uint32_t layerIndex = z * VOXEL_SIZE + x;
                    uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                    glm::vec3 position = glm::vec3(x, y, z) - center;
                    glm::vec3 squared = position * position;

                    if (pow(R - sqrt(squared.x + squared.z), 2) + squared.y < r * r)
                    {
                        if (sum % 2 == 0)
                            setVoxel({ x, y, z }, true, BLACK);
                        else
                            setVoxel({ x, y, z }, true, RED);
                    }
                    else
                    {
                        setVoxel({ x, y, z }, false);
                    }
                }
            }
        }
    }

    { // Top Right Front
        const float R = HALF_VOXEL_SIZE / 3.f;
        glm::vec3 center = glm::vec3(HALF_VOXEL_SIZE / 2.f);
        center.x += HALF_VOXEL_SIZE;

        const uint8_t BLUE = m_PaletteManager->getColourIndex({ 0.0f, 0.0f, 1.0f, 1.0f });
        const uint8_t GREEN = m_PaletteManager->getColourIndex({ 0.0f, 1.0f, 0.0f, 1.0f });

        for (uint32_t y = 0; y < HALF_VOXEL_SIZE; y++)
        {
            for (uint32_t z = 0; z < HALF_VOXEL_SIZE; z++)
            {
                for (uint32_t x = HALF_VOXEL_SIZE; x < VOXEL_SIZE; x++)
                {
                    uint32_t layerSum = x + z;
                    uint32_t sum = x + y + z;
                    uint32_t layerIndex = z * VOXEL_SIZE + x;
                    uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                    glm::vec3 position = glm::vec3(x, y, z) - center;

                    if (dot(position, position) < R * R)
                    {
                        if (sum % 2 == 0)
                            setVoxel({ x, y, z }, true, BLUE);
                        else
                            setVoxel({ x, y, z }, true, GREEN);
                    }
                    else
                    {
                        setVoxel({ x, y, z }, false);
                    }
                }
            }
        }
    }

    { // Top Left Back
        const float R = HALF_VOXEL_SIZE / 2.f;
        glm::vec3 center = glm::vec3(HALF_VOXEL_SIZE / 2.f);
        center.z += HALF_VOXEL_SIZE;

        const uint8_t MAGENTA = m_PaletteManager->getColourIndex({ 1.0f, 0.0f, 1.0f, 1.0f });
        const uint8_t YELLOW = m_PaletteManager->getColourIndex({ 1.0f, 1.0f, 0.0f, 1.0f });

        for (uint32_t y = 0; y < HALF_VOXEL_SIZE; y++)
        {
            for (uint32_t z = HALF_VOXEL_SIZE; z < VOXEL_SIZE; z++)
            {
                for (uint32_t x = 0; x < HALF_VOXEL_SIZE; x++)
                {
                    uint32_t layerSum = x + z;
                    uint32_t sum = x + y + z;
                    uint32_t layerIndex = z * VOXEL_SIZE + x;
                    uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                    glm::vec3 position = glm::vec3(x, y, z) - center;
                    position.y = 0;

                    if (dot(position, position) < R * R)
                    {
                        if (sum % 2 == 0)
                            setVoxel({ x, y, z }, true, MAGENTA);
                        else
                            setVoxel({ x, y, z }, true, YELLOW);
                    }
                    else
                    {
                        setVoxel({ x, y, z }, false);
                    }
                }
            }
        }
    }

    { // Top Right Back
        const float R = HALF_VOXEL_SIZE / 2.0f;
        glm::vec3 center = glm::vec3(HALF_VOXEL_SIZE / 2.f);
        center.x += HALF_VOXEL_SIZE;
        center.z += HALF_VOXEL_SIZE;

        const uint8_t WHITE = m_PaletteManager->getColourIndex({ 1.0f, 1.0f, 1.0f, 1.0f });
        const uint8_t BLUE = m_PaletteManager->getColourIndex({ 0.0f, 1.0f, 1.0f, 1.0f });

        for (uint32_t y = 0; y < HALF_VOXEL_SIZE; y++)
        {
            for (uint32_t z = HALF_VOXEL_SIZE; z < VOXEL_SIZE; z++)
            {
                for (uint32_t x = HALF_VOXEL_SIZE; x < VOXEL_SIZE; x++)
                {
                    uint32_t layerSum = x + z;
                    uint32_t sum = x + y + z;
                    uint32_t layerIndex = z * VOXEL_SIZE + x;
                    uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                    glm::vec3 position = glm::vec3(x, y, z) - center;
                    position.y *= 1.8;
                    position.z *= 1.2;

                    if (dot(position, position) < R * R)
                    {
                        if (sum % 2 == 0)
                            setVoxel({ x, y, z }, true, BLUE);
                        else
                            setVoxel({ x, y, z }, true, WHITE);
                    }
                    else
                    {
                        setVoxel({ x, y, z }, false);
                    }
                }
            }
        }
    }

    { // Bottom Left Front
        const float R = HALF_VOXEL_SIZE / 3.0f;

        glm::vec3 center = glm::vec3(HALF_VOXEL_SIZE / 2.f);
        center.y += HALF_VOXEL_SIZE;

        const uint8_t YELLOW = m_PaletteManager->getColourIndex({ 1.0f, 1.0f, 1.0f, 1.0f });
        const uint8_t BLACK = m_PaletteManager->getColourIndex({ 0.0f, 0.0f, 0.0f, 1.0f });

        for (uint32_t y = HALF_VOXEL_SIZE; y < VOXEL_SIZE; y++)
        {
            for (uint32_t z = 0; z < HALF_VOXEL_SIZE; z++)
            {
                for (uint32_t x = 0; x < HALF_VOXEL_SIZE; x++)
                {
                    uint32_t layerSum = x + z;
                    uint32_t sum = x + y + z;
                    uint32_t layerIndex = z * VOXEL_SIZE + x;
                    uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                    glm::vec3 position = glm::vec3(x, y, z) - center;

                    float yz = fabs(position.y + position.z);
                    float zx = fabs(position.z + position.x);
                    float xy = fabs(position.x + position.y);

                    if (fmax(yz - 1, fmax(zx - 1, xy - 1)) < R)
                    {
                        if (sum % 2 == 0)
                            setVoxel({ x, y, z }, true, YELLOW);
                        else
                            setVoxel({ x, y, z }, true, BLACK);
                    }
                    else
                    {
                        setVoxel({ x, y, z }, false);
                    }
                }
            }
        }
    }

    { // Bottom Right Front
        const float R = HALF_VOXEL_SIZE / 2.0f;

        glm::vec3 center = glm::vec3(HALF_VOXEL_SIZE / 2.f);
        center.x += HALF_VOXEL_SIZE;
        center.y += HALF_VOXEL_SIZE;

        for (uint32_t y = HALF_VOXEL_SIZE; y < VOXEL_SIZE; y++)
        {
            for (uint32_t z = 0; z < HALF_VOXEL_SIZE; z++)
            {
                for (uint32_t x = HALF_VOXEL_SIZE; x < VOXEL_SIZE; x++)
                {
                    uint32_t layerSum = x + z;
                    uint32_t sum = x + y + z;
                    uint32_t layerIndex = z * VOXEL_SIZE + x;
                    uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                    glm::vec3 position = 1.f * (glm::vec3(x, y, z) - center);
                    float fx = position.x;
                    float fy = position.y;
                    float fz = position.z;
                    float implicit =
                        (2 * fz * (fz * fz - 3 * fx * fx) * (1 - fy * fy) +
                         pow(fx * fx + fz * fz, 2) - (9 * fy * fy - 1) * (1 - fy * fy)) -
                        5.f;

                    if (implicit <= 0)
                    {
                        if (sum % 2 == 0)
                            setVoxel({ x, y, z }, true, 6);
                        else
                            setVoxel({ x, y, z }, true, 3);
                    }
                    else
                    {
                        setVoxel({ x, y, z }, false);
                    }
                }
            }
        }
    }

    { // Bottom Left Back
        const float c = 1.0;
        const float y0 = 2.0;

        glm::vec3 center = glm::vec3(HALF_VOXEL_SIZE / 2.f);
        center.z += HALF_VOXEL_SIZE;
        center.y += HALF_VOXEL_SIZE;

        const uint8_t GREEN = m_PaletteManager->getColourIndex({ 0.0f, 1.0f, 0.0f, 1.0f });
        const uint8_t BLACK = m_PaletteManager->getColourIndex({ 0.0f, 0.0f, 0.0f, 1.0f });

        for (uint32_t y = HALF_VOXEL_SIZE; y < VOXEL_SIZE; y++)
        {
            for (uint32_t z = HALF_VOXEL_SIZE; z < VOXEL_SIZE; z++)
            {
                for (uint32_t x = 0; x < HALF_VOXEL_SIZE; x++)
                {
                    uint32_t layerSum = x + z;
                    uint32_t sum = x + y + z;
                    uint32_t layerIndex = z * VOXEL_SIZE + x;
                    uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                    glm::vec3 position = glm::vec3(x, y, z) - center;
                    float fx = position.x;
                    float fy = position.y;
                    float fz = position.z;

                    float implicit = (fx * fx + fz * fz) / (c * c) - pow(fy - y0, 2);

                    if (implicit <= 0)
                    {
                        if (sum % 2 == 0)
                            setVoxel({ x, y, z }, true, GREEN);
                        else
                            setVoxel({ x, y, z }, true, BLACK);
                    }
                    else
                    {
                        setVoxel({ x, y, z }, false);
                    }
                }
            }
        }
    }

    { // Bottom Right Back
        const float c = 1.0;
        const float y0 = 2.0;

        glm::vec3 center = glm::vec3(HALF_VOXEL_SIZE / 2.f);
        center.x += HALF_VOXEL_SIZE;
        center.z += HALF_VOXEL_SIZE;
        center.y += HALF_VOXEL_SIZE;

        for (uint32_t y = HALF_VOXEL_SIZE; y < VOXEL_SIZE; y++)
        {
            for (uint32_t z = HALF_VOXEL_SIZE; z < VOXEL_SIZE; z++)
            {
                for (uint32_t x = HALF_VOXEL_SIZE; x < VOXEL_SIZE; x++)
                {
                    uint32_t layerSum = x + z;
                    uint32_t sum = x + y + z;
                    uint32_t layerIndex = z * VOXEL_SIZE + x;
                    uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                    glm::vec3 position = glm::vec3(x, y, z) - center;
                    float fx = position.x;
                    float fy = position.y;
                    float fz = position.z;

                    float implicit = (fx * fx + fz * fz) / (c * c) - pow(fy - y0, 2);

                    if (implicit <= 0)
                    {
                        setVoxel({ x, y, z }, false);
                    }
                    else
                    {
                        if (sum % 2 == 0)
                            setVoxel({ x, y, z }, true, 2);
                        else
                            setVoxel({ x, y, z }, true, 5);
                    }
                }
            }
        }
    }
}

void SceneManager::sphereScene()
{
    const float R = m_Dimension / 2.0f;
    glm::vec3 center(m_Dimension / 2.f);

    // m_PaletteManager->flushColours();
    const uint8_t EMPTY = m_PaletteManager->getEmptyIndex();
    const uint8_t BLUE = m_PaletteManager->getColourIndex({ 0.f, 0.f, 1.f, 1.f });
    const uint8_t GREEN = m_PaletteManager->getColourIndex({ 0.f, 1.f, 0.f, 1.f });

    for (uint32_t y = 0; y < m_Dimension; y++)
    {
        for (uint32_t z = 0; z < m_Dimension; z++)
        {
            for (uint32_t x = 0; x < m_Dimension; x++)
            {
                uint32_t layerSum = x + z;
                uint32_t sum = x + y + z;
                uint32_t layerIndex = z * m_Dimension + x;
                uint32_t index = layerIndex + y * m_Dimension * m_Dimension;

                glm::vec3 position = glm::vec3(x, y, z) - center;

                if (dot(position, position) < R * R)
                {
                    if (sum % 2 == 0)
                        setVoxel({ x, y, z }, true, GREEN);
                    else
                        setVoxel({ x, y, z }, true, BLUE);
                }
                else
                {
                    setVoxel({ x, y, z }, false);
                }
            }
        }
    }
}

void SceneManager::torusScene()
{
    { // Top Left Front
        const float R = m_Dimension / 4.f;
        const float r = m_Dimension / 5.f;
        glm::vec3 center = glm::vec3(m_Dimension / 2.f);

        const uint8_t BLACK = m_PaletteManager->getColourIndex({ 0.0f, 0.0f, 0.0f, 1.0f });
        const uint8_t PINK = m_PaletteManager->getColourIndex({ 1.0f, 0.2f, 1.0f, 1.0f });

        for (uint32_t y = 0; y < m_Dimension; y++)
        {
            for (uint32_t z = 0; z < m_Dimension; z++)
            {
                for (uint32_t x = 0; x < m_Dimension; x++)
                {
                    uint32_t layerSum = x + z;
                    uint32_t sum = x + y + z;
                    uint32_t layerIndex = z * m_Dimension + x;

                    glm::vec3 position = glm::vec3(x, y, z) - center;
                    glm::vec3 squared = position * position;

                    if (pow(R - sqrt(squared.x + squared.z), 2) + squared.y < r * r)
                    {
                        if (sum % 2 == 0)
                            setVoxel({ x, y, z }, true, 0);
                        else
                            setVoxel({ x, y, z }, true, 1);
                    }
                    else
                    {
                        setVoxel({ x, y, z }, false);
                    }
                }
            }
        }
    }
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
