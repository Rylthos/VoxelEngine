#include "ImmediateSubmit.hpp"

#include "VkCheck.hpp"

VkFence ImmediateSubmit::m_Fence;
VkCommandPool ImmediateSubmit::m_CommandPool;
VkCommandBuffer ImmediateSubmit::m_CommandBuffer;

VkDevice ImmediateSubmit::m_Device;
VkQueue ImmediateSubmit::m_GraphicsQueue;

void ImmediateSubmit::init(VkDevice device, VkQueue graphicsQueue, uint32_t graphicsQueueFamily)
{
    m_Device = device;
    m_GraphicsQueue = graphicsQueue;

    VkCommandPoolCreateInfo commandPoolCI{};
    commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCI.pNext = nullptr;
    commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCI.queueFamilyIndex = graphicsQueueFamily;

    VK_CHECK(vkCreateCommandPool(device, &commandPoolCI, nullptr, &m_CommandPool));

    VkCommandBufferAllocateInfo commandBufferAI{};
    commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAI.pNext = nullptr;
    commandBufferAI.commandPool = m_CommandPool;
    commandBufferAI.commandBufferCount = 1;
    commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(m_Device, &commandBufferAI, &m_CommandBuffer));

    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.pNext = nullptr;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VK_CHECK(vkCreateFence(device, &fenceCI, nullptr, &m_Fence));
}

void ImmediateSubmit::submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    VK_CHECK(vkResetFences(m_Device, 1, &m_Fence));
    VK_CHECK(vkResetCommandBuffer(m_CommandBuffer, 0));

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(m_CommandBuffer, &commandBufferBI));

    function(m_CommandBuffer);

    VK_CHECK(vkEndCommandBuffer(m_CommandBuffer));

    VkCommandBufferSubmitInfo commandBufferSI{};
    commandBufferSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandBufferSI.pNext = nullptr;
    commandBufferSI.commandBuffer = m_CommandBuffer;
    commandBufferSI.deviceMask = 0;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.pNext = nullptr;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandBufferSI;

    VK_CHECK(vkQueueSubmit2(m_GraphicsQueue, 1, &submitInfo, m_Fence));
    VK_CHECK(vkWaitForFences(m_Device, 1, &m_Fence, true, 1e10));
}

void ImmediateSubmit::free()
{
    vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
    vkDestroyFence(m_Device, m_Fence, nullptr);
}
