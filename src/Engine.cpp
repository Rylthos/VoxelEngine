#include "Engine.hpp"

#include "IntervalList.hpp"
#include "VkBootstrap.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <random>
#include <spdlog/fmt/ranges.h>

#include "Constants.hpp"
#include "Descriptors.hpp"
#include "PipelineBuilder.hpp"
#include "Profilling.hpp"
#include "ShaderModule.hpp"
#include "Timer.hpp"
#include "VkCheck.hpp"
#include "VoxLoader.hpp"

#include "Events.hpp"

#include <cmath>
#include <vulkan/vulkan_core.h>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

void Engine::init()
{
    IntervalList<int> list;
    list.addInterval(1, 4);
    list.addInterval(5);
    list.addInterval(6, 10);
    auto copy = list.getIntervals();
    auto size = list.totalFree();

    // exit(-1);
    spdlog::set_level(spdlog::level::trace);
    m_Window.create("Voxel Engine", 960, 960);

    m_Camera = Camera(glm::vec3(8.f, 8.f, -10.f), 0., 0.);

    // m_PaletteManager.defaultPalette();
    m_SceneManager = SceneManager(&m_PaletteManager, &m_Camera);

    initVulkan();
    initSwapchain();
    initCommandPool();
    ImmediateSubmit::init(m_Device, m_GraphicsQueue.queue, m_GraphicsQueue.queueFamily);
    initSyncStructures();
    initImGui();
    initSSAO();
    initDescriptorPool();
    initDescriptorLayouts();
    initPipelines();
    initDescriptorSets();
    initQueryPool();

#ifdef PROF_TRACY
    PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT
        myvkGetPhysicalDeviceCalibrateableTimeDomainsEXT
        = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(
            vkGetInstanceProcAddr(m_Instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"));

    PFN_vkGetCalibratedTimestampsEXT myvkGetCalibratedTimestampsEXT
        = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(
            vkGetInstanceProcAddr(m_Instance, "vkGetCalibratedTimestampsEXT"));

    g_TracyVkCtx = TracyVkContextHostCalibrated(m_PhysicalDevice, m_Device, vkResetQueryPool,

        myvkGetPhysicalDeviceCalibrateableTimeDomainsEXT, myvkGetCalibratedTimestampsEXT);
#endif

    m_SceneManager.initResources(m_Device, m_Allocator, &m_ComputeQueue);

    // m_PaletteManager.updateImage();

    EventHandler::subscribe(
        { EventType::KeyboardInput, EventType::ImGuiRender, EventType::WindowResize }, this);

    EventHandler::subscribe({ EventType::KeyboardInput, EventType::MouseMove, EventType::GameUpdate,
                                EventType::ImGuiRender },
        &m_Camera);

    EventHandler::subscribe({ EventType::GameUpdate, EventType::MouseButton, EventType::MouseScroll,
                                EventType::ImGuiRender },
        &m_SceneManager);
    // EventHandler::subscribe(EventType::ImGuiRender, &m_PaletteManager);

    m_DeferredPushConstants.lightColour = glm::vec4(1.);
    m_DeferredPushConstants.skyColour = glm::vec4(0.3, 0.73, 1., 1.);

    m_RenderAlt = false;
}

void Engine::start()
{
    float currentTime;
    float previousTime = glfwGetTime();

    while (!m_Window.shouldClose()) {
        if (m_ShouldResize)
            resizeWindow();

        currentTime = glfwGetTime();
        float frameDelta = currentTime - previousTime;
        previousTime = currentTime;

        m_Stats.frameDelta = frameDelta;

        m_Window.pollInput();

        update(frameDelta);

        render(frameDelta);

        {
            VkCommandBufferBeginInfo commandBufferBI {};
            commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            commandBufferBI.pNext = nullptr;
            commandBufferBI.pInheritanceInfo = nullptr;
            commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            Timer::startTimer("Render");
            VK_CHECK(vkResetCommandBuffer(m_TracyCommandBuffer, 0));

            VK_CHECK(vkBeginCommandBuffer(m_TracyCommandBuffer, &commandBufferBI));

            PROF_VK_COLLECT(m_TracyCommandBuffer);

            VK_CHECK(vkEndCommandBuffer(m_TracyCommandBuffer));
        }

        m_Window.swapBuffers();
        PROF_FRAME_MARK;
    }
}

void Engine::cleanup()
{
    std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk2(m_GraphicsQueue.queueMutex);
    std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk(m_ComputeQueue.queueMutex);

    vkDeviceWaitIdle(m_Device);

    m_SSAOSamples.free();
    m_SSAONoise.free();

#ifdef PROF_TRACY
    TracyVkDestroy(g_TracyVkCtx);
#endif

    m_SceneManager.freeResources();

    ImmediateSubmit::free();

    vkDestroyQueryPool(m_Device, m_QueryPool, nullptr);

    vkDestroyPipeline(m_Device, m_DeferredPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_DeferredPipelineLayout, nullptr);

    vkDestroyPipeline(m_Device, m_SSAOBlurPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_SSAOBlurPipelineLayout, nullptr);

    vkDestroyPipeline(m_Device, m_SSAOPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_SSAOPipelineLayout, nullptr);

    vkDestroyPipeline(m_Device, m_VoxelPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_VoxelPipelineLayout, nullptr);

    vkDestroyDescriptorSetLayout(m_Device, m_GBufferDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, m_NoiseDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_Device, m_AltDescriptorSetLayout, nullptr);

    vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDestroyDescriptorPool(m_Device, m_ImguiPool, nullptr);

    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        vkDestroyFence(m_Device, m_Frames[i].renderFence, nullptr);
        vkDestroySemaphore(m_Device, m_Frames[i].renderSemaphore, nullptr);
        vkDestroySemaphore(m_Device, m_Frames[i].swapchainSemaphore, nullptr);
    }

    vkDestroyCommandPool(m_Device, m_TracyCommandPool, nullptr);
    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        vkDestroyCommandPool(m_Device, m_Frames[i].commandPool, nullptr);
    }

    m_SceneManager.freeResources();
    // m_PaletteManager.freeResources();

    destroySwapchain();

    vmaDestroyAllocator(m_Allocator);
    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger, nullptr);
    vkDestroyInstance(m_Instance, nullptr);
}

