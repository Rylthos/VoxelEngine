#include "Engine.hpp"

#include "VkBootstrap.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <spdlog/fmt/ranges.h>

#include "Descriptors.hpp"
#include "PipelineBuilder.hpp"
#include "ShaderModule.hpp"
#include "VkCheck.hpp"

#include "Events.hpp"

#include <cmath>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

void Engine::init()
{
    spdlog::set_level(spdlog::level::trace);
    m_Window.create("Voxel Engine", 960, 960);

    m_PaletteManager.defaultPalette();
    m_SceneManager = SceneManager(VOXEL_SIZE, &m_PaletteManager);

    initVulkan();
    m_PaletteManager.initResources(m_Device, m_Allocator);
    m_SceneManager.initResources(m_Allocator);

    initSwapchain();
    initCommandPool();
    ImmediateSubmit::init(m_Device, m_GraphicsQueue.queue, m_GraphicsQueue.queueFamily);
    initSyncStructures();
    initImGui();
    initImages();
    initDescriptorPool();
    initDescriptorLayouts();
    initPipelines();
    initDescriptorSets();
    initQueryPool();

    m_SceneManager.loadScene(Scene::SPHERE);
    updateScene();

    m_Camera = Camera(glm::vec3(VOXEL_SIZE / 2.0f, 0.0f, 2.0f), 0.f, -45.f);

    EventHandler::subscribe({ EventType::KeyboardInput, EventType::ImGuiRender }, this);

    EventHandler::subscribe({ EventType::KeyboardInput, EventType::MouseMove, EventType::GameUpdate,
                              EventType::ImGuiRender },
                            &m_Camera);

    EventHandler::subscribe(EventType::ImGuiRender, &m_PaletteManager);

    m_VoxelPushConstants.maxIterations = MAX_ITERATIONS;
    m_VoxelPushConstants.maxDepthShown = std::log2(VOXEL_SIZE);
    m_VoxelPushConstants.maxHeatShown = m_VoxelPushConstants.maxIterations;
    m_VoxelPushConstants.lod = std::log2(VOXEL_SIZE);

    m_VoxelPushConstants.flags = 0;
    m_VoxelPushConstants.flags ^= PCF_SHOW_HEAT_MAP;

    m_RenderAlt = false;
}

void Engine::start()
{
    float currentTime;
    float previousTime = glfwGetTime();
    // return;
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

    vkDestroyQueryPool(m_Device, m_QueryPool, nullptr);

    vkDestroyPipeline(m_Device, m_VoxelPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_VoxelPipelineLayout, nullptr);

    vkDestroyPipeline(m_Device, m_VoxelGenerationPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_VoxelGenerationPipelineLayout, nullptr);
    m_GeneratedVoxels.free();

    vkDestroyDescriptorSetLayout(m_Device, m_VoxelDescriptorSetLayout, nullptr);

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

    m_DrawImage.free();
    m_AltImage.free();

    m_SceneManager.freeResources();
    m_PaletteManager.freeResources();

    destroySwapchain();

    vmaDestroyAllocator(m_Allocator);
    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger, nullptr);
    vkDestroyInstance(m_Instance, nullptr);
}

void Engine::receive(const Event* event)
{
    switch (event->getType())
    {
    case EventType::KeyboardInput:
        {
            const KeyboardInput* ki = reinterpret_cast<const KeyboardInput*>(event);

            if (ki->key == GLFW_KEY_M && ki->action == GLFW_PRESS) m_RenderImGui = !m_RenderImGui;

            // if (ki->key == GLFW_KEY_RIGHT_CONTROL && ki->action == GLFW_PRESS)
            //     m_RenderAlt = !m_RenderAlt;

            break;
        }
    case EventType::ImGuiRender:
        {
            updateImGui();
            break;
        }
    default:
        break;
    }
}

void Engine::initVulkan()
{
    vkb::InstanceBuilder builder;
    auto system_info = vkb::SystemInfo::get_system_info().value();

    auto instRet = builder.set_app_name("VoxelEngine")
                       .request_validation_layers(true)
                       .use_default_debug_messenger()
                       .require_api_version(1, 3, 0)
                       .build();

    if (!instRet)
    {
        spdlog::error("Failed to create Instance: {}", instRet.error().message());
        exit(-1);
    }

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
    features12.hostQueryReset = true;
    features12.shaderInt8 = true;
    features12.storageBuffer8BitAccess = true;

    VkPhysicalDeviceVulkan11Features features11{};
    features11.shaderDrawParameters = true;
    features11.storageBuffer16BitAccess = true;

    VkPhysicalDeviceFeatures features{};
    features.robustBufferAccess = true;
    features.fragmentStoresAndAtomics = true;
    features.imageCubeArray = true;
    features.geometryShader = true;
    features.shaderInt16 = true;
    features.shaderInt64 = true;

    vkb::PhysicalDeviceSelector selector{ vkbInst };
    auto vkbMaybeDevice =
        selector.set_minimum_version(1, 3)
            .set_required_features_13(features13)
            .set_required_features_12(features12)
            .set_required_features_11(features11)
            .set_required_features(features)
            .add_required_extension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME)
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

    m_DrawImage.create(m_Allocator, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageExtent,
                       VK_IMAGE_TYPE_2D,
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                       VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_DrawImage.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);

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
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
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
    spdlog::info("Initialized ImGui");
}

