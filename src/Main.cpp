#define VMA_IMPLEMENTATION
#define GLM_ENABLE_EXPERIMENTAL

#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtx/string_cast.hpp>

#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan.h"

#include <VkBootstrap.h>
#include <spdlog/spdlog.h>
#include <vk_mem_alloc.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <deque>
#include <fstream>
#include <vector>

#include "Buffer.hpp"
#include "DeletionQueue.hpp"
#include "Descriptors.hpp"
#include "ImmediateSubmit.hpp"
#include "PipelineBuilder.hpp"
#include "ShaderModule.hpp"
#include "VkCheck.hpp"
#include "Window.hpp"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800
#define FRAME_OVERLAP 2

struct Stats {
    float frameDelta;
};

struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;

    VkSemaphore swapchainSemaphore, renderSemaphore;
    VkFence renderFence;

    DeletionQueue deletionQueue;
};

Stats stats;

std::unique_ptr<Window> window;

VkInstance instance;
VkDebugUtilsMessengerEXT debugMessenger;
VkSurfaceKHR surface;
VkPhysicalDevice physicalDevice;
VkDevice device;

VkQueue graphicsQueue;
uint32_t graphicsQueueFamily;
VmaAllocator allocator;

VkFormat swapchainImageFormat;
VkExtent2D swapchainImageExtent;
VkSwapchainKHR swapchain;
std::vector<VkImage> swapchainImages;
std::vector<VkImageView> swapchainImageViews;
AllocatedImage drawImage;

std::vector<FrameData> frames(FRAME_OVERLAP);
std::unique_ptr<DeletionQueue> mainDeletionQueue;

VkDescriptorPool descriptorPool;

VkDescriptorSetLayout backgroundDescriptorLayout;
VkDescriptorSet backgroundDescriptor;

VkDescriptorSetLayout meshDescriptorLayout;
std::vector<VkDescriptorSet> meshDescriptor;

VkPipeline backgroundPipeline;
VkPipelineLayout backgroundPipelineLayout;

VkPipeline meshPipeline;
VkPipelineLayout meshPipelineLayout;

VkExtent2D drawExtent;

std::unique_ptr<Buffer> vertices;

void initVulkan()
{
    vkb::InstanceBuilder builder;
    auto instRet = builder.set_app_name("VoxelEngine")
                       .request_validation_layers(true)
                       .use_default_debug_messenger()
                       .require_api_version(1, 3, 0)
                       .build();

    vkb::Instance vkbInst = instRet.value();
    instance = vkbInst.instance;
    debugMessenger = vkbInst.debug_messenger;
    surface = window->createSurface(instance);
    spdlog::info("Created Window Surface");

    VkPhysicalDeviceVulkan13Features features13{};
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    VkPhysicalDeviceVulkan11Features features11{};
    features11.shaderDrawParameters = true;

    VkPhysicalDeviceFeatures features{};
    features.robustBufferAccess = true;
    features.fragmentStoresAndAtomics = true;
    features.imageCubeArray = true;
    features.geometryShader = true;

    vkb::PhysicalDeviceSelector selector{ vkbInst };
    auto vkbMaybeDevice = selector.set_minimum_version(1, 3)
                              .set_required_features_13(features13)
                              .set_required_features_12(features12)
                              .set_required_features_11(features11)
                              .set_required_features(features)
                              .set_surface(surface)
                              .select();

    if (!vkbMaybeDevice.has_value())
    {
        spdlog::error("{}: {}", vkbMaybeDevice.error().value(), vkbMaybeDevice.error().message());
        exit(-1);
    }

    vkb::PhysicalDevice vkbPhysicalDevice = vkbMaybeDevice.value();

    vkb::DeviceBuilder deviceBuilder{ vkbPhysicalDevice };
    vkb::Device vkbDevice = deviceBuilder.build().value();

    physicalDevice = vkbPhysicalDevice.physical_device;
    device = vkbDevice.device;
    spdlog::info("Created Devices");

    graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    spdlog::info("Created Queues");

    VmaAllocatorCreateInfo allocatorCI{};
    allocatorCI.physicalDevice = physicalDevice;
    allocatorCI.device = device;
    allocatorCI.instance = instance;
    allocatorCI.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorCI, &allocator);
    spdlog::info("Created Allocator");

    mainDeletionQueue->pushFunction([=]() {
        vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkb::destroy_debug_utils_messenger(instance, debugMessenger, nullptr);
        vkDestroyInstance(instance, nullptr);
    });
}