void Engine::receive(const Event* event)
{
    switch (event->getType()) {
    case EventType::KeyboardInput: {
        const KeyboardInput* ki = reinterpret_cast<const KeyboardInput*>(event);

        if (ki->key == GLFW_KEY_M && ki->action == GLFW_PRESS)
            m_RenderImGui = !m_RenderImGui;

        if (ki->key == GLFW_KEY_RIGHT_CONTROL && ki->action == GLFW_PRESS)
            m_RenderAlt = !m_RenderAlt;

        break;
    }
    case EventType::ImGuiRender: {
        updateImGui();
        break;
    }
    case EventType::WindowResize: {
        m_ShouldResize = true;
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

    if (!instRet) {
        spdlog::error("Failed to create Instance: {}", instRet.error().message());
        exit(-1);
    }

    vkb::Instance vkbInst = instRet.value();
    m_Instance = vkbInst.instance;
    m_DebugMessenger = vkbInst.debug_messenger;
    m_Surface = m_Window.createSurface(m_Instance);
    spdlog::info("Created Window Surface");

    VkPhysicalDeviceVulkan13Features features13 {};
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12 {};
    features12.bufferDeviceAddress = true;
    features12.shaderBufferInt64Atomics = true;
    features12.descriptorIndexing = true;
    features12.hostQueryReset = true;
    features12.shaderInt8 = true;
    features12.storageBuffer8BitAccess = true;
    features12.runtimeDescriptorArray = true;

    VkPhysicalDeviceVulkan11Features features11 {};
    features11.shaderDrawParameters = true;
    features11.storageBuffer16BitAccess = true;

    VkPhysicalDeviceFeatures features {};
    features.robustBufferAccess = true;
    features.fragmentStoresAndAtomics = true;
    features.imageCubeArray = true;
    features.geometryShader = true;
    features.shaderInt16 = true;
    features.shaderInt64 = true;

    vkb::PhysicalDeviceSelector selector { vkbInst };
    auto vkbMaybeDevice
        = selector.set_minimum_version(1, 3)
              .set_required_features_13(features13)
              .set_required_features_12(features12)
              .set_required_features_11(features11)
              .set_required_features_11(features11)
              .set_required_features(features)
              .add_required_extension(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME)
              .add_required_extension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)
              .add_required_extension(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME)
              .set_surface(m_Surface)
              .select();

    if (!vkbMaybeDevice.has_value()) {
        spdlog::error("{}: {}", vkbMaybeDevice.error().value(), vkbMaybeDevice.error().message());
        exit(-1);
    }

    vkb::PhysicalDevice vkbPhysicalDevice = vkbMaybeDevice.value();

    vkb::DeviceBuilder deviceBuilder { vkbPhysicalDevice };

    vkb::Device vkbDevice = deviceBuilder.build().value();

    m_PhysicalDevice = vkbPhysicalDevice.physical_device;
    m_Device = vkbDevice.device;
    spdlog::info("Created Devices");

    m_GraphicsQueue.queue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_GraphicsQueue.queueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    spdlog::info("Created Graphics Queue: {}", m_GraphicsQueue.queueFamily);

    m_ComputeQueue.queue = vkbDevice.get_queue(vkb::QueueType::compute).value();
    m_ComputeQueue.queueFamily = vkbDevice.get_queue_index(vkb::QueueType::compute).value();
    spdlog::info("Created Compute Queue: {}", m_ComputeQueue.queueFamily);

    VmaAllocatorCreateInfo allocatorCI {};
    allocatorCI.physicalDevice = m_PhysicalDevice;
    allocatorCI.device = m_Device;
    allocatorCI.instance = m_Instance;
    allocatorCI.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorCI, &m_Allocator);
    spdlog::info("Created Allocator");
}

