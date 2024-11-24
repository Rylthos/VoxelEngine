#include "PaletteManager.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

PaletteManager::PaletteManager() { m_Colours.resize(256); }

void PaletteManager::initResources(VkDevice device, VmaAllocator allocator)
{
    VkExtent3D lookupExtent = { 256, 1, 1 };
    m_LookupTexture.create(allocator, VK_FORMAT_R32G32B32A32_SFLOAT, lookupExtent, VK_IMAGE_TYPE_1D,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
                           VMA_MEMORY_USAGE_GPU_ONLY, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_LookupTexture.createImageView(device, VK_IMAGE_VIEW_TYPE_1D);

    m_StagingBuffer.create(allocator, 256 * sizeof(glm::vec4),
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VMA_MEMORY_USAGE_CPU_TO_GPU);
}

void PaletteManager::freeResources()
{
    m_LookupTexture.free();
    m_StagingBuffer.free();
}

void PaletteManager::flushColours()
{
    for (auto pair : m_Mapping)
    {
        m_Colours[pair.second] = glm::vec4(0.);
    }
    m_Mapping.clear();

    m_CurrentColour = 0;
    m_HasChanged = true;
}

uint8_t PaletteManager::getColourIndex(glm::vec4 colour)
{
    auto it = m_Mapping.find(colour);
    if (it == m_Mapping.end())
        return addColour(colour);
    else
        return it->second;
}

void PaletteManager::setColourIndex(uint8_t index, glm::vec4 colour)
{
    glm::vec4 previousColour = m_Colours[index];
    if (m_Mapping.find(previousColour) != m_Mapping.end())
    {
        m_Mapping.erase(m_Mapping.find(previousColour));
    }

    m_Mapping[colour] = index;
    m_Colours[index] = colour;
    m_HasChanged = true;
}

void PaletteManager::updateImage()
{
    if (!m_HasChanged) return;

    m_StagingBuffer.copyFromData_CPUOnly<glm::vec4>(m_Colours);

    ImmediateSubmit::submit(
        [&](VkCommandBuffer cmd) { m_LookupTexture.copyFromBuffer(cmd, m_StagingBuffer); });

    m_HasChanged = false;
}

std::array<float, 4> vec4ToArray(const glm::vec4& colour)
{
    std::array<float, 4> returnValue;
    returnValue[0] = colour.r;
    returnValue[1] = colour.g;
    returnValue[2] = colour.b;
    returnValue[3] = colour.a;
    return returnValue;
}

void arrayToVec4(std::array<float, 4> modifiedColour, glm::vec4& colour)
{
    colour.r = modifiedColour[0];
    colour.g = modifiedColour[1];
    colour.b = modifiedColour[2];
    colour.a = modifiedColour[3];
}

void PaletteManager::receive(const Event* event)
{
    switch (event->getType())
    {
    case EventType::ImGuiRender:
        {
            if (ImGui::Begin("Palette Manager"))
            {
                float windowVisible =
                    ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

                ImGuiStyle& style = ImGui::GetStyle();
                float buttonWidth = 20.0f;

                const int rows = 256 / 8;
                const int cols = 8;

                for (int r = rows - 1; r >= 0; --r)
                {
                    for (int c = 0; c < cols; ++c)
                    {
                        int i = r * cols + c;
                        ImGui::PushID(i);

                        std::array<float, 4> floatColour = vec4ToArray(m_Colours[i]);

                        if (ImGui::ColorEdit4("##Temp", floatColour.data(),
                                              ImGuiColorEditFlags_NoInputs |
                                                  ImGuiColorEditFlags_NoLabel))
                        {
                            arrayToVec4(floatColour, m_Colours[i]);
                            m_HasChanged = true;
                        }

                        float lastButtonX2 = ImGui::GetItemRectMax().x;
                        float nextButtonX2 = lastButtonX2 + style.ItemSpacing.x + buttonWidth;

                        if (c < cols - 1)
                        {
                            ImGui::SameLine();
                        }

                        ImGui::PopID();
                    }
                }
            }
            ImGui::End();

            if (m_HasChanged)
            {
                updateImage();
            }

            break;
        }
    default:
        break;
    }
}

void PaletteManager::defaultPalette()
{
    setColourIndex(0, { 1.0, 1.0, 1.0, 1.0 });
    setColourIndex(1, { 1.0, 1.0, 0.8, 1.0 });
    setColourIndex(2, { 1.0, 1.0, 0.6, 1.0 });
    setColourIndex(3, { 1.0, 1.0, 0.4, 1.0 });
    setColourIndex(4, { 1.0, 1.0, 0.2, 1.0 });
    setColourIndex(5, { 1.0, 1.0, 0.0, 1.0 });
    setColourIndex(6, { 1.0, 0.8, 1.0, 1.0 });
    setColourIndex(7, { 1.0, 0.8, 0.8, 1.0 });
    setColourIndex(8, { 1.0, 0.8, 0.6, 1.0 });
    setColourIndex(9, { 1.0, 0.8, 0.4, 1.0 });
    setColourIndex(10, { 1.0, 0.8, 0.2, 1.0 });
    setColourIndex(11, { 1.0, 0.8, 0.0, 1.0 });
    setColourIndex(12, { 1.0, 0.6, 1.0, 1.0 });
    setColourIndex(13, { 1.0, 0.6, 0.8, 1.0 });
    setColourIndex(14, { 1.0, 0.6, 0.6, 1.0 });
    setColourIndex(15, { 1.0, 0.6, 0.4, 1.0 });
    setColourIndex(16, { 1.0, 0.6, 0.2, 1.0 });
    setColourIndex(17, { 1.0, 0.6, 0.0, 1.0 });
    setColourIndex(18, { 1.0, 0.4, 1.0, 1.0 });
    setColourIndex(19, { 1.0, 0.4, 0.8, 1.0 });
    setColourIndex(20, { 1.0, 0.4, 0.6, 1.0 });
    setColourIndex(21, { 1.0, 0.4, 0.4, 1.0 });
    setColourIndex(22, { 1.0, 0.4, 0.2, 1.0 });
    setColourIndex(23, { 1.0, 0.4, 0.0, 1.0 });
    setColourIndex(24, { 1.0, 0.2, 1.0, 1.0 });
    setColourIndex(25, { 1.0, 0.2, 0.8, 1.0 });
    setColourIndex(26, { 1.0, 0.2, 0.6, 1.0 });
    setColourIndex(27, { 1.0, 0.2, 0.4, 1.0 });
    setColourIndex(28, { 1.0, 0.2, 0.2, 1.0 });
    setColourIndex(29, { 1.0, 0.2, 0.0, 1.0 });
    setColourIndex(30, { 1.0, 0.0, 1.0, 1.0 });
    setColourIndex(31, { 1.0, 0.0, 0.8, 1.0 });
    setColourIndex(32, { 1.0, 0.0, 0.6, 1.0 });
    setColourIndex(33, { 1.0, 0.0, 0.4, 1.0 });
    setColourIndex(34, { 1.0, 0.0, 0.2, 1.0 });
    setColourIndex(35, { 1.0, 0.0, 0.0, 1.0 });
    setColourIndex(36, { 0.8, 1.0, 1.0, 1.0 });
    setColourIndex(37, { 0.8, 1.0, 0.8, 1.0 });
    setColourIndex(38, { 0.8, 1.0, 0.6, 1.0 });
    setColourIndex(39, { 0.8, 1.0, 0.4, 1.0 });
    setColourIndex(40, { 0.8, 1.0, 0.2, 1.0 });
    setColourIndex(41, { 0.8, 1.0, 0.0, 1.0 });
    setColourIndex(42, { 0.8, 0.8, 1.0, 1.0 });
    setColourIndex(43, { 0.8, 0.8, 0.8, 1.0 });
    setColourIndex(44, { 0.8, 0.8, 0.6, 1.0 });
    setColourIndex(45, { 0.8, 0.8, 0.4, 1.0 });
    setColourIndex(46, { 0.8, 0.8, 0.2, 1.0 });
    setColourIndex(47, { 0.8, 0.8, 0.0, 1.0 });
    setColourIndex(48, { 0.8, 0.6, 1.0, 1.0 });
    setColourIndex(49, { 0.8, 0.6, 0.8, 1.0 });
    setColourIndex(50, { 0.8, 0.6, 0.6, 1.0 });
    setColourIndex(51, { 0.8, 0.6, 0.4, 1.0 });
    setColourIndex(52, { 0.8, 0.6, 0.2, 1.0 });
    setColourIndex(53, { 0.8, 0.6, 0.0, 1.0 });
    setColourIndex(54, { 0.8, 0.4, 1.0, 1.0 });
    setColourIndex(55, { 0.8, 0.4, 0.8, 1.0 });
    setColourIndex(56, { 0.8, 0.4, 0.6, 1.0 });
    setColourIndex(57, { 0.8, 0.4, 0.4, 1.0 });
    setColourIndex(58, { 0.8, 0.4, 0.2, 1.0 });
    setColourIndex(59, { 0.8, 0.4, 0.0, 1.0 });
    setColourIndex(60, { 0.8, 0.2, 1.0, 1.0 });
    setColourIndex(61, { 0.8, 0.2, 0.8, 1.0 });
    setColourIndex(62, { 0.8, 0.2, 0.6, 1.0 });
    setColourIndex(63, { 0.8, 0.2, 0.4, 1.0 });
    setColourIndex(64, { 0.8, 0.2, 0.2, 1.0 });
    setColourIndex(65, { 0.8, 0.2, 0.0, 1.0 });
    setColourIndex(66, { 0.8, 0.0, 1.0, 1.0 });
    setColourIndex(67, { 0.8, 0.0, 0.8, 1.0 });
    setColourIndex(68, { 0.8, 0.0, 0.6, 1.0 });
    setColourIndex(69, { 0.8, 0.0, 0.4, 1.0 });
    setColourIndex(70, { 0.8, 0.0, 0.2, 1.0 });
    setColourIndex(71, { 0.8, 0.0, 0.0, 1.0 });
    setColourIndex(72, { 0.6, 1.0, 1.0, 1.0 });
    setColourIndex(73, { 0.6, 1.0, 0.8, 1.0 });
    setColourIndex(74, { 0.6, 1.0, 0.6, 1.0 });
    setColourIndex(75, { 0.6, 1.0, 0.4, 1.0 });
    setColourIndex(76, { 0.6, 1.0, 0.2, 1.0 });
    setColourIndex(77, { 0.6, 1.0, 0.0, 1.0 });
    setColourIndex(78, { 0.6, 0.8, 1.0, 1.0 });
    setColourIndex(79, { 0.6, 0.8, 0.8, 1.0 });
    setColourIndex(80, { 0.6, 0.8, 0.6, 1.0 });
    setColourIndex(81, { 0.6, 0.8, 0.4, 1.0 });
    setColourIndex(82, { 0.6, 0.8, 0.2, 1.0 });
    setColourIndex(83, { 0.6, 0.8, 0.0, 1.0 });
    setColourIndex(84, { 0.6, 0.6, 1.0, 1.0 });
    setColourIndex(85, { 0.6, 0.6, 0.8, 1.0 });
    setColourIndex(86, { 0.6, 0.6, 0.6, 1.0 });
    setColourIndex(87, { 0.6, 0.6, 0.4, 1.0 });
    setColourIndex(88, { 0.6, 0.6, 0.2, 1.0 });
    setColourIndex(89, { 0.6, 0.6, 0.0, 1.0 });
    setColourIndex(90, { 0.6, 0.4, 1.0, 1.0 });
    setColourIndex(91, { 0.6, 0.4, 0.8, 1.0 });
    setColourIndex(92, { 0.6, 0.4, 0.6, 1.0 });
    setColourIndex(93, { 0.6, 0.4, 0.4, 1.0 });
    setColourIndex(94, { 0.6, 0.4, 0.2, 1.0 });
    setColourIndex(95, { 0.6, 0.4, 0.0, 1.0 });
    setColourIndex(96, { 0.6, 0.2, 1.0, 1.0 });
    setColourIndex(97, { 0.6, 0.2, 0.8, 1.0 });
    setColourIndex(98, { 0.6, 0.2, 0.6, 1.0 });
    setColourIndex(99, { 0.6, 0.2, 0.4, 1.0 });
    setColourIndex(100, { 0.6, 0.2, 0.2, 1.0 });
    setColourIndex(101, { 0.6, 0.2, 0.0, 1.0 });
    setColourIndex(102, { 0.6, 0.0, 1.0, 1.0 });
    setColourIndex(103, { 0.6, 0.0, 0.8, 1.0 });
    setColourIndex(104, { 0.6, 0.0, 0.6, 1.0 });
    setColourIndex(105, { 0.6, 0.0, 0.4, 1.0 });
    setColourIndex(106, { 0.6, 0.0, 0.2, 1.0 });
    setColourIndex(107, { 0.6, 0.0, 0.0, 1.0 });
    setColourIndex(108, { 0.4, 1.0, 1.0, 1.0 });
    setColourIndex(109, { 0.4, 1.0, 0.8, 1.0 });
    setColourIndex(110, { 0.4, 1.0, 0.6, 1.0 });
    setColourIndex(111, { 0.4, 1.0, 0.4, 1.0 });
    setColourIndex(112, { 0.4, 1.0, 0.2, 1.0 });
    setColourIndex(113, { 0.4, 1.0, 0.0, 1.0 });
    setColourIndex(114, { 0.4, 0.8, 1.0, 1.0 });
    setColourIndex(115, { 0.4, 0.8, 0.8, 1.0 });
    setColourIndex(116, { 0.4, 0.8, 0.6, 1.0 });
    setColourIndex(117, { 0.4, 0.8, 0.4, 1.0 });
    setColourIndex(118, { 0.4, 0.8, 0.2, 1.0 });
    setColourIndex(119, { 0.4, 0.8, 0.0, 1.0 });
    setColourIndex(120, { 0.4, 0.6, 1.0, 1.0 });
    setColourIndex(121, { 0.4, 0.6, 0.8, 1.0 });
    setColourIndex(122, { 0.4, 0.6, 0.6, 1.0 });
    setColourIndex(123, { 0.4, 0.6, 0.4, 1.0 });
    setColourIndex(124, { 0.4, 0.6, 0.2, 1.0 });
    setColourIndex(125, { 0.4, 0.6, 0.0, 1.0 });
    setColourIndex(126, { 0.4, 0.4, 1.0, 1.0 });
    setColourIndex(127, { 0.4, 0.4, 0.8, 1.0 });
    setColourIndex(128, { 0.4, 0.4, 0.6, 1.0 });
    setColourIndex(129, { 0.4, 0.4, 0.4, 1.0 });
    setColourIndex(130, { 0.4, 0.4, 0.2, 1.0 });
    setColourIndex(131, { 0.4, 0.4, 0.0, 1.0 });
    setColourIndex(132, { 0.4, 0.2, 1.0, 1.0 });
    setColourIndex(133, { 0.4, 0.2, 0.8, 1.0 });
    setColourIndex(134, { 0.4, 0.2, 0.6, 1.0 });
    setColourIndex(135, { 0.4, 0.2, 0.4, 1.0 });
    setColourIndex(136, { 0.4, 0.2, 0.2, 1.0 });
    setColourIndex(137, { 0.4, 0.2, 0.0, 1.0 });
    setColourIndex(138, { 0.4, 0.0, 1.0, 1.0 });
    setColourIndex(139, { 0.4, 0.0, 0.8, 1.0 });
    setColourIndex(140, { 0.4, 0.0, 0.6, 1.0 });
    setColourIndex(141, { 0.4, 0.0, 0.4, 1.0 });
    setColourIndex(142, { 0.4, 0.0, 0.2, 1.0 });
    setColourIndex(143, { 0.4, 0.0, 0.0, 1.0 });
    setColourIndex(144, { 0.2, 1.0, 1.0, 1.0 });
    setColourIndex(145, { 0.2, 1.0, 0.8, 1.0 });
    setColourIndex(146, { 0.2, 1.0, 0.6, 1.0 });
    setColourIndex(147, { 0.2, 1.0, 0.4, 1.0 });
    setColourIndex(148, { 0.2, 1.0, 0.2, 1.0 });
    setColourIndex(149, { 0.2, 1.0, 0.0, 1.0 });
    setColourIndex(150, { 0.2, 0.8, 1.0, 1.0 });
    setColourIndex(151, { 0.2, 0.8, 0.8, 1.0 });
    setColourIndex(152, { 0.2, 0.8, 0.6, 1.0 });
    setColourIndex(153, { 0.2, 0.8, 0.4, 1.0 });
    setColourIndex(154, { 0.2, 0.8, 0.2, 1.0 });
    setColourIndex(155, { 0.2, 0.8, 0.0, 1.0 });
    setColourIndex(156, { 0.2, 0.6, 1.0, 1.0 });
    setColourIndex(157, { 0.2, 0.6, 0.8, 1.0 });
    setColourIndex(158, { 0.2, 0.6, 0.6, 1.0 });
    setColourIndex(159, { 0.2, 0.6, 0.4, 1.0 });
    setColourIndex(160, { 0.2, 0.6, 0.2, 1.0 });
    setColourIndex(161, { 0.2, 0.6, 0.0, 1.0 });
    setColourIndex(162, { 0.2, 0.4, 1.0, 1.0 });
    setColourIndex(163, { 0.2, 0.4, 0.8, 1.0 });
    setColourIndex(164, { 0.2, 0.4, 0.6, 1.0 });
    setColourIndex(165, { 0.2, 0.4, 0.4, 1.0 });
    setColourIndex(166, { 0.2, 0.4, 0.2, 1.0 });
    setColourIndex(167, { 0.2, 0.4, 0.0, 1.0 });
    setColourIndex(168, { 0.2, 0.2, 1.0, 1.0 });
    setColourIndex(169, { 0.2, 0.2, 0.8, 1.0 });
    setColourIndex(170, { 0.2, 0.2, 0.6, 1.0 });
    setColourIndex(171, { 0.2, 0.2, 0.4, 1.0 });
    setColourIndex(172, { 0.2, 0.2, 0.2, 1.0 });
    setColourIndex(173, { 0.2, 0.2, 0.0, 1.0 });
    setColourIndex(174, { 0.2, 0.0, 1.0, 1.0 });
    setColourIndex(175, { 0.2, 0.0, 0.8, 1.0 });
    setColourIndex(176, { 0.2, 0.0, 0.6, 1.0 });
    setColourIndex(177, { 0.2, 0.0, 0.4, 1.0 });
    setColourIndex(178, { 0.2, 0.0, 0.2, 1.0 });
    setColourIndex(179, { 0.2, 0.0, 0.0, 1.0 });
    setColourIndex(180, { 0.0, 1.0, 1.0, 1.0 });
    setColourIndex(181, { 0.0, 1.0, 0.8, 1.0 });
    setColourIndex(182, { 0.0, 1.0, 0.6, 1.0 });
    setColourIndex(183, { 0.0, 1.0, 0.4, 1.0 });
    setColourIndex(184, { 0.0, 1.0, 0.2, 1.0 });
    setColourIndex(185, { 0.0, 1.0, 0.0, 1.0 });
    setColourIndex(186, { 0.0, 0.8, 1.0, 1.0 });
    setColourIndex(187, { 0.0, 0.8, 0.8, 1.0 });
    setColourIndex(188, { 0.0, 0.8, 0.6, 1.0 });
    setColourIndex(189, { 0.0, 0.8, 0.4, 1.0 });
    setColourIndex(190, { 0.0, 0.8, 0.2, 1.0 });
    setColourIndex(191, { 0.0, 0.8, 0.0, 1.0 });
    setColourIndex(192, { 0.0, 0.6, 1.0, 1.0 });
    setColourIndex(193, { 0.0, 0.6, 0.8, 1.0 });
    setColourIndex(194, { 0.0, 0.6, 0.6, 1.0 });
    setColourIndex(195, { 0.0, 0.6, 0.4, 1.0 });
    setColourIndex(196, { 0.0, 0.6, 0.2, 1.0 });
    setColourIndex(197, { 0.0, 0.6, 0.0, 1.0 });
    setColourIndex(198, { 0.0, 0.4, 1.0, 1.0 });
    setColourIndex(199, { 0.0, 0.4, 0.8, 1.0 });
    setColourIndex(200, { 0.0, 0.4, 0.6, 1.0 });
    setColourIndex(201, { 0.0, 0.4, 0.4, 1.0 });
    setColourIndex(202, { 0.0, 0.4, 0.2, 1.0 });
    setColourIndex(203, { 0.0, 0.4, 0.0, 1.0 });
    setColourIndex(204, { 0.0, 0.2, 1.0, 1.0 });
    setColourIndex(205, { 0.0, 0.2, 0.8, 1.0 });
    setColourIndex(206, { 0.0, 0.2, 0.6, 1.0 });
    setColourIndex(207, { 0.0, 0.2, 0.4, 1.0 });
    setColourIndex(208, { 0.0, 0.2, 0.2, 1.0 });
    setColourIndex(209, { 0.0, 0.2, 0.0, 1.0 });
    setColourIndex(210, { 0.0, 0.0, 1.0, 1.0 });
    setColourIndex(211, { 0.0, 0.0, 0.8, 1.0 });
    setColourIndex(212, { 0.0, 0.0, 0.6, 1.0 });
    setColourIndex(213, { 0.0, 0.0, 0.4, 1.0 });
    setColourIndex(214, { 0.0, 0.0, 0.2, 1.0 });
    setColourIndex(215, { 0.93, 0.0, 0.0, 1.0 });
    setColourIndex(216, { 0.87, 0.0, 0.0, 1.0 });
    setColourIndex(217, { 0.73, 0.0, 0.0, 1.0 });
    setColourIndex(218, { 0.67, 0.0, 0.0, 1.0 });
    setColourIndex(219, { 0.53, 0.0, 0.0, 1.0 });
    setColourIndex(220, { 0.47, 0.0, 0.0, 1.0 });
    setColourIndex(221, { 0.33, 0.0, 0.0, 1.0 });
    setColourIndex(222, { 0.27, 0.0, 0.0, 1.0 });
    setColourIndex(223, { 0.13, 0.0, 0.0, 1.0 });
    setColourIndex(224, { 0.07, 0.0, 0.0, 1.0 });
    setColourIndex(225, { 0.0, 0.93, 0.0, 1.0 });
    setColourIndex(226, { 0.0, 0.87, 0.0, 1.0 });
    setColourIndex(227, { 0.0, 0.73, 0.0, 1.0 });
    setColourIndex(228, { 0.0, 0.67, 0.0, 1.0 });
    setColourIndex(229, { 0.0, 0.53, 0.0, 1.0 });
    setColourIndex(230, { 0.0, 0.47, 0.0, 1.0 });
    setColourIndex(231, { 0.0, 0.33, 0.0, 1.0 });
    setColourIndex(232, { 0.0, 0.27, 0.0, 1.0 });
    setColourIndex(233, { 0.0, 0.13, 0.0, 1.0 });
    setColourIndex(234, { 0.0, 0.07, 0.0, 1.0 });
    setColourIndex(235, { 0.0, 0.0, 0.93, 1.0 });
    setColourIndex(236, { 0.0, 0.0, 0.87, 1.0 });
    setColourIndex(237, { 0.0, 0.0, 0.73, 1.0 });
    setColourIndex(238, { 0.0, 0.0, 0.67, 1.0 });
    setColourIndex(239, { 0.0, 0.0, 0.53, 1.0 });
    setColourIndex(240, { 0.0, 0.0, 0.47, 1.0 });
    setColourIndex(241, { 0.0, 0.0, 0.33, 1.0 });
    setColourIndex(242, { 0.0, 0.0, 0.27, 1.0 });
    setColourIndex(243, { 0.0, 0.0, 0.13, 1.0 });
    setColourIndex(244, { 0.0, 0.0, 0.07, 1.0 });
    setColourIndex(245, { 0.93, 0.93, 0.93, 1.0 });
    setColourIndex(246, { 0.87, 0.87, 0.87, 1.0 });
    setColourIndex(247, { 0.73, 0.73, 0.73, 1.0 });
    setColourIndex(248, { 0.67, 0.67, 0.67, 1.0 });
    setColourIndex(249, { 0.53, 0.53, 0.53, 1.0 });
    setColourIndex(250, { 0.47, 0.47, 0.47, 1.0 });
    setColourIndex(251, { 0.33, 0.33, 0.33, 1.0 });
    setColourIndex(252, { 0.27, 0.27, 0.27, 1.0 });
    setColourIndex(253, { 0.13, 0.13, 0.13, 1.0 });
    setColourIndex(254, { 0.07, 0.07, 0.07, 1.0 });
    setColourIndex(255, { 0.0, 0.0, 0.0, 0.0 });
}

uint8_t PaletteManager::addColour(glm::vec4 colour)
{
    size_t index = m_CurrentColour;
    if (index > 256)
    {
        throw std::runtime_error("Too many colours");
    }

    m_CurrentColour++;
    m_Mapping[colour] = index;
    m_Colours[index] = colour;
    m_HasChanged = true;
    return index;
}