void createSwapchain()
{
    vkb::SwapchainBuilder swapchainBuilder{ physicalDevice, device, surface };
    swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain =
        swapchainBuilder
            .set_desired_format(
                { .format = swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(WINDOW_WIDTH, WINDOW_HEIGHT)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build()
            .value();

    swapchainImageExtent = vkbSwapchain.extent;
    swapchain = vkbSwapchain.swapchain;
    swapchainImages = vkbSwapchain.get_images().value();
    swapchainImageViews = vkbSwapchain.get_image_views().value();
    spdlog::info("Created Swapchain");
}

void destroySwapchain()
{
    vkDestroySwapchainKHR(device, swapchain, nullptr);

    for (size_t i = 0; i < swapchainImageViews.size(); i++)
    {
        vkDestroyImageView(device, swapchainImageViews[i], nullptr);
    }
    spdlog::info("Destroyed Swapchain");
}

void initSwapchain()
{
    createSwapchain();

    VkExtent3D drawImageExtent = { WINDOW_WIDTH, WINDOW_HEIGHT, 1 };

    drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    drawImage.imageExtent = drawImageExtent;

    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.pNext = nullptr;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = drawImage.imageFormat;
    imageCI.extent = drawImage.imageExtent;
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VmaAllocationCreateInfo vmaImageCI{};
    vmaImageCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaImageCI.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vmaCreateImage(allocator, &imageCI, &vmaImageCI, &drawImage.image, &drawImage.allocation,
                   nullptr);
    spdlog::info("Createed Swapchain Image");

    VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.pNext = nullptr;
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.image = drawImage.image;
    imageViewCI.format = drawImage.imageFormat;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VK_CHECK(vkCreateImageView(device, &imageViewCI, nullptr, &drawImage.imageView));
    spdlog::info("Createed Swapchain ImageView");

    mainDeletionQueue->pushFunction([=]() {
        vkDestroyImageView(device, drawImage.imageView, nullptr);
        vmaDestroyImage(allocator, drawImage.image, drawImage.allocation);
        destroySwapchain();
    });
}

void initCommands()
{
    VkCommandPoolCreateInfo commandPoolCI{};
    commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCI.pNext = nullptr;
    commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCI.queueFamilyIndex = graphicsQueueFamily;

    VkCommandBufferAllocateInfo commandBufferAI{};
    commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAI.pNext = nullptr;
    commandBufferAI.commandBufferCount = 1;
    commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    for (size_t i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateCommandPool(device, &commandPoolCI, nullptr, &frames[i].commandPool));
        spdlog::info("Created Frame Command Pool: {}", i);

        commandBufferAI.commandPool = frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(device, &commandBufferAI, &frames[i].commandBuffer));
        spdlog::info("Allocated Command Buffer: {}", i);
    }

    mainDeletionQueue->pushFunction([=]() {
        for (size_t i = 0; i < FRAME_OVERLAP; i++)
        {
            vkDestroyCommandPool(device, frames[i].commandPool, nullptr);
        }
        spdlog::info("Destroyed Frame Command Pools");
    });
}

void initSyncStructures()
{
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.pNext = nullptr;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo semaphoreCI{};
    semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCI.pNext = nullptr;

    for (size_t i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateFence(device, &fenceCI, nullptr, &frames[i].renderFence));

        VK_CHECK(vkCreateSemaphore(device, &semaphoreCI, nullptr, &frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(device, &semaphoreCI, nullptr, &frames[i].renderSemaphore));
        spdlog::info("Created Frame {} Sync structures", i);
    }
    mainDeletionQueue->pushFunction([=]() {
        for (size_t i = 0; i < FRAME_OVERLAP; i++)
        {
            vkDestroyFence(device, frames[i].renderFence, nullptr);
            vkDestroySemaphore(device, frames[i].renderSemaphore, nullptr);
            vkDestroySemaphore(device, frames[i].swapchainSemaphore, nullptr);
        }
        spdlog::info("Destroyed Frame sync structures");
    });
}

