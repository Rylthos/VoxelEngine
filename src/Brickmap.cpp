#include "Brickmap.hpp"

#include <cstdlib>
#include <cstring>

Brickmap::Brickmap()
{
    for (int i = 0; i < BRICK_SIZE; i++)
    {
        m_Brick.solidMask[i] = 0;
    }
}

void Brickmap::setAir(glm::ivec3 position)
{
    if (!validPosition(position))
    {
        return;
    }

    uint64_t mask = position.z * BRICK_SIZE + position.x;
    m_Brick.solidMask[position.y] &= ~(1 << mask);
}

void Brickmap::setVoxel(glm::ivec3 position, glm::vec4 colour)
{
    auto previous = getVoxel(position);

    uint64_t mask = position.z * BRICK_SIZE + position.x;
    m_Brick.solidMask[position.y] |= ((uint64_t)1) << mask;

    m_Colours[getColourIndex(position)] = colour;
}

std::optional<glm::vec4> Brickmap::getVoxel(glm::ivec3 position)
{
    if (!validPosition(position))
    {
        return {};
    }

    uint64_t mask = position.z * BRICK_SIZE + position.x;
    uint64_t v = m_Brick.solidMask[position.y] >> mask;
    if (v != 0)
    {
        return std::optional<glm::vec4>(m_Colours[getColourIndex(position)]);
    }
    else
    {
        return {};
    }
}

std::optional<Brick> Brickmap::getStruct()
{
    bool isAir = true;
    for (int i = 0; i < BRICK_SIZE; i++)
    {
        if (m_Brick.solidMask[i] != 0)
        {
            isAir = false;
            break;
        }
    }
    if (isAir) return {};

    m_Brick.lodR = 0;
    m_Brick.lodG = 255;
    m_Brick.lodB = 255;

    return std::optional(m_Brick);
}

std::vector<glm::vec4> Brickmap::getColours()
{
    std::vector<glm::vec4> colours;
    colours.reserve(m_Colours.size());
    for (auto i : m_Colours)
    {
        colours.push_back(i.second);
    }

    return colours;
}

bool Brickmap::validPosition(glm::ivec3 pos)
{
    return !(pos.x < 0 || pos.x >= BRICK_SIZE || pos.y < 0 || pos.y >= BRICK_SIZE || pos.z < 0 ||
             pos.z >= BRICK_SIZE);
}

uint64_t Brickmap::getColourIndex(glm::ivec3 position)
{
    return position.x + position.z * BRICK_SIZE + position.y * BRICK_SIZE * BRICK_SIZE;
}