void Engine::createSwapchain()
{
    vkb::SwapchainBuilder swapchainBuilder { m_PhysicalDevice, m_Device, m_Surface };
    m_SwapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain
        = swapchainBuilder
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

    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageUsageFlags imageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    m_GBuffer.position.create(m_Allocator, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageExtent,
        imageType, imageFlags, VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_GBuffer.normal.create(m_Allocator, VK_FORMAT_R8G8B8A8_SINT, drawImageExtent, imageType,
        imageFlags, VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_GBuffer.colour.create(m_Allocator, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageExtent, imageType,
        imageFlags, VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_GBuffer.occlusion.create(m_Allocator, VK_FORMAT_R32_SFLOAT, drawImageExtent, imageType,
        imageFlags, VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_SSAOBlurTemp.create(m_Allocator, VK_FORMAT_R32_SFLOAT, drawImageExtent, imageType, imageFlags,
        VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_DrawImage.create(m_Allocator, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageExtent, imageType,
        imageFlags, VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_AltImage.create(m_Allocator, VK_FORMAT_R16G16B16A16_SFLOAT, m_DrawImage.getExtent(),
        imageType, imageFlags, VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_GBuffer.position.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);
    m_GBuffer.normal.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);
    m_GBuffer.colour.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);
    m_GBuffer.occlusion.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);
    m_SSAOBlurTemp.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);
    m_DrawImage.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);
    m_AltImage.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);

    spdlog::info("Createed Swapchain ImageView");
}

void Engine::destroySwapchain()
{
    m_AltImage.free();
    m_DrawImage.free();

    m_SSAOBlurTemp.free();
    m_GBuffer.occlusion.free();
    m_GBuffer.colour.free();
    m_GBuffer.normal.free();
    m_GBuffer.position.free();

    vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);

    for (size_t i = 0; i < m_SwapchainImageViews.size(); i++) {
        vkDestroyImageView(m_Device, m_SwapchainImageViews[i], nullptr);
    }
    spdlog::info("Destroyed Swapchain");
}

void Engine::initCommandPool()
{
    VkCommandPoolCreateInfo commandPoolCI {};
    commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCI.pNext = nullptr;
    commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCI.queueFamilyIndex = m_GraphicsQueue.queueFamily;

    VkCommandBufferAllocateInfo commandBufferAI {};
    commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAI.pNext = nullptr;
    commandBufferAI.commandBufferCount = 1;
    commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    m_Frames.resize(FRAMES_IN_FLIGHT);
    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VK_CHECK(vkCreateCommandPool(m_Device, &commandPoolCI, nullptr, &m_Frames[i].commandPool));
        spdlog::info("Created Frame Command Pool: {}", i);

        commandBufferAI.commandPool = m_Frames[i].commandPool;
        VK_CHECK(vkAllocateCommandBuffers(m_Device, &commandBufferAI, &m_Frames[i].commandBuffer));
        spdlog::info("Allocated Command Buffer: {}", i);
    }

    VK_CHECK(vkCreateCommandPool(m_Device, &commandPoolCI, nullptr, &m_TracyCommandPool));

    commandBufferAI.commandPool = m_TracyCommandPool;
    VK_CHECK(vkAllocateCommandBuffers(m_Device, &commandBufferAI, &m_TracyCommandBuffer));
}