void initImGui()
{
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 },
    };

    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCI.maxSets = 1000;
    poolCI.poolSizeCount = (uint32_t)std::size(poolSizes);
    poolCI.pPoolSizes = poolSizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(device, &poolCI, nullptr, &imguiPool));

    ImGui::CreateContext();
    ImGuiIO io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window->get(), true);

    VkFormat colourFormat = swapchainImageFormat;

    VkPipelineRenderingCreateInfoKHR pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineCI.pNext = nullptr;
    pipelineCI.colorAttachmentCount = 1;
    pipelineCI.pColorAttachmentFormats = &colourFormat;

    ImGui_ImplVulkan_InitInfo vulkanII{};
    vulkanII.Instance = instance;
    vulkanII.PhysicalDevice = physicalDevice;
    vulkanII.Device = device;
    vulkanII.QueueFamily = graphicsQueueFamily;
    vulkanII.Queue = graphicsQueue;
    vulkanII.DescriptorPool = imguiPool;
    vulkanII.MinImageCount = 3;
    vulkanII.ImageCount = 3;
    vulkanII.UseDynamicRendering = true;
    vulkanII.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vulkanII.PipelineRenderingCreateInfo = pipelineCI;

    ImGui_ImplVulkan_Init(&vulkanII);
    spdlog::info("Initializsed ImGui");

    mainDeletionQueue->pushFunction([=]() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        vkDestroyDescriptorPool(device, imguiPool, nullptr);
        spdlog::info("Deinitialized ImGui");
    });
}

void initDescriptorPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  .descriptorCount = 1             },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = FRAME_OVERLAP }
    };

    VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.pNext = nullptr;
    descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = FRAME_OVERLAP + 1;

    VK_CHECK(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));
    spdlog::info("Created descriptor pool");

    mainDeletionQueue->pushFunction([&]() {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        spdlog::info("Destroyed Descriptor pool");
    });
}

void initDescriptorLayouts()
{
    backgroundDescriptorLayout = DescriptorLayoutBuilder::start(device)
                                     .addStorageImage(0, VK_SHADER_STAGE_COMPUTE_BIT)
                                     .build();

    meshDescriptorLayout = DescriptorLayoutBuilder::start(device)
                               .addStorageBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
                               .build();
    spdlog::info("Created descriptor layouts");

    mainDeletionQueue->pushFunction([&]() {
        vkDestroyDescriptorSetLayout(device, backgroundDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, meshDescriptorLayout, nullptr);
        spdlog::info("Destroyed Descriptor layouts");
    });
}

void initDescriptors()
{
    meshDescriptor =
        DescriptorSetBuilder::start(device, descriptorPool, FRAME_OVERLAP, meshDescriptorLayout)
            .addStorageBuffer(0, vertices->getBuffer(), 0, sizeof(glm::vec3) * 3)
            .build();

    backgroundDescriptor =
        DescriptorSetBuilder::start(device, descriptorPool, backgroundDescriptorLayout)
            .addStorageImage(0, VK_IMAGE_LAYOUT_GENERAL, drawImage.imageView)
            .build()
            .at(0);

    spdlog::info("Created descriptors");
}

void initBackgroundPipeline()
{
    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(glm::vec4) * 2;
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayoutCreateInfo computeLayoutCI{};
    computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayoutCI.pNext = nullptr;
    computeLayoutCI.pSetLayouts = &backgroundDescriptorLayout;
    computeLayoutCI.setLayoutCount = 1;
    computeLayoutCI.pushConstantRangeCount = 1;
    computeLayoutCI.pPushConstantRanges = &pushConstant;

    VK_CHECK(vkCreatePipelineLayout(device, &computeLayoutCI, nullptr, &backgroundPipelineLayout));

    ShaderModule gradientShader;
    gradientShader.create("res/shaders/gradient_color.comp.spv", device);

    VkPipelineShaderStageCreateInfo shaderStageCI{};
    shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCI.pNext = nullptr;
    shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageCI.module = gradientShader.getShaderModule();
    shaderStageCI.pName = "main";

    VkComputePipelineCreateInfo computePipelineCI{};
    computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCI.pNext = nullptr;
    computePipelineCI.layout = backgroundPipelineLayout;
    computePipelineCI.stage = shaderStageCI;

    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                      &backgroundPipeline));

    spdlog::info("Created Background Pipeline and Pipeline Layout");
    mainDeletionQueue->pushFunction([=]() {
        vkDestroyPipelineLayout(device, backgroundPipelineLayout, nullptr);
        vkDestroyPipeline(device, backgroundPipeline, nullptr);
        spdlog::info("Destroyed Background Pipeline and Pipeline Layout");
    });
}

