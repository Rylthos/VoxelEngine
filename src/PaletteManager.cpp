#include "PaletteManager.hpp"

PaletteManager::PaletteManager() { m_Colours.resize(256); }

void PaletteManager::flushColours()
{
    m_Mapping.clear();
    m_CurrentColour = 1;
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
}

void PaletteManager::copyToBuffer(Buffer& staging) { staging.copyFromData<glm::vec4>(m_Colours); }

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
    return index;
}
