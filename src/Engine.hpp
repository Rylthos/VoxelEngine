#pragma once

#include "glm/glm.hpp"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"
#include <spdlog/spdlog.h>

#include <vector>

#include "Buffer.hpp"
#include "Image.hpp"
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

struct Stats {
    float frameDelta;
};

class Engine
{
  public:
  public:
    void init();
    void start();
    void cleanup();

  private:
    const uint32_t FRAMES_IN_FLIGHT = 2;

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

    std::vector<FrameData> m_Frames;

    VkDescriptorPool m_DescriptorPool;

    VkDescriptorSetLayout m_BackgroundDescriptorLayout;
    VkDescriptorSet m_BackgroundDescriptorSet;

    VkDescriptorSetLayout m_MeshDescriptorLayout;
    std::vector<VkDescriptorSet> m_MeshDescriptorSet;

    VkDescriptorPool m_ImguiPool;

    VkPipeline m_BackgroundPipeline;
    VkPipelineLayout m_BackgroundPipelineLayout;

    VkPipeline m_MeshPipeline;
    VkPipelineLayout m_MeshPipelineLayout;

    Buffer m_Vertices;

    Stats m_Stats;

  private:
    void initVulkan();

    void createSwapchain();
    void initSwapchain();
    void destroySwapchain();

    void initCommandPool();
    void initSyncStructures();

    void initImGui();

    void initBuffers();

    void initDescriptorPool();
    void initDescriptorLayouts();

    void initPipelines();

    void initDescriptorSets();

    void update(float frameDelta);
    void renderImGui(VkCommandBuffer& commandBuffer, VkImageView targetView, VkExtent2D extent);
    void render(float frameDelta);
};