void initMeshPipeline()
{
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(VkDeviceAddress);

    VkPipelineLayoutCreateInfo meshLayoutCI{};
    meshLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    meshLayoutCI.pNext = nullptr;
    meshLayoutCI.setLayoutCount = 1;
    meshLayoutCI.pSetLayouts = &meshDescriptorLayout;
    meshLayoutCI.pushConstantRangeCount = 1;
    meshLayoutCI.pPushConstantRanges = &pushConstant;

    VK_CHECK(vkCreatePipelineLayout(device, &meshLayoutCI, nullptr, &meshPipelineLayout));

    ShaderModule vertShader;
    ShaderModule fragShader;

    vertShader.create("res/shaders/Triangle.vert.spv", device);
    fragShader.create("res/shaders/Triangle.frag.spv", device);

    meshPipeline = PipelineBuilder::start()
                       .setPipelineLayout(meshPipelineLayout)
                       .setShaders({
                           { VK_SHADER_STAGE_VERTEX_BIT,   vertShader.getShaderModule() },
                           { VK_SHADER_STAGE_FRAGMENT_BIT, fragShader.getShaderModule() }
    })
                       .inputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
                       .rasterizer(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE)
                       .setMultisampleNone()
                       .disableBlending()
                       .disableDepthTest()
                       .setColourAttachmentFormat(drawImage.imageFormat)
                       .buildPipeline(device);

    spdlog::info("Created Mesh Pipeline and pipeline layout");
    mainDeletionQueue->pushFunction([=]() {
        vkDestroyPipelineLayout(device, meshPipelineLayout, nullptr);
        vkDestroyPipeline(device, meshPipeline, nullptr);
        spdlog::info("Destroyed Mesh Pipeline and pipeline layout");
    });
}

void initPipelines()
{
    initBackgroundPipeline();
    initMeshPipeline();
}

void initBuffers()
{
    const size_t size = sizeof(glm::vec4) * 3;
    Buffer staging;
    staging.create(allocator, size,
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VMA_MEMORY_USAGE_CPU_ONLY);
    std::vector<glm::vec4> vertexData = {
        { 0.0f,  -0.5f, 0.0f, 1.0f },
        { -0.5f, 0.5f,  0.0f, 1.0f },
        { 0.5f,  0.5f,  0.0f, 1.0f }
    };

    staging.copyFromData<glm::vec4>(vertexData);

    vertices = std::make_unique<Buffer>();
    vertices->create(allocator, size,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                     VMA_MEMORY_USAGE_GPU_ONLY);

    vertices->copyFromBuffer(staging, size);
    spdlog::info("Created Vertex Buffer");
}

void transitionImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currentLayout,
                     VkImageLayout newLayout)
{
    VkImageMemoryBarrier2 imageBarrier{};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imageBarrier.pNext = nullptr;
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    imageBarrier.oldLayout = currentLayout;
    imageBarrier.newLayout = newLayout;

    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                        ? VK_IMAGE_ASPECT_DEPTH_BIT
                                        : VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.aspectMask = aspectMask;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    imageBarrier.image = image;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.pNext = nullptr;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void copyImageToImage(VkCommandBuffer cmd, VkImage src, VkImage dest, VkExtent2D srcSize,
                      VkExtent2D dstSize)
{
    VkImageBlit2 blitRegion{};
    blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blitRegion.pNext = nullptr;

    blitRegion.srcOffsets[1].x = srcSize.width;
    blitRegion.srcOffsets[1].y = srcSize.height;
    blitRegion.srcOffsets[1].z = 1;

    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcSubresource.mipLevel = 0;

    blitRegion.dstOffsets[1].x = dstSize.width;
    blitRegion.dstOffsets[1].y = dstSize.height;
    blitRegion.dstOffsets[1].z = 1;

    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstSubresource.mipLevel = 0;

    VkBlitImageInfo2 blitInfo{};
    blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blitInfo.pNext = nullptr;
    blitInfo.srcImage = src;
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.dstImage = dest;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.filter = VK_FILTER_LINEAR;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;

    vkCmdBlitImage2(cmd, &blitInfo);
}

void renderImGuiImmediate()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    const size_t frameTimeSize = 200;
    static float frameTimes[frameTimeSize];
    static int currentFrame = 0;

    float maxTime = 1000.0f;
    float minTime = 0.0f;
    float avgTime = 0.0f;

    frameTimes[currentFrame] = stats.frameDelta;
    if (currentFrame + 1 == frameTimeSize)
    {
        for (size_t i = 0; i < frameTimeSize - 1; i++)
        {
            avgTime += frameTimes[i];
            maxTime = fmin(maxTime, frameTimes[i]);
            minTime = fmax(minTime, frameTimes[i]);
            frameTimes[i] = frameTimes[i + 1];
        }
        avgTime += frameTimes[frameTimeSize - 1];
        avgTime /= (float)frameTimeSize;
    }
    else
    {
        currentFrame += 1;
    }

    ImGui::NewFrame();

    if (ImGui::Begin("Stats"))
    {
        ImGui::PushItemWidth(ImGui::GetWindowContentRegionMax().x - 10.0f);
        ImGui::Text("Frametime (ms)");

        ImGui::PlotLines("##FrametimeGraph", frameTimes, frameTimeSize, 0, NULL, 0.0f, FLT_MAX,
                         ImVec2(0, 80.0f));
        ImGui::PopItemWidth();

        ImGui::Text("MAX: %1.3f : %.3f", maxTime, 1.0f / maxTime);
        ImGui::Text("AVG: %1.3f : %.2f", avgTime, 1.0f / avgTime);
        ImGui::Text("MIN: %1.3f : %.2f", minTime, 1.0f / minTime);
        ImGui::Text("FPS: %1.3f", 1.0f / stats.frameDelta);
    }
    ImGui::End();

    ImGui::ShowDemoWindow();
    ImGui::Render();
}

void renderImGui(VkCommandBuffer& commandBuffer, VkImageView targetView, VkExtent2D extent)
{
    VkRenderingAttachmentInfo colorAI{};
    colorAI.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAI.pNext = nullptr;
    colorAI.imageView = targetView;
    colorAI.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAI.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAI.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.pNext = nullptr;
    renderInfo.flags = 0;
    renderInfo.renderArea = VkRect2D({ 0, 0 }, extent);
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAI;
    renderInfo.pDepthAttachment = nullptr;
    renderInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBuffer, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRendering(commandBuffer);
}

void renderGeometry(VkCommandBuffer& commandBuffer, int currentFrame)
{
    VkRenderingAttachmentInfo colourAI{};
    colourAI.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colourAI.pNext = nullptr;
    colourAI.imageView = drawImage.imageView;
    colourAI.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colourAI.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colourAI.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colourAI.clearValue.color = {
        { 0.2f, 0.2f, 0.2f, 1.0f }
    };

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.pNext = nullptr;
    renderInfo.flags = 0;
    renderInfo.renderArea =
        VkRect2D({ 0, 0 }, { drawImage.imageExtent.width, drawImage.imageExtent.height });
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colourAI;
    renderInfo.pDepthAttachment = nullptr;
    renderInfo.pStencilAttachment = nullptr;

    vkCmdBeginRendering(commandBuffer, &renderInfo);

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = drawImage.imageExtent.width;
    viewport.height = drawImage.imageExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset.x = 0.0f;
    scissor.offset.y = 0.0f;
    scissor.extent.width = drawImage.imageExtent.width;
    scissor.extent.height = drawImage.imageExtent.height;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout, 0,
                            1, (&meshDescriptor.at(currentFrame)), 0, nullptr);

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkBufferDeviceAddressInfo deviceAI{};
    deviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    deviceAI.pNext = nullptr;
    deviceAI.buffer = vertices->getBuffer();
    VkDeviceAddress address = vkGetBufferDeviceAddress(device, &deviceAI);
    vkCmdPushConstants(commandBuffer, meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(VkDeviceAddress), &address);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRendering(commandBuffer);
}

