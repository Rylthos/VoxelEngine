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

struct DeferredPushConstants {
    glm::vec4 sunDirection;
    glm::vec4 skyColour;
    glm::vec4 lightColour;
};

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore swapchainSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;
};

struct GBuffer {
    Image position;
    Image normal;
    Image colour;
    Image occlusion;
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

    GBuffer m_GBuffer;
    Image m_DrawImage;
    Image m_AltImage;

    bool m_RenderAlt = false;

    VkDescriptorSetLayout m_GBufferDescriptorSetLayout;
    VkDescriptorSet m_GBufferDescriptorSet;

    VkDescriptorSetLayout m_AltDescriptorSetLayout;
    VkDescriptorSet m_AltImageDescriptorSet;
    VkDescriptorSet m_DrawImageDescriptorSet;

    VkPipeline m_VoxelPipeline;
    VkPipelineLayout m_VoxelPipelineLayout;

    VkPipeline m_DeferredPipeline;
    VkPipelineLayout m_DeferredPipelineLayout;

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

    std::array<glm::vec4, 64> m_SSAOSamples;
    Image m_SSAONoise;

    bool m_IncreaseTime = false;
    float m_Time = 1200.f;
    float m_MinutePerSecond = 10.f;
    DeferredPushConstants m_DeferredPushConstants;

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