void Engine::initSyncStructures()
{
    VkFenceCreateInfo fenceCI {};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.pNext = nullptr;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo semaphoreCI {};
    semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCI.pNext = nullptr;

    for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
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

    VkDescriptorPoolCreateInfo poolCI {};
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

    VkPipelineRenderingCreateInfoKHR pipelineCI {};
    pipelineCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineCI.pNext = nullptr;
    pipelineCI.colorAttachmentCount = 1;
    pipelineCI.pColorAttachmentFormats = &colourFormat;

    ImGui_ImplVulkan_InitInfo vulkanII {};
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

void Engine::initSSAO()
{
    VkExtent3D extent = { .width = 4, .height = 4, .depth = 1 };
    m_SSAONoise.create(m_Allocator, VK_FORMAT_R32G32B32A32_SFLOAT, extent, VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_SSAONoise.createImageView(m_Device, VK_IMAGE_VIEW_TYPE_2D);
    m_SSAONoise.createImageSampler(m_Device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    m_SSAOSamples.create(m_Allocator, 64 * sizeof(glm::vec4),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine generator;
    std::vector<glm::vec4> samples;
    for (size_t i = 0; i < m_SSAOKernelSize; i++) {
        glm::vec3 sample = { randomFloats(generator) * 2.0 - 1.0,
            randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) };
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);
        float scale = (float)i / 64.;
        scale = 0.1f + 0.9 * scale * scale; // lerp(0.1f, 1.0f, scale * scale);
        sample *= scale;
        samples.push_back(glm::vec4(sample, 1.));
    }

    m_SSAOSamples.copyFromData<glm::vec4>(samples);

    std::vector<glm::vec4> ssaoNoise;
    for (unsigned int i = 0; i < 16; i++) {
        glm::vec4 noise(
            randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 1.0f, 1.);
        ssaoNoise.push_back(noise);
    }

    Buffer temp;
    temp.create(m_Allocator, ssaoNoise.size() * sizeof(glm::vec4), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

    temp.copyFromData_CPUOnly<glm::vec4>(ssaoNoise);
    ImmediateSubmit::submit(
        [&](VkCommandBuffer buffer) { m_SSAONoise.copyFromBuffer(buffer, temp); });
    temp.free();

    m_SSAOPushConstants.radius = 1.0;
    m_SSAOPushConstants.bias = 0.01;

    m_SSAOBlurPushConstants.blurSize = 2;
}

void Engine::initDescriptorPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         .descriptorCount = FRAMES_IN_FLIGHT },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 6                },
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1                }
    };

    VkDescriptorPoolCreateInfo descriptorPoolCI {};
    descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCI.pNext = nullptr;
    descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCI.pPoolSizes = poolSizes.data();
    descriptorPoolCI.maxSets = FRAMES_IN_FLIGHT + 4;
    descriptorPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    VK_CHECK(vkCreateDescriptorPool(m_Device, &descriptorPoolCI, nullptr, &m_DescriptorPool));
    spdlog::info("Created descriptor pool");
}

void Engine::initDescriptorLayouts()
{
    m_GBufferDescriptorSetLayout = DescriptorLayoutBuilder::start(m_Device)
                                       .addStorageImage(0, VK_SHADER_STAGE_COMPUTE_BIT)
                                       .addStorageImage(1, VK_SHADER_STAGE_COMPUTE_BIT)
                                       .addStorageImage(2, VK_SHADER_STAGE_COMPUTE_BIT)
                                       .addStorageImage(3, VK_SHADER_STAGE_COMPUTE_BIT)
                                       .build();

    m_NoiseDescriptorSetLayout = DescriptorLayoutBuilder::start(m_Device)
                                     .addCombinedImageSampler(0, VK_SHADER_STAGE_COMPUTE_BIT)
                                     .build();

    m_AltDescriptorSetLayout = DescriptorLayoutBuilder::start(m_Device)
                                   .addStorageImage(0, VK_SHADER_STAGE_COMPUTE_BIT)
                                   .build();

    spdlog::info("Created descriptor layouts");
}

