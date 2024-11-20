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
    TORUS,

    END
};

enum SVONodeFlags {
    SVONODE_IS_SOLID = 1 << 0,  // All Smaller nodes are equal
    SVONODE_IS_PARENT = 1 << 1, // Has Smaller Nodes
    SVONODE_IS_AIR = 1 << 2     // Is air
};

struct SVONode {
    uint32_t childPointer;
    uint8_t flags;
    uint8_t materialIndex;
    uint8_t validMask;
    uint8_t leafMask;
} __attribute__((packed));

// Remove morten code
// colour 16 bit
// children indices 1 32 bit
// leaf mask
// valid mask

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
    uint32_t updateBuffers();

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
    void torusScene();

    int64_t splitBy3(uint32_t a);
    int64_t mortenEncode(glm::uvec3 position);

    uint32_t compactBy3(int64_t a);
    glm::uvec3 mortenDecode(int64_t code);
};
