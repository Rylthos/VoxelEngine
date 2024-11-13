#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <unordered_map>

#include "Image.hpp"

class PaletteManager
{
  public:
    PaletteManager();

    void flushColours();
    uint8_t getColourIndex(glm::vec4 colour);
    void setColourIndex(uint8_t index, glm::vec4 colour);
    uint8_t getEmptyIndex() { return 0; }

    void copyToBuffer(Buffer& image);

  private:
    size_t m_MaxColours;
    std::unordered_map<glm::vec4, size_t> m_Mapping;
    std::vector<glm::vec4> m_Colours;
    size_t m_CurrentColour = 1;

  private:
    uint8_t addColour(glm::vec4 colour);
};
