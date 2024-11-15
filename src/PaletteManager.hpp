#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <unordered_map>

#include "Events.hpp"
#include "Image.hpp"

class PaletteManager : public EventReceiver
{
  public:
    PaletteManager();

    void initResources(VkDevice device, VmaAllocator allocator);
    void freeResources();

    void flushColours();
    uint8_t getColourIndex(glm::vec4 colour);
    void setColourIndex(uint8_t index, glm::vec4 colour);
    uint8_t getEmptyIndex() { return 0; }
    Image& getImage() { return m_LookupTexture; }

    void updateImage();

    void receive(const Event* event);

    void defaultPalette();

  private:
    size_t m_MaxColours;
    std::unordered_map<glm::vec4, size_t> m_Mapping;
    std::vector<glm::vec4> m_Colours;
    size_t m_CurrentColour = 0;

    Image m_LookupTexture;
    Buffer m_StagingBuffer;

    bool m_HasChanged = false;

  private:
    uint8_t addColour(glm::vec4 colour);
};