void Engine::initPipelines()
{
    {
        VkPushConstantRange pushConstant {};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(VoxelPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        std::vector<VkDescriptorSetLayout> layouts
            = { m_GBufferDescriptorSetLayout, m_AltDescriptorSetLayout };
        VkPipelineLayoutCreateInfo computeLayoutCI {};
        computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayoutCI.pNext = nullptr;
        computeLayoutCI.setLayoutCount = layouts.size();
        computeLayoutCI.pSetLayouts = layouts.data();
        computeLayoutCI.pushConstantRangeCount = 1;
        computeLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(
            vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr, &m_VoxelPipelineLayout));

        ShaderModule voxelShader;
        voxelShader.create("res/shaders/Brickmap.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI {};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = voxelShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI {};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_VoxelPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(
            m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr, &m_VoxelPipeline));
        spdlog::info("Created Geometry Pipeline and Pipeline Layout");
    }

    {
        VkPushConstantRange pushConstant {};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(SSAOPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkSpecializationMapEntry entry;
        entry.constantID = 0;
        entry.offset = 0;
        entry.size = sizeof(m_SSAOKernelSize);
        VkSpecializationInfo spec_info;

        spec_info.mapEntryCount = 1;
        spec_info.pMapEntries = &entry;
        spec_info.dataSize = sizeof(m_SSAOKernelSize);
        spec_info.pData = &m_SSAOKernelSize;

        std::vector<VkDescriptorSetLayout> layouts
            = { m_GBufferDescriptorSetLayout, m_NoiseDescriptorSetLayout };
        VkPipelineLayoutCreateInfo computeLayoutCI {};
        computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayoutCI.pNext = nullptr;
        computeLayoutCI.setLayoutCount = layouts.size();
        computeLayoutCI.pSetLayouts = layouts.data();
        computeLayoutCI.pushConstantRangeCount = 1;
        computeLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(
            vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr, &m_SSAOPipelineLayout));

        ShaderModule ssaoShader;
        ssaoShader.create("res/shaders/SSAO.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI {};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = ssaoShader.getShaderModule();
        shaderStageCI.pName = "main";
        shaderStageCI.pSpecializationInfo = &spec_info;

        VkComputePipelineCreateInfo computePipelineCI {};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_SSAOPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(
            m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr, &m_SSAOPipeline));
        spdlog::info("Created SSAO Pipeline and Pipeline Layout");
    }

    {
        VkPushConstantRange pushConstant {};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(SSAOBlurPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        std::vector<VkDescriptorSetLayout> layouts
            = { m_GBufferDescriptorSetLayout, m_AltDescriptorSetLayout };
        VkPipelineLayoutCreateInfo computeLayoutCI {};
        computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayoutCI.pNext = nullptr;
        computeLayoutCI.setLayoutCount = layouts.size();
        computeLayoutCI.pSetLayouts = layouts.data();
        computeLayoutCI.pushConstantRangeCount = 1;
        computeLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(
            vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr, &m_SSAOBlurPipelineLayout));

        ShaderModule ssaoShader;
        ssaoShader.create("res/shaders/SSAOBlur.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI {};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = ssaoShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI {};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_SSAOBlurPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(
            m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr, &m_SSAOBlurPipeline));
        spdlog::info("Created SSAO Blur Pipeline and Pipeline Layout");
    }

    {
        VkPushConstantRange pushConstant {};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(DeferredPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        std::vector<VkDescriptorSetLayout> layouts
            = { m_GBufferDescriptorSetLayout, m_AltDescriptorSetLayout };
        VkPipelineLayoutCreateInfo computeLayoutCI {};
        computeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayoutCI.pNext = nullptr;
        computeLayoutCI.setLayoutCount = layouts.size();
        computeLayoutCI.pSetLayouts = layouts.data();
        computeLayoutCI.pushConstantRangeCount = 1;
        computeLayoutCI.pPushConstantRanges = &pushConstant;

        VK_CHECK(
            vkCreatePipelineLayout(m_Device, &computeLayoutCI, nullptr, &m_DeferredPipelineLayout));

        ShaderModule deferredShader;
        deferredShader.create("res/shaders/Deferred.comp.spv", m_Device);

        VkPipelineShaderStageCreateInfo shaderStageCI {};
        shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCI.pNext = nullptr;
        shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageCI.module = deferredShader.getShaderModule();
        shaderStageCI.pName = "main";

        VkComputePipelineCreateInfo computePipelineCI {};
        computePipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCI.pNext = nullptr;
        computePipelineCI.layout = m_DeferredPipelineLayout;
        computePipelineCI.stage = shaderStageCI;

        VK_CHECK(vkCreateComputePipelines(
            m_Device, VK_NULL_HANDLE, 1, &computePipelineCI, nullptr, &m_DeferredPipeline));
        spdlog::info("Created Deferred Pipeline and Pipeline Layout");
    }
}

void Engine::initDescriptorSets()
{
    m_GBufferDescriptorSet
        = DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_GBufferDescriptorSetLayout)
              .addStorageImage(0, VK_IMAGE_LAYOUT_GENERAL, m_GBuffer.position.getImageView())
              .addStorageImage(1, VK_IMAGE_LAYOUT_GENERAL, m_GBuffer.normal.getImageView())
              .addStorageImage(2, VK_IMAGE_LAYOUT_GENERAL, m_GBuffer.colour.getImageView())
              .addStorageImage(3, VK_IMAGE_LAYOUT_GENERAL, m_GBuffer.occlusion.getImageView())
              .build()
              .at(0);

    m_NoiseDescriptorSet
        = DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_NoiseDescriptorSetLayout)
              .addCombinedImageSampler(
                  0, VK_IMAGE_LAYOUT_GENERAL, m_SSAONoise.getImageView(), m_SSAONoise.getSampler())
              .build()
              .at(0);

    m_DrawImageDescriptorSet
        = DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_AltDescriptorSetLayout)
              .addStorageImage(0, VK_IMAGE_LAYOUT_GENERAL, m_DrawImage.getImageView())
              .build()
              .at(0);

    m_AltImageDescriptorSet
        = DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_AltDescriptorSetLayout)
              .addStorageImage(0, VK_IMAGE_LAYOUT_GENERAL, m_AltImage.getImageView())
              .build()
              .at(0);

    m_SSAOBlurImageDescriptorSet
        = DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_AltDescriptorSetLayout)
              .addStorageImage(0, VK_IMAGE_LAYOUT_GENERAL, m_SSAOBlurTemp.getImageView())
              .build()
              .at(0);

    spdlog::info("Created descriptors");
}

void Engine::recreateDescriptorSets()
{
    spdlog::info("Recreating Descriptor Sets");
    std::vector<VkDescriptorSet> descriptorSets = { m_GBufferDescriptorSet, m_NoiseDescriptorSet,
        m_DrawImageDescriptorSet, m_SSAOBlurImageDescriptorSet, m_AltImageDescriptorSet };
    vkFreeDescriptorSets(m_Device, m_DescriptorPool, descriptorSets.size(), descriptorSets.data());
    initDescriptorSets();
}