void Engine::initImages()
{
    m_AltImage.create(m_Allocator, VK_FORMAT_R32G32B32A32_SFLOAT, m_DrawImage.getExtent(),
                      VK_IMAGE_TYPE_2D,
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                      VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_AltImage.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);
}

void Engine::updateScene()
{
    vkDeviceWaitIdle(m_Device);

    ImmediateSubmit::submit([&](VkCommandBuffer buffer) {
        vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_VoxelGenerationPipeline);

        VoxelGenerationPushConstants pushConstant;
        pushConstant.size = 1.0f;
        pushConstant.dimension = VOXEL_SIZE;
        pushConstant.targetBuffer = m_GeneratedVoxels.getDeviceAddress(m_Device);
        vkCmdPushConstants(buffer, m_VoxelGenerationPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(VoxelGenerationPushConstants), &pushConstant);

        vkCmdDispatch(buffer, VOXEL_SIZE / 4, VOXEL_SIZE / 4, VOXEL_SIZE / 4);
    });

    m_GeneratedVoxels.copyToVector<Voxel>(m_SceneManager.getVoxels());

    m_VoxelPushConstants.initialParent = m_SceneManager.updateBuffers();
    m_PaletteManager.updateImage();
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
    m_VoxelDescriptorSetLayout = DescriptorLayoutBuilder::start(m_Device)
                                     .addStorageImage(0, VK_SHADER_STAGE_COMPUTE_BIT)
                                     .addStorageImage(1, VK_SHADER_STAGE_COMPUTE_BIT)
                                     .addStorageImage(2, VK_SHADER_STAGE_COMPUTE_BIT)
                                     .build();
    spdlog::info("Created descriptor layouts");
}

void Engine::initPipelines()
{
    {
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VoxelPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkPipelineLayoutCreateInfo computeLayoutCI{};
        computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayoutCI.pNext = nullptr;
        computeLayoutCI.setLayoutCount = 1;
        computeLayoutCI.pSetLayouts = &m_VoxelDescriptorSetLayout;
        computeLayoutCI.pushConstantRangeCount = 1;
        computeLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(
            vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr, &m_VoxelPipelineLayout));

        ShaderModule voxelShader;
        voxelShader.create("res/shaders/EfficientSVO.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = voxelShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_VoxelPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &m_VoxelPipeline));
        spdlog::info("Created Background Pipeline and Pipeline Layout");
    }

    {
        m_GeneratedVoxels.create(m_Allocator, VOXEL_SIZE * VOXEL_SIZE * VOXEL_SIZE * sizeof(Voxel),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                 VMA_MEMORY_USAGE_GPU_TO_CPU);

        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VoxelGenerationPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkPipelineLayoutCreateInfo computeLayoutCI{};
        computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayoutCI.pNext = nullptr;
        computeLayoutCI.setLayoutCount = 0;
        computeLayoutCI.pSetLayouts = nullptr;
        computeLayoutCI.pushConstantRangeCount = 1;
        computeLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr,
                                        &m_VoxelGenerationPipelineLayout));

        ShaderModule voxelShader;
        voxelShader.create("res/shaders/Generation.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI{};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = voxelShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI{};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_VoxelGenerationPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr,
                                          &m_VoxelGenerationPipeline));
        spdlog::info("Created Background Pipeline and Pipeline Layout");
    }
}

void Engine::initDescriptorSets()
{
    m_VoxelDescriptorSet =
        DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_VoxelDescriptorSetLayout)
            .addStorageImage(0, VK_IMAGE_LAYOUT_GENERAL, m_DrawImage.getImageView())
            .addStorageImage(1, VK_IMAGE_LAYOUT_GENERAL, m_AltImage.getImageView())
            .addStorageImage(2, VK_IMAGE_LAYOUT_GENERAL, m_PaletteManager.getImage().getImageView())
            .build()
            .at(0);

    spdlog::info("Created descriptors");
}

