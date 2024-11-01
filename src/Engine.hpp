#pragma once

#include "glm/glm.hpp"
#include "vk_mem_alloc.h"
#include "vulkan/vulkan.h"
#include <spdlog/spdlog.h>

#include <vector>

#include "Buffer.hpp"
#include "EventHandler.hpp"
#include "Events.hpp"
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

struct Voxel {
    glm::vec4 colour;
};

struct VoxelPushConstants {
    glm::mat4 viewMatrix;
    glm::uvec3 dimensions;
    float size;
    VkDeviceAddress voxelAddress;
};

struct Stats {
    float frameDelta;
};

class Engine : public EventReceiver
{
  public:
  public:
    Engine() {}
    virtual ~Engine() {}

    void init();
    void start();
    void cleanup();

    void receive(const Event* event) override;

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

    VkDescriptorSet m_VoxelDescriptorSet;
    VkDescriptorSetLayout m_VoxelDescriptorSetLayout;

    VkPipeline m_VoxelPipeline;
    VkPipelineLayout m_VoxelPipelineLayout;

    std::vector<FrameData> m_Frames;

    VkDescriptorPool m_DescriptorPool;

    VkDescriptorPool m_ImguiPool;

    const uint32_t VOXEL_SIZE = 16;
    size_t m_TotalVoxels;
    Buffer m_VoxelBuffer;

    Stats m_Stats;

  private:
    void initVulkan();

    void createSwapchain();
    void initSwapchain();
    void destroySwapchain();

    void initCommandPool();
    void initSyncStructures();

    void initImGui();

    void initVoxelBuffer();

    void initDescriptorPool();
    void initDescriptorLayouts();

    void initPipelines();

    void initDescriptorSets();

    void update(float frameDelta);
    void renderImGui(VkCommandBuffer& commandBuffer, VkImageView targetView, VkExtent2D extent);
    void render(float frameDelta);
};
