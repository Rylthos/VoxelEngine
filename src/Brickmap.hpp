#pragma once

#include <bitset>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#define BRICK_SIZE 8

struct Brick {
    uint64_t solidMask[8];
    uint32_t colourPtr;
    uint8_t lodR;
    uint8_t lodG;
    uint8_t lodB;
    uint8_t _;
};

class Brickmap
{
  public:
    Brickmap();

    void setAir(glm::ivec3 position);
    void setVoxel(glm::ivec3 position, glm::vec4 colour);
    std::optional<glm::vec4> getVoxel(glm::ivec3 position);

    std::optional<Brick> getStruct();
    std::vector<glm::vec4> getColours();

  private:
    Brick m_Brick;
    std::map<int, glm::vec4> m_Colours;

  private:
    bool validPosition(glm::ivec3 pos);
    uint64_t getColourIndex(glm::ivec3 pos);
};
