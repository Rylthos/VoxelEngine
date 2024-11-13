#include "SceneManager.hpp"

#include <spdlog/spdlog.h>

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

    default:
        return "ERROR";
    }
}

SceneManager::SceneManager() : m_Dimensions(0) {}

SceneManager::SceneManager(glm::ivec3 voxelDimensions, PaletteManager* paletteManager)
    : m_Dimensions(voxelDimensions), m_PaletteManager(paletteManager)
{
    m_Voxels.resize(voxelDimensions.x * voxelDimensions.y * voxelDimensions.z);
    loadScene(m_CurrentScene);
}

void SceneManager::loadScene(Scene newScene)
{
    m_CurrentScene = newScene;
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
    default:
        throw std::runtime_error("Invalid Scene");
    }
}

void SceneManager::copyDataToBuffer(Buffer& buffer)
{
    buffer.copyFromData_CPUOnly<Voxel>(m_Voxels);
}

void SceneManager::squareScene()
{
    const uint32_t VOXEL_SIZE = m_Dimensions.x;

    spdlog::info("Loaded Scene: Square");

    m_PaletteManager->flushColours();
    const uint8_t RED = m_PaletteManager->getColourIndex({ 1.0f, 0.0f, 0.0f, 1.0f });
    const uint8_t GREEN = m_PaletteManager->getColourIndex({ 0.0f, 1.0f, 0.0f, 1.0f });
    const uint8_t BLUE = m_PaletteManager->getColourIndex({ 0.0f, 0.0f, 1.0f, 1.0f });
    const uint8_t AQUA = m_PaletteManager->getColourIndex({ 0.0f, 1.0f, 1.0f, 1.0f });

    spdlog::info("Red: {}", RED);
    spdlog::info("Green: {}", GREEN);
    spdlog::info("Blue: {}", BLUE);
    spdlog::info("Aqua: {}", AQUA);

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
                        m_Voxels.at(index) = { .colourIndex = RED };
                    else
                        m_Voxels.at(index) = { .colourIndex = GREEN };
                }
                else
                {
                    if (sum % 2 == 0)
                        m_Voxels.at(index) = { .colourIndex = BLUE };
                    else
                        m_Voxels.at(index) = { .colourIndex = AQUA };
                }
            }
        }
    }
}

void SceneManager::holedSquareScene()
{
    const uint32_t VOXEL_SIZE = m_Dimensions.x;

    spdlog::info("Loaded Scene: Holed Square");

    m_PaletteManager->flushColours();
    const uint8_t EMPTY = m_PaletteManager->getEmptyIndex();
    const uint8_t YELLOW = m_PaletteManager->getColourIndex({ 1.0f, 1.0f, 0.0f, 1.0f });
    const uint8_t MAGENTA = m_PaletteManager->getColourIndex({ 1.0f, 0.0f, 1.0f, 1.0f });

    spdlog::info("Yellow: {}", YELLOW);
    spdlog::info("Magenta: {}", MAGENTA);

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
                        m_Voxels.at(index) = { .colourIndex = YELLOW };
                    else
                        m_Voxels.at(index) = { .colourIndex = EMPTY };
                }
                else
                {
                    if (sum % 2 == 0)
                        m_Voxels.at(index) = { .colourIndex = MAGENTA };
                    else
                        m_Voxels.at(index) = { .colourIndex = EMPTY };
                }
            }
        }
    }
}

void SceneManager::randomObjectsScene()
{
    const uint32_t VOXEL_SIZE = m_Dimensions.x;
    const uint32_t HALF_VOXEL_SIZE = m_Dimensions.x / 2;

    spdlog::info("Loaded Scene: Random Objects");

    m_PaletteManager->flushColours();
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
                            m_Voxels.at(index) = { .colourIndex = BLACK };
                        else
                            m_Voxels.at(index) = { .colourIndex = RED };
                    }
                    else
                    {
                        m_Voxels.at(index) = { .colourIndex = EMPTY };
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
                            m_Voxels.at(index) = { .colourIndex = BLUE };
                        else
                            m_Voxels.at(index) = { .colourIndex = GREEN };
                    }
                    else
                    {
                        m_Voxels.at(index) = { .colourIndex = EMPTY };
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
                            m_Voxels.at(index) = { .colourIndex = MAGENTA };
                        else
                            m_Voxels.at(index) = { .colourIndex = YELLOW };
                    }
                    else
                    {
                        m_Voxels.at(index) = { .colourIndex = EMPTY };
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
                            m_Voxels.at(index) = { .colourIndex = BLUE };
                        else
                            m_Voxels.at(index) = { .colourIndex = WHITE };
                    }
                    else
                    {
                        m_Voxels.at(index) = { .colourIndex = EMPTY };
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
                            m_Voxels.at(index) = { .colourIndex = YELLOW };
                        else
                            m_Voxels.at(index) = { .colourIndex = BLACK };
                    }
                    else
                    {
                        m_Voxels.at(index) = { .colourIndex = EMPTY };
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
                            m_Voxels.at(index) = { .colourIndex = 6 };
                        else
                            m_Voxels.at(index) = { .colourIndex = 3 };
                    }
                    else
                    {
                        m_Voxels.at(index) = { .colourIndex = 0 };
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
                            m_Voxels.at(index) = { .colourIndex = GREEN };
                        else
                            m_Voxels.at(index) = { .colourIndex = BLACK };
                    }
                    else
                    {
                        m_Voxels.at(index) = { .colourIndex = EMPTY };
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
                        m_Voxels.at(index) = { .colourIndex = 0 };
                    }
                    else
                    {
                        if (sum % 2 == 0)
                            m_Voxels.at(index) = { .colourIndex = 2 };
                        else
                            m_Voxels.at(index) = { .colourIndex = 5 };
                    }
                }
            }
        }
    }
}

void SceneManager::sphereScene()
{
    const float R = m_Dimensions.x / 2.0f;
    glm::vec3 center(m_Dimensions.x / 2.f);

    m_PaletteManager->flushColours();
    const uint8_t EMPTY = m_PaletteManager->getEmptyIndex();
    const uint8_t BLUE = m_PaletteManager->getColourIndex({ 0.f, 0.f, 1.f, 1.f });
    const uint8_t GREEN = m_PaletteManager->getColourIndex({ 0.f, 1.f, 0.f, 1.f });

    spdlog::info("Loaded Scene: Sphere");

    for (int32_t y = 0; y < m_Dimensions.y; y++)
    {
        for (int32_t z = 0; z < m_Dimensions.z; z++)
        {
            for (int32_t x = 0; x < m_Dimensions.x; x++)
            {
                uint32_t layerSum = x + z;
                uint32_t sum = x + y + z;
                uint32_t layerIndex = z * m_Dimensions.x + x;
                uint32_t index = layerIndex + y * m_Dimensions.x * m_Dimensions.z;

                glm::vec3 position = glm::vec3(x, y, z) - center;

                if (dot(position, position) < R * R)
                {
                    if (sum % 2 == 0)
                        m_Voxels.at(index) = { .colourIndex = GREEN };
                    else
                        m_Voxels.at(index) = { .colourIndex = BLUE };
                }
                else
                {
                    m_Voxels.at(index) = { .colourIndex = EMPTY };
                }
            }
        }
    }
}