void Engine::initQueryPool()
{
    VkQueryPoolCreateInfo queryPoolCI{};
    queryPoolCI.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolCI.pNext = nullptr;
    queryPoolCI.flags = 0;
    queryPoolCI.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolCI.queryCount = m_Frames.size() * 2;
    VK_CHECK(vkCreateQueryPool(m_Device, &queryPoolCI, nullptr, &m_QueryPool));

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &deviceProperties);
    m_QueryTimestampInterval = deviceProperties.limits.timestampPeriod;

    spdlog::info("Created Query Pool");

    vkResetQueryPool(m_Device, m_QueryPool, 0, m_Frames.size() * 2);
}

void Engine::updateImGui()
{
    static std::array<float, 200> frameTimes;
    static size_t currentFrame = 0;

    double dispatchTime = m_PreviousFrameTime * m_QueryTimestampInterval / 1000000;

    float maxTime = 1000.0f;
    float minTime = 0.0f;
    float avgTime = 0.0f;

    // frameTimes[currentFrame] = m_Stats.frameDelta;
    frameTimes[currentFrame] = dispatchTime;
    if (currentFrame > 0)
    {
        for (size_t i = 0; i < currentFrame; i++)
        {
            avgTime += frameTimes[i];
            maxTime = fmin(maxTime, frameTimes[i]);
            minTime = fmax(minTime, frameTimes[i]);

            if (currentFrame == frameTimes.size() - 1)
            {
                frameTimes[i] = frameTimes[i + 1];
            }
        }
        avgTime += frameTimes[currentFrame];
        maxTime = fmin(maxTime, frameTimes[currentFrame]);
        minTime = fmax(minTime, frameTimes[currentFrame]);
        avgTime /= (float)currentFrame;
    }

    if (currentFrame + 1 < frameTimes.size())
    {
        currentFrame += 1;
    }

    if (ImGui::Begin("Stats"))
    {
        ImGui::PushItemWidth(ImGui::GetWindowContentRegionMax().x - 10.0f);
        ImGui::Text("Dispatch Time %.3f(ms)", dispatchTime);

        ImGui::PlotLines("##FrametimeGraph", frameTimes.data(), frameTimes.size(), 0, NULL, 0.0f,
                         FLT_MAX, ImVec2(0, 80.0f));
        ImGui::PopItemWidth();

        ImGui::Text("MAX: %1.3f : %.3f", maxTime, 1000.0f / maxTime);
        ImGui::Text("AVG: %1.3f : %.2f", avgTime, 1000.0f / avgTime);
        ImGui::Text("MIN: %1.3f : %.2f", minTime, 1000.0f / minTime);

        ImGui::Text("FPS: %1.3f", 1.0f / m_Stats.frameDelta);
    }
    ImGui::End();

    if (ImGui::Begin("Scene"))
    {
        static size_t selectedIndex = static_cast<size_t>(m_SceneManager.currentScene());
        size_t startIndex = static_cast<size_t>(Scene::START) + 1;
        size_t endIndex = static_cast<size_t>(Scene::END);
        std::string preview = stringOfScene(m_SceneManager.currentScene());

        ImGui::Text("Current Scene");

        if (ImGui::BeginCombo("##Scene", preview.c_str(), 0))
        {
            bool hasChanged = false;
            for (size_t i = startIndex; i < endIndex; i++)
            {
                Scene scene = static_cast<Scene>(i);
                const bool isSelected = (selectedIndex == i);
                if (ImGui::Selectable(stringOfScene(scene).c_str(), isSelected))
                {
                    selectedIndex = i;
                    m_SceneManager.loadScene(static_cast<Scene>(i));
                    hasChanged = true;
                }

                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            if (hasChanged) updateScene();

            ImGui::EndCombo();
        }

        ImGui::Text("Max Iterations");
        int maxIterations = m_VoxelPushConstants.maxIterations;
        if (ImGui::SliderInt("##MaxIterations", &maxIterations, 1, MAX_ITERATIONS))
        {
            m_VoxelPushConstants.maxIterations = maxIterations;
        }

        if (ImGui::Checkbox("Show Alternative View", &m_RenderAlt)) ImGui::Text("Max Iterations");

        bool showHeatMap = (m_VoxelPushConstants.flags & PCF_SHOW_HEAT_MAP) != 0;
        if (ImGui::Checkbox("Show Heat Map", &showHeatMap))
        {
            m_VoxelPushConstants.flags &= ~(PCF_SHOW_HEAT_MAP);
            m_VoxelPushConstants.flags |= (showHeatMap * PCF_SHOW_HEAT_MAP);
        }

        if (showHeatMap)
        {
            ImGui::Text("Max Heat Shown");
            int maxHeat = m_VoxelPushConstants.maxHeatShown;
            if (ImGui::SliderInt("##MaxHeat", &maxHeat, 1, MAX_ITERATIONS))
                m_VoxelPushConstants.maxHeatShown = maxHeat;
        }
        else
        {
            ImGui::Text("Max Depth Shown");
            int maxDepth = m_VoxelPushConstants.maxDepthShown;
            if (ImGui::SliderInt("##MaxDepth", &maxDepth, 1, std::log2(VOXEL_SIZE)))
                m_VoxelPushConstants.maxDepthShown = maxDepth;
        }

        ImGui::Text("Max LOD");
        int LOD = m_VoxelPushConstants.lod;
        if (ImGui::SliderInt("##MaxLOD", &LOD, 1, std::log2(VOXEL_SIZE)))
            m_VoxelPushConstants.lod = LOD;
    }
    ImGui::End();

    ImGui::ShowDemoWindow();
}

void Engine::update(float frameDelta)
{
    GameUpdate update;
    update.frameDelta = frameDelta;
    EventHandler::dispatchEvent(&update);

    static float t = 0.0f;
    t += frameDelta;

    glm::vec4 colour1 = { 1.0f, 1.0f, 0.0f, 1.0f };
    glm::vec4 colour2 = { 1.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 colour3 = { 0.0f, 1.0f, 0.0f, 1.0f };
    glm::vec4 colour4 = { 0.0f, 0.0f, 1.0f, 1.0f };

    float tValue = sin(0.1 * t) * sin(0.1 * t);
    m_PaletteManager.setColourIndex(0, glm::mix(colour1, colour2, tValue));
    m_PaletteManager.setColourIndex(1, glm::mix(colour3, colour4, tValue));

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();

    ImGuiRender imGuiRender;
    EventHandler::dispatchEvent(&imGuiRender);

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

    if (m_RenderImGui)
    {
        vkCmdBeginRendering(commandBuffer, &renderInfo);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

        vkCmdEndRendering(commandBuffer);
    }
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
    drawExtent.width = m_DrawImage.getExtent().width;
    drawExtent.height = m_DrawImage.getExtent().height;

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    Image& renderImage = m_RenderAlt ? m_AltImage : m_DrawImage;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBI));

    vkCmdResetQueryPool(commandBuffer, m_QueryPool, frameIndex * 2, 2);

    m_DrawImage.transition(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    m_AltImage.transition(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    m_PaletteManager.getImage().transition(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_GENERAL);

    Image::transition(commandBuffer, m_SwapchainImages[swapchainImageIndex],
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_QueryPool,
                        frameIndex * 2);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_VoxelPipeline);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_VoxelPipelineLayout, 0,
                            1, &m_VoxelDescriptorSet, 0, nullptr);

    m_VoxelPushConstants.cameraPosition = m_Camera.getPosition();
    m_VoxelPushConstants.cameraForward = m_Camera.getForward();
    m_VoxelPushConstants.cameraRight = m_Camera.getRight();
    m_VoxelPushConstants.cameraUp = m_Camera.getUp();

    m_VoxelPushConstants.size = 1.0f;

    m_VoxelPushConstants.dimension = VOXEL_SIZE;
    m_VoxelPushConstants.voxelAddress = m_SceneManager.getBufferAddress(m_Device);

    vkCmdPushConstants(commandBuffer, m_VoxelPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(m_VoxelPushConstants), &m_VoxelPushConstants);

    vkCmdDispatch(commandBuffer, std::ceil(drawExtent.width / 16.0),
                  std::ceil(drawExtent.height / 16.0), 1);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool,
                        frameIndex * 2 + 1);

    renderImage.transition(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkExtent3D target = { .width = m_SwapchainImageExtent.width,
                          .height = m_SwapchainImageExtent.height,
                          .depth = 1 };

    Image::copyFromTo(commandBuffer, renderImage.getImage(), m_SwapchainImages[swapchainImageIndex],
                      renderImage.getExtent(), target);

    Image::transition(commandBuffer, m_SwapchainImages[swapchainImageIndex],
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    renderImGui(commandBuffer, m_SwapchainImageViews[swapchainImageIndex], m_SwapchainImageExtent);

    Image::transition(commandBuffer, m_SwapchainImages[swapchainImageIndex],
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

    static uint64_t timeQueryBuffer[2];
    VkResult result =
        vkGetQueryPoolResults(m_Device, m_QueryPool, frameIndex * 2, 2, sizeof(uint64_t) * 2,
                              timeQueryBuffer, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result == VK_NOT_READY)
    {
    }
    else if (result == VK_SUCCESS)
    {
        m_PreviousFrameTime = timeQueryBuffer[1] - timeQueryBuffer[0];
    }
    vkResetQueryPool(m_Device, m_QueryPool, frameIndex * 2, 2);

    currentFrameIndex++;
}
