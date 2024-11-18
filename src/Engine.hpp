#pragma once

#include "glm/glm.hpp"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"
#include <spdlog/spdlog.h>

#include <vector>

#include "Camera.hpp"
#include "EventHandler.hpp"
#include "Events.hpp"
#include "Image.hpp"
#include "PaletteManager.hpp"
#include "SceneManager.hpp"
#include "Window.hpp"

struct Queue {
    VkQueue queue;
    uint32_t queueFamily;
};

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore swapchainSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;
};

struct VoxelPushConstants {
    glm::vec4 cameraPosition;
    glm::vec4 cameraForward;
    glm::vec4 cameraRight;
    glm::vec4 cameraUp;
    uint32_t dimension;
    float size;
    uint32_t maxDepthShown = 5;
    uint32_t lod;
    VkDeviceAddress voxelAddress;
};

struct Stats {
    float frameDelta;
};

class Engine : EventReceiver
{
  public:
  public:
    Engine() {}

    void init();
    void start();
    void cleanup();

    void receive(const Event* event) override;

  private:
    const uint32_t FRAMES_IN_FLIGHT = 2;

    Camera m_Camera;

    Window m_Window;

    VkInstance m_Instance;
    VkDebugUtilsMessengerEXT m_DebugMessenger;
    VkSurfaceKHR m_Surface;
    VkPhysicalDevice m_PhysicalDevice;
    VkDevice m_Device;

    Queue m_GraphicsQueue;

    VmaAllocator m_Allocator;

    VkFormat m_SwapchainImageFormat;
    VkExtent2D m_SwapchainImageExtent;
    VkSwapchainKHR m_Swapchain;
    std::vector<VkImage> m_SwapchainImages;
    std::vector<VkImageView> m_SwapchainImageViews;

    Image m_DrawImage;
    Image m_RayImage;

    bool m_RenderRay = false;

    VkDescriptorSet m_VoxelDescriptorSet;
    VkDescriptorSetLayout m_VoxelDescriptorSetLayout;

    VkPipeline m_VoxelPipeline;
    VkPipelineLayout m_VoxelPipelineLayout;

    std::vector<FrameData> m_Frames;

    VkDescriptorPool m_DescriptorPool;

    bool m_RenderImGui = true;
    VkDescriptorPool m_ImguiPool;

    float m_QueryTimestampInterval;
    VkQueryPool m_QueryPool;
    uint64_t m_PreviousFrameTime;

    const uint32_t VOXEL_SIZE = 1 << 5;

    Stats m_Stats;

    SceneManager m_SceneManager;
    PaletteManager m_PaletteManager;

    VoxelPushConstants m_VoxelPushConstants;

  private:
    void initVulkan();

    void createSwapchain();
    void initSwapchain();
    void destroySwapchain();

    void initCommandPool();
    void initSyncStructures();

    void initImGui();

    void initImages();

    void updateScene();

    void initDescriptorPool();
    void initDescriptorLayouts();

    void initPipelines();

    void initDescriptorSets();

    void initQueryPool();

    void updateImGui();

    void update(float frameDelta);
    void renderImGui(VkCommandBuffer& commandBuffer, VkImageView targetView, VkExtent2D extent);
    void render(float frameDelta);
};
