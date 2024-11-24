#pragma once

#include <array>
#include <fstream>

#include "PaletteManager.hpp"
#include "SceneManager.hpp"

class VoxLoader
{
  public:
    VoxLoader(SceneManager* sceneManager, PaletteManager* paletteManager);

    void loadModel(const char* name);

  private:
    SceneManager* m_SceneManager;
    PaletteManager* m_PaletteManager;

  private:
    void readChunk(std::ifstream& file);

    template<int N>
    std::array<uint8_t, N> readBytes(std::ifstream& file)
    {
        std::array<uint8_t, N> bytes;
        std::array<char, N> data;
        file.read(data.data(), N);
        std::memcpy(bytes.data(), data.data(), N);
        return bytes;
    }

    std::array<char, 5> readID(std::ifstream& file);
    uint32_t readU32(std::ifstream& file);
    uint8_t readU8(std::ifstream& file);
};
