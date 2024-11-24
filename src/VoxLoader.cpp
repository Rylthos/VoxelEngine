#include "VoxLoader.hpp"

#include <fstream>

#include <spdlog/spdlog.h>

#define MAX_LOG 9

typedef std::array<char, 5> VoxID;

VoxLoader::VoxLoader(SceneManager* sceneManager, PaletteManager* paletteManager)
    : m_SceneManager{ sceneManager }, m_PaletteManager{ paletteManager }
{
}

bool VoxLoader::loadModel(const char* name)
{
    spdlog::info("Loading Model: {}", name);

    std::ifstream inputFile(name, std::ios::in | std::ios::binary);
    if (!inputFile.is_open())
    {
        spdlog::error("Failed to open {}", name);
        return false;
    }

    VoxID mainID = readID(inputFile);
    if (strcmp(mainID.data(), "VOX ") != 0)
    {
        spdlog::error("Unexpected File Format: EXPECTED \"VOX \" GOT {}", mainID.data());
        return false;
    }

    uint32_t version = readU32(inputFile);

    return readChunk(inputFile);
}

bool VoxLoader::readChunk(std::ifstream& file)
{
    VoxID chunkID = readID(file);
    uint32_t numBytesChunk = readU32(file);
    uint32_t numBytesChildren = readU32(file);

    if (strcmp(chunkID.data(), "MAIN") == 0)
    {
        spdlog::info("Main | Chunk {} | Children {}", numBytesChunk, numBytesChildren);

        if (!readChunk(file)) return false; // Size
        if (!readChunk(file)) return false; // XYZI

        if (!file.eof()) return readChunk(file);
        return true;
    }
    else if (strcmp(chunkID.data(), "SIZE") == 0)
    {
        uint32_t x = readU32(file);
        uint32_t y = readU32(file);
        uint32_t z = readU32(file);

        uint32_t size = std::max(std::max(x, y), z);
        uint32_t closestLog = std::ceil(std::log2((float)size));
        if (closestLog > MAX_LOG)
        {
            spdlog::error("Model Too large");
        }
        size = 1 << closestLog;

        m_SceneManager->setDimensions(size);

        spdlog::info("SIZE | X: {} | Y: {} | Z: {}", x, y, z);
        return true;
    }
    else if (strcmp(chunkID.data(), "XYZI") == 0)
    {
        uint32_t numVoxels = readU32(file);

        spdlog::info("XYZI | Reading {} voxels", numVoxels);

        for (size_t i = 0; i < numVoxels; i++)
        {
            uint8_t x = readU8(file);
            uint8_t z = readU8(file);
            uint8_t y = m_SceneManager->getDimension() - readU8(file) - 1;
            uint8_t c = readU8(file);

            if (c == 255)
                m_SceneManager->setVoxel({ x, y, z }, { .colourIndex = -1 });
            else
                m_SceneManager->setVoxel({ x, y, z }, { .colourIndex = (int16_t)(c + 1) });
        }
        return true;
    }
    else if (strcmp(chunkID.data(), "RGBA") == 0)
    {
        for (int i = 0; i < 255; i++)
        {
            uint8_t R = readU8(file);
            uint8_t G = readU8(file);
            uint8_t B = readU8(file);
            uint8_t A = readU8(file);

            float r = R / 255.;
            float g = G / 255.;
            float b = B / 255.;
            float a = A / 255.;

            m_PaletteManager->setColourIndex(i, { r, g, b, a });
        }
        return true;
    }
    else if (strcmp(chunkID.data(), "PACK") == 0)
    {
        spdlog::error("Vox models with PAX not supported");
        return false;
    }
    else
    {
        spdlog::error("Unexpected ID : GOT {}", chunkID.data());
    }
    return false;
}

std::array<char, 5> VoxLoader::readID(std::ifstream& file)
{
    std::array<uint8_t, 4> bytes = readBytes<4>(file);
    std::array<char, 5> returnBytes;
    for (int i = 0; i < 4; i++)
        returnBytes[i] = bytes[i];
    returnBytes[5] = 0x0;

    return returnBytes;
}

uint32_t VoxLoader::readU32(std::ifstream& file)
{
    std::array<uint8_t, 4> bytes = readBytes<4>(file);
    uint32_t data = 0;
    for (size_t i = 0; i < 4; i++)
    {
        data |= bytes[i] << (8u * i);
    }

    return data;
}

uint8_t VoxLoader::readU8(std::ifstream& file) { return readBytes<1>(file).at(0); }
