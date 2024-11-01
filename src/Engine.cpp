#include "Engine.hpp"

#include "VkBootstrap.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "Descriptors.hpp"
#include "PipelineBuilder.hpp"
#include "ShaderModule.hpp"
#include "VkCheck.hpp"

#include "Events.hpp"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

void Engine::init()
{
    m_Window.create("Voxel Engine", 500, 500);

    initVulkan();
    initSwapchain();
    initCommandPool();
    ImmediateSubmit::init(m_Device, m_GraphicsQueue.queue, m_GraphicsQueue.queueFamily);
    initSyncStructures();
    initImGui();
    initVoxelBuffer();
    initDescriptorPool();
    initDescriptorLayouts();
    initPipelines();
    initDescriptorSets();

    m_Camera = Camera(glm::vec3(8.0f, 8.0f, -10.0f));

    EventHandler::subscribe(
        { EventType::KeyboardInput, EventType::MouseMove, EventType::GameUpdate }, &m_Camera);
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

    m_VoxelBuffer.free();
    vkDestroyPipeline(m_Device, m_VoxelPipeline, nullptr);
    vkDestroyPipelineLayout(m_Device, m_VoxelPipelineLayout, nullptr);

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

void Engine::initVoxelBuffer()
{
    std::vector<Voxel> voxels(VOXEL_SIZE * VOXEL_SIZE * VOXEL_SIZE);
    for (uint32_t y = 0; y < VOXEL_SIZE; y++)
    {
        for (uint32_t z = 0; z < VOXEL_SIZE; z++)
        {
            for (uint32_t x = 0; x < VOXEL_SIZE; x++)
            {

                uint32_t layerSum = x + z;
                uint32_t sum = x + y + z;
                uint32_t layerIndex = z * VOXEL_SIZE + x;
                uint32_t index = layerIndex + y * VOXEL_SIZE * VOXEL_SIZE;

                glm::vec4 colour = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

                if (layerSum % 2 == 0)
                {
                    if (sum % 2 == 0)
                    {
                        colour = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    }
                    else
                    {
                        colour = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
                    }
                }
                else
                {
                    if (sum % 2 == 0)
                    {
                        colour = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
                    }
                    else
                    {
                        colour = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
                    }
                }

                voxels.at(index) = { .colour = colour };
            }
        }
    }
    Buffer staging;
    staging.create(m_Allocator, voxels.size() * sizeof(Voxel),
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VMA_MEMORY_USAGE_CPU_COPY);

    staging.copyFromData<Voxel>(voxels);

    m_VoxelBuffer.create(m_Allocator, voxels.size() * sizeof(Voxel),
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                         VMA_MEMORY_USAGE_GPU_ONLY);

    m_VoxelBuffer.copyFromBuffer(staging, voxels.size() * sizeof(Voxel));
    m_TotalVoxels = voxels.size();
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
    m_VoxelDescriptorSetLayout = DescriptorLayoutBuilder::start(m_Device)
                                     .addStorageImage(0, VK_SHADER_STAGE_COMPUTE_BIT)
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
        voxelShader.create("res/shaders/basic_voxel_raytracer.comp.spv", m_Device);

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
}

void Engine::initDescriptorSets()
{
    m_VoxelDescriptorSet =
        DescriptorSetBuilder::start(m_Device, m_DescriptorPool, m_VoxelDescriptorSetLayout)
            .addStorageImage(0, VK_IMAGE_LAYOUT_GENERAL, m_DrawImage.getImageView())
            .build()
            .at(0);

    spdlog::info("Created descriptors");
}

void Engine::update(float frameDelta)
{
    GameUpdate update;
    update.frameDelta = frameDelta;
    EventHandler::dispatchEvent(&update);

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
    drawExtent.width = m_DrawImage.getExtent().width;
    drawExtent.height = m_DrawImage.getExtent().height;

    VkCommandBufferBeginInfo commandBufferBI{};
    commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBI.pNext = nullptr;
    commandBufferBI.pInheritanceInfo = nullptr;
    commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBI));

    m_DrawImage.transition(commandBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    Image::transition(commandBuffer, m_SwapchainImages[swapchainImageIndex],
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_VoxelPipeline);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_VoxelPipelineLayout, 0,
                            1, &m_VoxelDescriptorSet, 0, nullptr);

    VoxelPushConstants pushConstants;
    pushConstants.cameraPosition = m_Camera.getPosition();
    pushConstants.cameraForward = m_Camera.getForward();
    pushConstants.cameraRight = m_Camera.getRight();
    pushConstants.cameraUp = m_Camera.getUp();

    pushConstants.size = 1.0f;

    pushConstants.dimensions = { VOXEL_SIZE, VOXEL_SIZE, VOXEL_SIZE };
    pushConstants.voxelAddress = m_VoxelBuffer.getDeviceAddress(m_Device);

    vkCmdPushConstants(commandBuffer, m_VoxelPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(pushConstants), &pushConstants);

    vkCmdDispatch(commandBuffer, std::ceil(drawExtent.width / 16.0),
                  std::ceil(drawExtent.height / 16.0), 1);

    m_DrawImage.transition(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkExtent3D target = { .width = m_SwapchainImageExtent.width,
                          .height = m_SwapchainImageExtent.height,
                          .depth = 1 };

    Image::copyFromTo(commandBuffer, m_DrawImage.getImage(), m_SwapchainImages[swapchainImageIndex],
                      m_DrawImage.getExtent(), target);

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

    currentFrameIndex++;
}
