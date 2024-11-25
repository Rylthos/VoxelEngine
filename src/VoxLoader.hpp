#pragma once

#include <array>
#include <fstream>

#include "Chunk.hpp"
#include "PaletteManager.hpp"

class VoxLoader
{
  public:
    VoxLoader(Chunk* chunk, PaletteManager* paletteManager);

    bool loadModel(const char* name);

  private:
    Chunk* m_Chunk;
    PaletteManager* m_PaletteManager;

  private:
    bool readChunk(std::ifstream& file);

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
