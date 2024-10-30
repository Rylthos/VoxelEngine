#include "Engine.hpp"

#include "VkBootstrap.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "Descriptors.hpp"
#include "PipelineBuilder.hpp"
#include "ShaderModule.hpp"
#include "VkCheck.hpp"

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

void Engine::init()
{
    m_Window.create("Voxel Engine", 500, 500);

    initVulkan();
    initSwapchain();
    initCommandPool();
    ImmediateSubmit::init(m_Device, m_GraphicsQueue.queue, m_GraphicsQueue.queueFamily);
    initSyncStructures();
    initImGui();
    initBuffers();
    initDescriptorPool();
    initDescriptorLayouts();
    initPipelines();
    initDescriptorSets();
}

void Engine::start()
{
    float currentTime;
    float previousTime = glfwGetTime();
    while (!m_Window.shouldClose())
    {
        currentTime = glfwGetTime();
        float frameDelta = currentTime - previousTime;
        previousTime = currentTime;

        m_Stats.frameDelta = frameDelta;

        m_Window.pollInput();

        update(frameDelta);

        render(frameDelta);

        m_Window.swapBuffes();
    }
}

void Engine::cleanup()
{
    vkDeviceWaitIdle(m_Device);

    ImmediateSubmit::free();

    m_Vertices.free();
    vkDestroyPipeline(m_Device, m_MeshPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_MeshPipelineLayout, nullptr);

    vkDestroyPipelineLayout(m_Device, m_BackgroundPipelineLayout, nullptr);
    vkDestroyPipeline(m_Device, m_BackgroundPipeline, nullptr);

    vkDestroyDescriptorSetLayout(m_Device, m_BackgroundDescriptorLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, m_MeshDescriptorLayout, nullptr);

    vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDestroyDescriptorPool(m_Device, m_ImguiPool, nullptr);

    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        vkDestroyFence(m_Device, m_Frames[i].renderFence, nullptr);
        vkDestroySemaphore(m_Device, m_Frames[i].renderSemaphore, nullptr);
        vkDestroySemaphore(m_Device, m_Frames[i].swapchainSemaphore, nullptr);
    }

    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        vkDestroyCommandPool(m_Device, m_Frames[i].commandPool, nullptr);
    }

    vkDestroyImageView(m_Device, m_DrawImage.imageView, nullptr);
    vmaDestroyImage(m_Allocator, m_DrawImage.image, m_DrawImage.allocation);
    destroySwapchain();

    vmaDestroyAllocator(m_Allocator);
    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger, nullptr);
    vkDestroyInstance(m_Instance, nullptr);
}

void Engine::initVulkan()
{
    vkb::InstanceBuilder builder;
    auto instRet = builder.set_app_name("VoxelEngine")
                       .request_validation_layers(true)
                       .use_default_debug_messenger()
                       .require_api_version(1, 3, 0)
                       .build();

    vkb::Instance vkbInst = instRet.value();
    m_Instance = vkbInst.instance;
    m_DebugMessenger = vkbInst.debug_messenger;
    m_Surface = m_Window.createSurface(m_Instance);
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
                              .set_surface(m_Surface)
                              .select();

    if (!vkbMaybeDevice.has_value())
    {
        spdlog::error("{}: {}", vkbMaybeDevice.error().value(), vkbMaybeDevice.error().message());
        exit(-1);
    }

    vkb::PhysicalDevice vkbPhysicalDevice = vkbMaybeDevice.value();

    vkb::DeviceBuilder deviceBuilder{ vkbPhysicalDevice };
    vkb::Device vkbDevice = deviceBuilder.build().value();

    m_PhysicalDevice = vkbPhysicalDevice.physical_device;
    m_Device = vkbDevice.device;
    spdlog::info("Created Devices");

    m_GraphicsQueue.queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_GraphicsQueue.queueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    spdlog::info("Created Queues");

    VmaAllocatorCreateInfo allocatorCI{};
    allocatorCI.physicalDevice = m_PhysicalDevice;
    allocatorCI.device = m_Device;
    allocatorCI.instance = m_Instance;
    allocatorCI.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorCI, &m_Allocator);
    spdlog::info("Created Allocator");
}

