#include "Brick.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <glm/gtx/string_cast.hpp>
#include <spdlog/spdlog.h>

Brick::Brick()
{
    for (int i = 0; i < BRICK_SIZE; i++) {
        m_Brick.solidMask[i] = 0;
    }
    m_Brick.colourPtr = 0;
    m_Brick.lodR = 0;
    m_Brick.lodG = 0;
    m_Brick.lodB = 0;

    m_LODSum = glm::vec3(0.);
}

void Brick::setAir(glm::ivec3 position)
{
    if (!validPosition(position)) {
        spdlog::error("Invalid set position: {}", glm::to_string(position));
    }

    uint64_t mask = position.z * BRICK_SIZE + position.x;
    m_Brick.solidMask[position.y] &= ~((uint64_t)1 << mask);

    size_t index = getColourIndex(position);
    if (m_Colours.contains(index)) {
        glm::vec3 colour = m_Colours[index];
        m_LODSum -= colour;
    }
    m_Colours.erase(index);
}

void Brick::setVoxel(glm::ivec3 position, glm::vec4 colour, bool replace)
{
    if (!validPosition(position)) {
        spdlog::error("Invalid set position: {}", glm::to_string(position));
    };

    uint64_t mask = position.z * BRICK_SIZE + position.x;
    uint64_t bitMask = ((uint64_t)1) << mask;
    if (!replace && (m_Brick.solidMask[position.y] & bitMask) != 0) {
        return;
    }

    m_Brick.solidMask[position.y] |= bitMask;

    m_Colours[getColourIndex(position)] = colour;

    m_LODSum += colour;
}

std::optional<glm::vec4> Brick::getVoxel(glm::ivec3 position)
{
    if (!validPosition(position)) {
        return {};
    }

    uint64_t mask = position.z * BRICK_SIZE + position.x;
    uint64_t v = m_Brick.solidMask[position.y] >> mask;
    if (v != 0) {
        return std::optional<glm::vec4>(m_Colours[getColourIndex(position)]);
    } else {
        return {};
    }
}

std::optional<BrickStruct> Brick::getStruct()
{
    bool isAir = true;
    for (int i = 0; i < BRICK_SIZE; i++) {
        if (m_Brick.solidMask[i] != 0) {
            isAir = false;
            break;
        }
    }

    m_Brick.lodR = (int)std::clamp(m_LODSum.r / m_Colours.size(), 0.f, 1.f) * 255;
    m_Brick.lodG = (int)std::clamp(m_LODSum.g / m_Colours.size(), 0.f, 1.f) * 255;
    m_Brick.lodB = (int)std::clamp(m_LODSum.b / m_Colours.size(), 0.f, 1.f) * 255;

    if (isAir)
        return {};

    return std::optional(m_Brick);
}

std::vector<glm::vec4> Brick::getColours()
{
    std::vector<glm::vec4> colours;
    colours.reserve(m_Colours.size());
    for (auto i : m_Colours) {
        colours.push_back(i.second);
    }

    return colours;
}

bool Brick::validPosition(glm::ivec3 pos)
{
    return !(pos.x < 0 || pos.x >= BRICK_SIZE || pos.y < 0 || pos.y >= BRICK_SIZE || pos.z < 0
        || pos.z >= BRICK_SIZE);
}

uint64_t Brick::getColourIndex(glm::ivec3 position)
{
    return position.x + position.z * BRICK_SIZE + position.y * BRICK_SIZE * BRICK_SIZE;
}
