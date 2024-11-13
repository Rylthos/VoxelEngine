#pragma once

#include <glm/glm.hpp>
#include <string>

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

extern std::string stringOfScene(const Scene& scene);

class SceneManager
{
  public:
    SceneManager();
    SceneManager(glm::ivec3 voxelDimensions, PaletteManager* paletteManager);

    void loadScene(Scene newScene);
    Scene currentScene() { return m_CurrentScene; }

    void copyDataToBuffer(Buffer& buffer);

  private:
    Scene m_CurrentScene = Scene::SQUARE;
    glm::ivec3 m_Dimensions;
    std::vector<Voxel> m_Voxels;
    PaletteManager* m_PaletteManager;

  private:
    void squareScene();
    void holedSquareScene();
    void randomObjectsScene();
    void sphereScene();
};
