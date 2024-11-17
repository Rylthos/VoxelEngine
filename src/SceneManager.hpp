#pragma once

#include <glm/glm.hpp>
#include <string>

#include <spdlog/fmt/bin_to_hex.h>

#include "Buffer.hpp"
#include "PaletteManager.hpp"
#include "Voxel.hpp"

enum class Scene {
    START = 0,

    SQUARE,
    HOLED_SQUARE,
    RANDOM_OBJECTS,
    SPHERE,

    END
};

struct SVONode {
    uint32_t childPointer;
    uint16_t unused;
    uint8_t validMask;
    uint8_t leafMask;
} __attribute__((packed));

struct SVOConstructionNode {
    int64_t mortenCode;
    int64_t childrenIndices[8];
};

extern std::string stringOfScene(const Scene& scene);

class SceneManager
{
  public:
    SceneManager();
    SceneManager(uint32_t voxelDimension, PaletteManager* paletteManager);

    void loadScene(Scene newScene);
    Scene currentScene() { return m_CurrentScene; }

    void copyDataToBuffer(Buffer& buffer);

    Voxel getVoxel(glm::uvec3 position);
    void setVoxel(glm::uvec3 position, bool solid, uint8_t materialIndex = 0);
    void setVoxel(glm::uvec3 position, Voxel voxel);

    std::vector<SVONode> serializeScene();

  private:
    Scene m_CurrentScene = Scene::SPHERE;
    uint32_t m_Dimension;
    std::vector<Voxel> m_Voxels;
    PaletteManager* m_PaletteManager;

  private:
    void squareScene();
    void holedSquareScene();
    void randomObjectsScene();
    void sphereScene();

    int64_t splitBy3(uint32_t a);
    int64_t mortenEncode(glm::uvec3 position);

    uint32_t compactBy3(int64_t a);
    glm::uvec3 mortenDecode(int64_t code);
};