void Engine::createSwapchain()
{
    vkb::SwapchainBuilder swapchainBuilder{ m_PhysicalDevice, m_Device, m_Surface };
    m_SwapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain =
        swapchainBuilder
            .set_desired_format({ .format = m_SwapchainImageFormat,
                                  .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(m_Window.getSize().x, m_Window.getSize().y)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build()
            .value();

    m_SwapchainImageExtent = vkbSwapchain.extent;
    m_Swapchain = vkbSwapchain.swapchain;
    m_SwapchainImages = vkbSwapchain.get_images().value();
    m_SwapchainImageViews = vkbSwapchain.get_image_views().value();
    spdlog::info("Created Swapchain");
}

void Engine::initSwapchain()
{
    createSwapchain();

    VkExtent3D drawImageExtent = { m_Window.getSize().x, m_Window.getSize().y, 1 };

    m_DrawImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_DrawImage.extent = drawImageExtent;

    VkImageCreateInfo imageCI{};
    imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.pNext = nullptr;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = m_DrawImage.format;
    imageCI.extent = m_DrawImage.extent;
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VmaAllocationCreateInfo vmaImageCI{};
    vmaImageCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaImageCI.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vmaCreateImage(m_Allocator, &imageCI, &vmaImageCI, &m_DrawImage.image, &m_DrawImage.allocation,
                   nullptr);
    spdlog::info("Createed Swapchain Image");

    VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.pNext = nullptr;
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCI.image = m_DrawImage.image;
    imageViewCI.format = m_DrawImage.format;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 1;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VK_CHECK(vkCreateImageView(m_Device, &imageViewCI, nullptr, &m_DrawImage.imageView));
    spdlog::info("Createed Swapchain ImageView");
}

void Engine::destroySwapchain()
{
    vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);

    for (size_t i = 0; i < m_SwapchainImageViews.size(); i++)
    {
        vkDestroyImageView(m_Device, m_SwapchainImageViews[i], nullptr);
    }
    spdlog::info("Destroyed Swapchain");
}

void Engine::initCommandPool()
{
    VkCommandPoolCreateInfo commandPoolCI{};
    commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCI.pNext = nullptr;
    commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCI.queueFamilyIndex = m_GraphicsQueue.queueFamily;

    VkCommandBufferAllocateInfo commandBufferAI{};
    commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAI.pNext = nullptr;
    commandBufferAI.commandBufferCount = 1;
    commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    m_Frames.resize(FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(vkCreateCommandPool(m_Device, &commandPoolCI, nullptr, &m_Frames[i].commandPool));
        spdlog::info("Created Frame Command Pool: {}", i);

        commandBufferAI.commandPool = m_Frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(m_Device, &commandBufferAI, &m_Frames[i].commandBuffer));
        spdlog::info("Allocated Command Buffer: {}", i);
    }
}

void Engine::initSyncStructures()
{
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.pNext = nullptr;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo semaphoreCI{};
    semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCI.pNext = nullptr;

    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(vkCreateFence(m_Device, &fenceCI, nullptr, &m_Frames[i].renderFence));

        VK_CHECK(
            vkCreateSemaphore(m_Device, &semaphoreCI, nullptr, &m_Frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreCI, nullptr, &m_Frames[i].renderSemaphore));
        spdlog::info("Created Frame {} Sync structures", i);
    }
}

void Engine::initImGui()
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

    VK_CHECK(vkCreateDescriptorPool(m_Device, &poolCI, nullptr, &m_ImguiPool));

    ImGui::CreateContext();
    ImGuiIO io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(m_Window.get(), true);

    VkFormat colourFormat = m_SwapchainImageFormat;

    VkPipelineRenderingCreateInfoKHR pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineCI.pNext = nullptr;
    pipelineCI.colorAttachmentCount = 1;
    pipelineCI.pColorAttachmentFormats = &colourFormat;

    ImGui_ImplVulkan_InitInfo vulkanII{};
    vulkanII.Instance = m_Instance;
    vulkanII.PhysicalDevice = m_PhysicalDevice;
    vulkanII.Device = m_Device;
    vulkanII.Queue = m_GraphicsQueue.queue;
    vulkanII.QueueFamily = m_GraphicsQueue.queueFamily;
    vulkanII.DescriptorPool = m_ImguiPool;
    vulkanII.MinImageCount = 3;
    vulkanII.ImageCount = 3;
    vulkanII.UseDynamicRendering = true;
    vulkanII.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vulkanII.PipelineRenderingCreateInfo = pipelineCI;

    ImGui_ImplVulkan_Init(&vulkanII);
    spdlog::info("Initializsed ImGui");
}