int main()
{
    window = std::make_unique<Window>();
    window->create("Voxel Engine", WINDOW_WIDTH, WINDOW_HEIGHT);

    mainDeletionQueue = std::make_unique<DeletionQueue>();

    initVulkan();
    initSwapchain();
    initCommands();
    ImmediateSubmit::init(device, graphicsQueue, graphicsQueueFamily);
    initSyncStructures();
    initImGui();
    initDescriptorPool();
    initDescriptorLayouts();
    initPipelines();
    initBuffers();
    initDescriptors();

    int32_t currentFrameIndex = 0;

    float currentTime;
    float previousTime = glfwGetTime();
    while (!window->shouldClose())
    {
        currentTime = glfwGetTime();
        float delta = currentTime - previousTime;
        previousTime = currentTime;
        stats.frameDelta = delta;

        window->pollInput();

        renderImGuiImmediate();

        int frameIndex = currentFrameIndex % FRAME_OVERLAP;
        FrameData& currentFrame = frames[frameIndex];

        VK_CHECK(vkWaitForFences(device, 1, &currentFrame.renderFence, true, 1000000000));

        currentFrame.deletionQueue.flush();

        VK_CHECK(vkResetFences(device, 1, &currentFrame.renderFence));

        uint32_t swapchainImageIndex;
        {
            vkAcquireNextImageKHR(device, swapchain, 1000000000, currentFrame.swapchainSemaphore,
                                  nullptr, &swapchainImageIndex);
        }

        VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
        VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

        drawExtent.width = drawImage.imageExtent.width;
        drawExtent.height = drawImage.imageExtent.height;

        VkCommandBufferBeginInfo commandBufferBI{};
        commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBI.pNext = nullptr;
        commandBufferBI.pInheritanceInfo = nullptr;
        commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBI));

        transitionImage(commandBuffer, drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL);
        transitionImage(commandBuffer, swapchainImages[swapchainImageIndex],
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, backgroundPipeline);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                backgroundPipelineLayout, 0, 1, &backgroundDescriptor, 0, nullptr);

        static float total = 0.0f;
        total += delta;
        glm::vec4 top = { fabs(cos(total)), fabs(sin(total)), 0.0f, 1.0f };
        glm::vec4 bottom = { 0.0f, fabs(cos(-total * 0.5)), fabs(sin(-total * 0.5)), 1.0f };

        glm::vec4 data[2] = { top, bottom };
        vkCmdPushConstants(commandBuffer, backgroundPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(data), data);

        vkCmdDispatch(commandBuffer, std::ceil(drawExtent.width / 16.0),
                      std::ceil(drawExtent.height / 16.0), 1);

        transitionImage(commandBuffer, drawImage.image, VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        renderGeometry(commandBuffer, frameIndex);

        transitionImage(commandBuffer, drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        copyImageToImage(commandBuffer, drawImage.image, swapchainImages[swapchainImageIndex],
                         drawExtent, swapchainImageExtent);

        transitionImage(commandBuffer, swapchainImages[swapchainImageIndex],
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        renderImGui(commandBuffer, swapchainImageViews[swapchainImageIndex], swapchainImageExtent);

        transitionImage(commandBuffer, swapchainImages[swapchainImageIndex],
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        VkCommandBufferSubmitInfo commandBufferSI{};
        commandBufferSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferSI.pNext = nullptr;
        commandBufferSI.commandBuffer = commandBuffer;
        commandBufferSI.deviceMask = 0;

        VkSemaphoreSubmitInfo waitSI{};
        waitSI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitSI.pNext = nullptr;
        waitSI.semaphore = currentFrame.swapchainSemaphore;
        waitSI.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
        waitSI.deviceIndex = 0;
        waitSI.value = 1;

        VkSemaphoreSubmitInfo signalSI{};
        signalSI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalSI.pNext = nullptr;
        signalSI.semaphore = currentFrame.renderSemaphore;
        signalSI.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
        signalSI.deviceIndex = 0;
        signalSI.value = 1;

        VkSubmitInfo2 submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.pNext = nullptr;
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &waitSI;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signalSI;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &commandBufferSI;

        VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, currentFrame.renderFence));

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = nullptr;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.swapchainCount = 1;
        presentInfo.pWaitSemaphores = &currentFrame.renderSemaphore;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pImageIndices = &swapchainImageIndex;

        {
            vkQueuePresentKHR(graphicsQueue, &presentInfo);
        }

        currentFrameIndex++;

        window->swapBuffes();
    }
    spdlog::info("End of program");

    vkDeviceWaitIdle(device);

    vertices.reset();
    ImmediateSubmit::free();
    mainDeletionQueue->flush();

    window.reset();

    return 0;
}
