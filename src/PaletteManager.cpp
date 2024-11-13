#include "PaletteManager.hpp"

#include <imgui.h>

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

    m_CurrentColour = 1;
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
    m_Mapping.erase(m_Mapping.find(previousColour));
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
                ImVec2 buttonSize(40, 40);

                for (int i = 0; i < 256; i++)
                {
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
                    float nextButtonX2 = lastButtonX2 + style.ItemSpacing.x + buttonSize.x;

                    if (i + 1 < 256 && nextButtonX2 < windowVisible && (i + 1) % 8 != 0)
                        ImGui::SameLine();

                    ImGui::PopID();
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
