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
    uint8_t unused;
    uint8_t materialIndex;
    uint8_t validMask;
    uint8_t leafMask;
} __attribute__((packed));

struct SVOConstructionNode {
    int64_t mortenCode;
    int16_t colour;
    int64_t childrenIndices[8];
};

extern std::string stringOfScene(const Scene& scene);

class SceneManager
{
  public:
    SceneManager() {}
    ~SceneManager() { freeBuffers(); }
    SceneManager(uint32_t voxelDimension, PaletteManager* paletteManager);
    SceneManager(SceneManager& other);

    SceneManager operator=(const SceneManager& other);

    void initResources(VmaAllocator allocator);
    void freeResources();

    void loadScene(Scene newScene);
    Scene currentScene() { return m_CurrentScene; }

    VkDeviceAddress getBufferAddress(VkDevice device) { return m_SVO.getDeviceAddress(device); }
    void updateBuffers();

    Voxel getVoxel(glm::uvec3 position);
    void setVoxel(glm::uvec3 position, bool solid, uint8_t materialIndex = 0);
    void setVoxel(glm::uvec3 position, Voxel voxel);

    std::vector<SVONode> serializeScene();

  private:
    Scene m_CurrentScene = Scene::END;
    uint32_t m_Dimension;
    std::vector<Voxel> m_Voxels;
    PaletteManager* m_PaletteManager;

    VmaAllocator m_Allocator;
    Buffer m_SVO;
    Buffer m_Staging;

  private:
    void createBuffers(size_t size);
    void freeBuffers();

    void squareScene();
    void holedSquareScene();
    void randomObjectsScene();
    void sphereScene();

    int64_t splitBy3(uint32_t a);
    int64_t mortenEncode(glm::uvec3 position);

    uint32_t compactBy3(int64_t a);
    glm::uvec3 mortenDecode(int64_t code);
};