void Engine::initBuffers()
{
    const size_t size = sizeof(glm::vec4) * 3;
    Buffer staging;
    staging.create(m_Allocator, size,
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VMA_MEMORY_USAGE_CPU_ONLY);
    std::vector<glm::vec4> vertexData = {
        { 0.0f,  -0.5f, 0.0f, 1.0f },
        { -0.5f, 0.5f,  0.0f, 1.0f },
        { 0.5f,  0.5f,  0.0f, 1.0f }
    };

    staging.copyFromData<glm::vec4>(vertexData);

    m_Vertices.create(m_Allocator, size,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      VMA_MEMORY_USAGE_GPU_ONLY);

    m_Vertices.copyFromBuffer(staging, size);
    spdlog::info("Created Vertex Buffer");
}

void Engine::initDescriptorPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  .descriptorCount = 1                },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = FRAMES_IN_FLIGHT }
    };

    VkDescriptorPoolCreateInfo descriptorPoolCI{};
    descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.pNext = nullptr;
    descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = FRAMES_IN_FLIGHT + 1;

    VK_CHECK(vkCreateDescriptorPool(m_Device, &descriptorPoolCI, nullptr, &m_DescriptorPool));
    spdlog::info("Created descriptor pool");
}
void Engine::initDescriptorLayouts()
{
    m_BackgroundDescriptorLayout = DescriptorLayoutBuilder::start(m_Device)
                                       .addStorageImage(0, VK_SHADER_STAGE_COMPUTE_BIT)
                                       .build();

    m_MeshDescriptorLayout = DescriptorLayoutBuilder::start(m_Device)
                                 .addStorageBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
                                 .build();
    spdlog::info("Created descriptor layouts");
}

void Engine::initPipelines()
{
    {

        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(glm::vec4) * 2;
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkPipelineLayoutCreateInfo computeLayoutCI{};
        computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayoutCI.pNext = nullptr;
        computeLayoutCI.pSetLayouts = &m_BackgroundDescriptorLayout;
        computeLayoutCI.setLayoutCount = 1;
        computeLayoutCI.pushConstantRangeCount = 1;
        computeLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr,
                                        &m_BackgroundPipelineLayout));

        ShaderModule gradientShader;
        gradientShader.create("res/shaders/gradient_color.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = gradientShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_BackgroundPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &m_BackgroundPipeline));

        spdlog::info("Created Background Pipeline and Pipeline Layout");
    }

    {
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VkDeviceAddress);

        VkPipelineLayoutCreateInfo meshLayoutCI{};
        meshLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        meshLayoutCI.pNext = nullptr;
        meshLayoutCI.setLayoutCount = 1;
        meshLayoutCI.pSetLayouts = &m_MeshDescriptorLayout;
        meshLayoutCI.pushConstantRangeCount = 1;
        meshLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(vkCreatePipelineLayout(m_Device, &meshLayoutCI, nullptr, &m_MeshPipelineLayout));

        ShaderModule vertShader;
        ShaderModule fragShader;

        vertShader.create("res/shaders/Triangle.vert.spv", m_Device);
        fragShader.create("res/shaders/Triangle.frag.spv", m_Device);

        m_MeshPipeline =
            PipelineBuilder::start()
                .setPipelineLayout(m_MeshPipelineLayout)
                .setShaders({
                    { VK_SHADER_STAGE_VERTEX_BIT,   vertShader.getShaderModule() },
                    { VK_SHADER_STAGE_FRAGMENT_BIT, fragShader.getShaderModule() }
        })
                .inputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
                .rasterizer(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE)
                .setMultisampleNone()
                .disableBlending()
                .disableDepthTest()
                .setColourAttachmentFormat(m_DrawImage.format)
                .buildPipeline(m_Device);

        spdlog::info("Created Mesh Pipeline and pipeline layout");
    }
}

void Engine::initDescriptorSets()
{

    m_MeshDescriptorSet = DescriptorSetBuilder::start(m_Device, m_DescriptorPool, FRAMES_IN_FLIGHT,
                                                      m_MeshDescriptorLayout)
                              .addStorageBuffer(0, m_Vertices.getBuffer(), 0, sizeof(glm::vec3) * 3)
                              .build();

    m_BackgroundDescriptorSet =
        DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_BackgroundDescriptorLayout)
            .addStorageImage(0, VK_IMAGE_LAYOUT_GENERAL, m_DrawImage.imageView)
            .build()
            .at(0);

    spdlog::info("Created descriptors");
}