void Engine::initQueryPool()
{
    VkQueryPoolCreateInfo queryPoolCI {};
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

void Engine::resizeWindow()
{
    spdlog::info("Resizing | W: {} H: {}", m_Window.getSize().x, m_Window.getSize().y);

    std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk2(m_GraphicsQueue.queueMutex);
    std::unique_lock<PROF_LOCKABLE_BASE(std::mutex)> lk(m_ComputeQueue.queueMutex);

    vkDeviceWaitIdle(m_Device);

    destroySwapchain();
    initSwapchain();
    recreateDescriptorSets();

    m_ShouldResize = false;
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
    if (currentFrame > 0) {
        for (size_t i = 0; i < currentFrame; i++) {
            avgTime += frameTimes[i];
            maxTime = fmin(maxTime, frameTimes[i]);
            minTime = fmax(minTime, frameTimes[i]);

            if (currentFrame == frameTimes.size() - 1) {
                frameTimes[i] = frameTimes[i + 1];
            }
        }
        avgTime += frameTimes[currentFrame];
        maxTime = fmin(maxTime, frameTimes[currentFrame]);
        minTime = fmax(minTime, frameTimes[currentFrame]);
        avgTime /= (float)currentFrame;
    }

    if (currentFrame + 1 < frameTimes.size()) {
        currentFrame += 1;
    }

    if (ImGui::Begin("Stats")) {
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

    if (ImGui::Begin("Memory")) {
        static VmaTotalStatistics stats {};

        if (ImGui::Button("Refresh memory stats")) {
            vmaCalculateStatistics(m_Allocator, &stats);
        }

        ImGui::Text("Block Count: %d", stats.total.statistics.blockCount);
        ImGui::Text("Allocation Count: %d", stats.total.statistics.allocationCount);
        ImGui::Text("Block Bytes: %ld B", stats.total.statistics.blockBytes);
        ImGui::Text("           : %.2f kB", stats.total.statistics.blockBytes / 1024.f);
        ImGui::Text("           : %.2f mB", stats.total.statistics.blockBytes / (1024.f * 1024.f));
    }
    ImGui::End();

    if (ImGui::Begin("Time")) {

        ImGui::Text("Time");
        ImGui::Checkbox("Increase Time", &m_IncreaseTime);
        ImGui::SliderFloat("##Time", &m_Time, 0., 2399, "%.2f");
        ImGui::Text("Sun Direction: (%.2f, %.2f, %.2f)", m_DeferredPushConstants.sunDirection.x,
            m_DeferredPushConstants.sunDirection.y, m_DeferredPushConstants.sunDirection.z);

        ImGui::Text("Minutes Per Second");
        ImGui::SliderFloat("##MinutesPerSecond", &m_MinutePerSecond, 1., 60., "%.2f");

        {
            glm::vec4& colour = m_DeferredPushConstants.lightColour;
            float data[] = { colour.r, colour.g, colour.b };
            ImGui::Text("Light Colour");
            ImGui::ColorEdit3("Light Colour", (float*)&data,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            colour.r = data[0];
            colour.g = data[1];
            colour.b = data[2];
        }

        {
            glm::vec4& colour = m_DeferredPushConstants.skyColour;
            float data[] = { colour.r, colour.g, colour.b };
            ImGui::Text("Sky Colour");
            ImGui::ColorEdit3("Sky Colour", (float*)&data,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            colour.r = data[0];
            colour.g = data[1];
            colour.b = data[2];
        }
    }
    ImGui::End();

    if (ImGui::Begin("SSAO")) {
        ImGui::Checkbox("SSAO", &m_SSAOEnabled);
        ImGui::Checkbox("SSAO Blur", &m_SSAOBlurEnabled);

        ImGui::Text("Radius");
        ImGui::SliderFloat("##Radius", &m_SSAOPushConstants.radius, 0.0, 5.);

        ImGui::Text("Bias");
        ImGui::SliderFloat("##Bias", &m_SSAOPushConstants.bias, 0.0, 0.5);

        ImGui::Text("Blur Radius");
        ImGui::SliderInt("##BlurRadius", &m_SSAOBlurPushConstants.blurSize, 0, 5);
    }
    ImGui::End();

    Timer::ImGuiRender();

    ImGui::ShowDemoWindow();
}

void Engine::update(float frameDelta)
{
    PROF_ZONE_SCOPED;
    Timer::startTimer("Update");
    GameUpdate update;
    update.frameDelta = frameDelta;
    EventHandler::dispatchEvent(&update);

    if (m_IncreaseTime)
        m_Time = fmod(m_Time + frameDelta * m_MinutePerSecond, 2400.);

    float angle = glm::pi<float>() * ((m_Time / 1200.) + 0.5);
    m_DeferredPushConstants.sunDirection
        = glm::normalize(glm::vec4(-glm::cos(angle), glm::sin(angle), 0., 0.));

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();

    ImGuiRender imGuiRender;
    EventHandler::dispatchEvent(&imGuiRender);

    ImGui::Render();
    Timer::stopTimer("Update");
}

void Engine::renderImGui(VkCommandBuffer& commandBuffer, VkImageView targetView, VkExtent2D extent)
{
    VkRenderingAttachmentInfo colorAI {};
    colorAI.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAI.pNext = nullptr;
    colorAI.imageView = targetView;
    colorAI.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAI.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAI.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderInfo {};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.pNext = nullptr;
    renderInfo.flags = 0;
    renderInfo.renderArea = VkRect2D({ 0, 0 }, extent);
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAI;
    renderInfo.pDepthAttachment = nullptr;
    renderInfo.pStencilAttachment = nullptr;

    if (m_RenderImGui) {
        vkCmdBeginRendering(commandBuffer, &renderInfo);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

        vkCmdEndRendering(commandBuffer);
    }
}

void Engine::render(float frameDelta)
{
    PROF_ZONE_SCOPED;
    Timer::startTimer("Render");

    static uint32_t currentFrameIndex = 0;
    int frameIndex = currentFrameIndex % FRAMES_IN_FLIGHT;
    FrameData& currentFrame = m_Frames[frameIndex];

    VK_CHECK(vkWaitForFences(m_Device, 1, &currentFrame.renderFence, true, 1000000000));

    VK_CHECK(vkResetFences(m_Device, 1, &currentFrame.renderFence));

    uint32_t swapchainImageIndex;
    {
        VkResult result = vkAcquireNextImageKHR(m_Device, m_Swapchain, 1000000000,
            currentFrame.swapchainSemaphore, nullptr, &swapchainImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
            m_ShouldResize = true;
    }

    VkCommandBuffer commandBuffer = currentFrame.commandBuffer;
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    VkExtent2D drawExtent;
    drawExtent.width = m_DrawImage.getExtent().width;
    drawExtent.height = m_DrawImage.getExtent().height;

    VkCommandBufferBeginInfo commandBufferBI {};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    Image& renderImage = m_RenderAlt ? m_AltImage : m_DrawImage;

    VkExtent3D dispatchSize = { .width = (uint32_t)std::ceil(drawExtent.width / 16.0),
        .height = (uint32_t)std::ceil(drawExtent.height / 16.0),
        .depth = 1

    };

    VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT };

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBI));
    {
        PROF_VK_ZONE(commandBuffer, "VkRender");

        vkCmdResetQueryPool(commandBuffer, m_QueryPool, frameIndex * 2, 2);

        m_GBuffer.position.transition(
            commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        m_GBuffer.normal.transition(
            commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        m_GBuffer.colour.transition(
            commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        m_GBuffer.occlusion.transition(
            commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        m_SSAOBlurTemp.transition(
            commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        m_DrawImage.transition(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        m_AltImage.transition(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        Image::transition(commandBuffer, m_SwapchainImages[swapchainImageIndex],
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdWriteTimestamp(
            commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_QueryPool, frameIndex * 2);

        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_VoxelPipeline);

            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                m_VoxelPipelineLayout, 0, 1, &m_GBufferDescriptorSet, 0, nullptr);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                m_VoxelPipelineLayout, 1, 1, &m_AltImageDescriptorSet, 0, nullptr);

            VoxelPushConstants pushConstants = m_SceneManager.getVoxelPushConstants(frameIndex);
            pushConstants.sunDirection = m_DeferredPushConstants.sunDirection;
            pushConstants.cameraPosition = m_Camera.getPosition();
            glm::uvec2 windowSize = m_Window.getSize();
            pushConstants.aspectRatio = (float)windowSize.x / (float)windowSize.y;
            pushConstants.cameraForward = glm::vec4(m_Camera.getForward(), 1.0);
            pushConstants.cameraRight = glm::vec4(m_Camera.getRight(), 1.0);
            pushConstants.cameraUp = glm::vec4(m_Camera.getUp(), 1.0);

            vkCmdPushConstants(commandBuffer, m_VoxelPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                sizeof(pushConstants), &pushConstants);

            vkCmdDispatch(
                commandBuffer, dispatchSize.width, dispatchSize.height, dispatchSize.depth);
        }

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

        if (!m_RenderAlt) {
            if (m_SSAOEnabled) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_SSAOPipeline);

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_SSAOPipelineLayout, 0, 1, &m_GBufferDescriptorSet, 0, nullptr);

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_SSAOPipelineLayout, 1, 1, &m_NoiseDescriptorSet, 0, nullptr);

                m_SSAOPushConstants.cameraFront = glm::vec4(m_Camera.getForward(), 1.);
                m_SSAOPushConstants.cameraRight = glm::vec4(m_Camera.getRight(), 1.);
                m_SSAOPushConstants.cameraUp = glm::vec4(m_Camera.getUp(), 1.);
                m_SSAOPushConstants.samples = m_SSAOSamples.getDeviceAddress(m_Device);

                vkCmdPushConstants(commandBuffer, m_SSAOPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                    0, sizeof(SSAOPushConstants), &m_SSAOPushConstants);

                vkCmdDispatch(
                    commandBuffer, dispatchSize.width, dispatchSize.height, dispatchSize.depth);

                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            }

            if (m_SSAOEnabled && m_SSAOBlurEnabled) {
                vkCmdBindPipeline(
                    commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_SSAOBlurPipeline);

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_SSAOBlurPipelineLayout, 0, 1, &m_GBufferDescriptorSet, 0, nullptr);

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_SSAOBlurPipelineLayout, 1, 1, &m_SSAOBlurImageDescriptorSet, 0, nullptr);

                m_SSAOBlurPushConstants.axis = 0;

                vkCmdPushConstants(commandBuffer, m_SSAOBlurPipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SSAOBlurPushConstants),
                    &m_SSAOBlurPushConstants);

                vkCmdDispatch(
                    commandBuffer, dispatchSize.width, dispatchSize.height, dispatchSize.depth);

                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

                m_SSAOBlurPushConstants.axis = 1;

                vkCmdPushConstants(commandBuffer, m_SSAOBlurPipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SSAOBlurPushConstants),
                    &m_SSAOBlurPushConstants);

                vkCmdDispatch(
                    commandBuffer, dispatchSize.width, dispatchSize.height, dispatchSize.depth);

                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            }

            {
                vkCmdBindPipeline(
                    commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_DeferredPipeline);

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_DeferredPipelineLayout, 0, 1, &m_GBufferDescriptorSet, 0, nullptr);

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_DeferredPipelineLayout, 1, 1, &m_DrawImageDescriptorSet, 0, nullptr);

                vkCmdPushConstants(commandBuffer, m_DeferredPipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DeferredPushConstants),
                    &m_DeferredPushConstants);

                vkCmdDispatch(
                    commandBuffer, dispatchSize.width, dispatchSize.height, dispatchSize.depth);
            }
        }

        vkCmdWriteTimestamp(
            commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, frameIndex * 2 + 1);

        renderImage.transition(
            commandBuffer, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkExtent3D target = { .width = m_SwapchainImageExtent.width,
            .height = m_SwapchainImageExtent.height,
            .depth = 1 };

        Image::copyFromTo(commandBuffer, renderImage.getImage(),
            m_SwapchainImages[swapchainImageIndex], renderImage.getExtent(), target);

        Image::transition(commandBuffer, m_SwapchainImages[swapchainImageIndex],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        renderImGui(
            commandBuffer, m_SwapchainImageViews[swapchainImageIndex], m_SwapchainImageExtent);

        Image::transition(commandBuffer, m_SwapchainImages[swapchainImageIndex],
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandBufferSubmitInfo commandBufferSI {};
    commandBufferSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandBufferSI.pNext = nullptr;
    commandBufferSI.commandBuffer = commandBuffer;
    commandBufferSI.deviceMask = 0;

    VkSemaphoreSubmitInfo waitSI {};
    waitSI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSI.pNext = nullptr;
    waitSI.semaphore = currentFrame.swapchainSemaphore;
    waitSI.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
    waitSI.deviceIndex = 0;
    waitSI.value = 1;

    VkSemaphoreSubmitInfo signalSI {};
    signalSI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSI.pNext = nullptr;
    signalSI.semaphore = currentFrame.renderSemaphore;
    signalSI.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    signalSI.deviceIndex = 0;
    signalSI.value = 1;

    VkSubmitInfo2 submit {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.pNext = nullptr;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitSI;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalSI;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandBufferSI;

    VK_CHECK(vkQueueSubmit2(m_GraphicsQueue.queue, 1, &submit, currentFrame.renderFence));

    VkPresentInfoKHR presentInfo {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pWaitSemaphores = &currentFrame.renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &swapchainImageIndex;

    {
        VkResult result = vkQueuePresentKHR(m_GraphicsQueue.queue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            m_ShouldResize = true;
    }

    static uint64_t timeQueryBuffer[2];
    VkResult result = vkGetQueryPoolResults(m_Device, m_QueryPool, frameIndex * 2, 2,
        sizeof(uint64_t) * 2, timeQueryBuffer, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

    if (result == VK_NOT_READY) {
    } else if (result == VK_SUCCESS) {
        m_PreviousFrameTime = timeQueryBuffer[1] - timeQueryBuffer[0];
    }
    vkResetQueryPool(m_Device, m_QueryPool, frameIndex * 2, 2);

    currentFrameIndex++;

    Timer::stopTimer("Render");
}
