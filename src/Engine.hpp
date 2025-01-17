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
#include "Profilling.hpp"
#include "Queue.hpp"
#include "SceneManager.hpp"
#include "Window.hpp"

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore swapchainSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;
};

struct Stats {
    float frameDelta;
};

class Engine : EventReceiver {
  public:
  public:
    Engine() { }

    void init();
    void start();
    void cleanup();

    void receive(const Event* event) override;

  private:
    Camera m_Camera;

    Window m_Window;

    VkInstance m_Instance;
    VkDebugUtilsMessengerEXT m_DebugMessenger;
    VkSurfaceKHR m_Surface;
    VkPhysicalDevice m_PhysicalDevice;
    VkDevice m_Device;

    Queue m_GraphicsQueue;
    Queue m_ComputeQueue;

    VmaAllocator m_Allocator;

    VkFormat m_SwapchainImageFormat;
    VkExtent2D m_SwapchainImageExtent;
    VkSwapchainKHR m_Swapchain;
    std::vector<VkImage> m_SwapchainImages;
    std::vector<VkImageView> m_SwapchainImageViews;

    Image m_DrawImage;
    Image m_AltImage;

    bool m_RenderAlt = false;

    VkDescriptorSet m_VoxelDescriptorSet;
    VkDescriptorSetLayout m_VoxelDescriptorSetLayout;

    VkPipeline m_VoxelPipeline;
    VkPipelineLayout m_VoxelPipelineLayout;

    std::vector<FrameData> m_Frames;

    VkDescriptorPool m_DescriptorPool;

    VkCommandPool m_TracyCommandPool;
    VkCommandBuffer m_TracyCommandBuffer;

    bool m_RenderImGui = true;
    VkDescriptorPool m_ImguiPool;

    float m_QueryTimestampInterval;
    VkQueryPool m_QueryPool;
    uint64_t m_PreviousFrameTime;

    const uint32_t MAX_ITERATIONS = 4096;

    Stats m_Stats;

    SceneManager m_SceneManager;
    PaletteManager m_PaletteManager;

    bool m_ShouldResize = true;

  private:
    void initVulkan();

    void createSwapchain();
    void initSwapchain();
    void destroySwapchain();

    void initCommandPool();
    void initSyncStructures();

    void initImGui();

    void initDescriptorPool();
    void initDescriptorLayouts();

    void initPipelines();

    void initDescriptorSets();
    void recreateDescriptorSets();

    void initQueryPool();

    void resizeWindow();

    void updateImGui();

    void update(float frameDelta);
    void renderImGui(VkCommandBuffer& commandBuffer, VkImageView targetView, VkExtent2D extent);
    void render(float frameDelta);
};
