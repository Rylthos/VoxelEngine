#pragma once

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include <variant>

#define BRICK_SIZE 8
#define VOXEL_SIZE 0.125f

typedef std::tuple<glm::ivec3, glm::ivec3, glm::ivec3> WorldBrickPosition;
typedef std::tuple<glm::ivec3, glm::ivec3, glm::ivec3, glm::ivec3> WorldVoxelPosition;

#define ERASE_OP int
#define PLACE_OP glm::vec4
typedef std::variant<ERASE_OP, PLACE_OP> VoxelOp;
typedef std::tuple<WorldVoxelPosition, VoxelOp> VoxelChange;

enum class PlacementType : int { Cube = 0, Sphere = 1, NUM_TYPES };

struct BrickStruct {
    uint64_t solidMask[8];
    uint32_t colourPtr;
    uint8_t lodR;
    uint8_t lodG;
    uint8_t lodB;
    uint8_t _;
};

class Brick {
  public:
    Brick();

    void setAir(glm::ivec3 position);
    void setVoxel(glm::ivec3 position, glm::vec4 colour, bool replace);
    std::optional<glm::vec4> getVoxel(glm::ivec3 position);

    std::optional<BrickStruct> getStruct();
    std::vector<glm::vec4> getColours();
    size_t getColoursSize() { return m_Colours.size(); }

    void setSums(float r, float g, float b) { m_LODSum = { r, g, b }; }

  private:
    BrickStruct m_Brick;
    std::map<int, glm::vec4> m_Colours;

    glm::vec3 m_LODSum;

  private:
    bool validPosition(glm::ivec3 pos);
    uint64_t getColourIndex(glm::ivec3 pos);
};