void Engine::update(float frameDelta)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    const size_t frameTimeSize = 200;
    static float frameTimes[frameTimeSize];
    static int currentFrame = 0;

    float maxTime = 1000.0f;
    float minTime = 0.0f;
    float avgTime = 0.0f;

    frameTimes[currentFrame] = m_Stats.frameDelta;
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
        ImGui::Text("FPS: %1.3f", 1.0f / m_Stats.frameDelta);
    }
    ImGui::End();

    ImGui::ShowDemoWindow();
    ImGui::Render();
}

void Engine::renderImGui(VkCommandBuffer& commandBuffer, VkImageView targetView, VkExtent2D extent)
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

void Engine::render(float frameDelta)
{
    static uint32_t currentFrameIndex = 0;
    int frameIndex = currentFrameIndex % FRAMES_IN_FLIGHT;
    FrameData& currentFrame = m_Frames[frameIndex];

    VK_CHECK(vkWaitForFences(m_Device, 1, &currentFrame.renderFence, true, 1000000000));

    VK_CHECK(vkResetFences(m_Device, 1, &currentFrame.renderFence));

    uint32_t swapchainImageIndex;
    {
        vkAcquireNextImageKHR(m_Device, m_Swapchain, 1000000000, currentFrame.swapchainSemaphore,
                              nullptr, &swapchainImageIndex);
    }

    VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    VkExtent2D drawExtent;
    drawExtent.width = m_DrawImage.extent.width;
    drawExtent.height = m_DrawImage.extent.height;

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBI));

    transitionImage(commandBuffer, m_DrawImage.image, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL);
    transitionImage(commandBuffer, m_SwapchainImages[swapchainImageIndex],
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_BackgroundPipeline);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_BackgroundPipelineLayout, 0, 1, &m_BackgroundDescriptorSet, 0,
                            nullptr);

    static float total = 0.0f;
    total += frameDelta;
    glm::vec4 top = { fabs(cos(total)), fabs(sin(total)), 0.0f, 1.0f };
    glm::vec4 bottom = { 0.0f, fabs(cos(-total * 0.5)), fabs(sin(-total * 0.5)), 1.0f };

    glm::vec4 data[2] = { top, bottom };
    vkCmdPushConstants(commandBuffer, m_BackgroundPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(data), data);

    vkCmdDispatch(commandBuffer, std::ceil(drawExtent.width / 16.0),
                  std::ceil(drawExtent.height / 16.0), 1);

    transitionImage(commandBuffer, m_DrawImage.image, VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    {
        VkRenderingAttachmentInfo colourAI{};
        colourAI.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colourAI.pNext = nullptr;
        colourAI.imageView = m_DrawImage.imageView;
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
        renderInfo.renderArea = VkRect2D({ 0, 0 }, { drawExtent.width, drawExtent.height });
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colourAI;
        renderInfo.pDepthAttachment = nullptr;
        renderInfo.pStencilAttachment = nullptr;

        vkCmdBeginRendering(commandBuffer, &renderInfo);

        VkViewport viewport{};
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = drawExtent.width;
        viewport.height = drawExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset.x = 0.0f;
        scissor.offset.y = 0.0f;
        scissor.extent.width = drawExtent.width;
        scissor.extent.height = drawExtent.height;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_MeshPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_MeshPipelineLayout, 0, 1, &(m_MeshDescriptorSet.at(frameIndex)),
                                0, nullptr);

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        VkBufferDeviceAddressInfo deviceAI{};
        deviceAI.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        deviceAI.pNext = nullptr;
        deviceAI.buffer = m_Vertices.getBuffer();
        VkDeviceAddress address = vkGetBufferDeviceAddress(m_Device, &deviceAI);
        vkCmdPushConstants(commandBuffer, m_MeshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(VkDeviceAddress), &address);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdEndRendering(commandBuffer);
    }

    transitionImage(commandBuffer, m_DrawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    copyImageToImage(commandBuffer, m_DrawImage.image, m_SwapchainImages[swapchainImageIndex],
                     drawExtent, m_SwapchainImageExtent);

    transitionImage(commandBuffer, m_SwapchainImages[swapchainImageIndex],
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    renderImGui(commandBuffer, m_SwapchainImageViews[swapchainImageIndex], m_SwapchainImageExtent);

    transitionImage(commandBuffer, m_SwapchainImages[swapchainImageIndex],
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

    VK_CHECK(vkQueueSubmit2(m_GraphicsQueue.queue, 1, &submit, currentFrame.renderFence));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pWaitSemaphores = &currentFrame.renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &swapchainImageIndex;

    {
        vkQueuePresentKHR(m_GraphicsQueue.queue, &presentInfo);
    }

    currentFrameIndex++;
}
