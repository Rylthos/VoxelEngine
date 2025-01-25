#pragma once

#include <vulkan/vulkan.h>

#include <functional>

class ImmediateSubmit {
  public:
    static void init(VkDevice device, VkQueue graphicsQueue, uint32_t graphicsQueueFamily);

    static void start();
    static void execute(std::function<void(VkCommandBuffer cmd)>&& function);
    static void end();

    static void submit(std::function<void(VkCommandBuffer cmd)>&& function);

    static void free();

  private:
    static VkFence m_Fence;
    static VkCommandPool m_CommandPool;
    static VkCommandBuffer m_CommandBuffer;

    static VkDevice m_Device;
    static VkQueue m_GraphicsQueue;

    inline static bool m_Started = false;
};
