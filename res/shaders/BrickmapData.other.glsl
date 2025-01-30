#define VOXEL_SIZE 0.125
#define BRICK_SIZE 8
#define SUPER_BRICK_SIZE 16
#define CHUNK_SIZE 16

#define SUPER_BRICK_IS_LOADED_OFFSET 0
#define SUPER_BRICK_IS_LOADED_SIZE 1

#define SUPER_BRICK_FLAG_SIZE 1
#define SUPER_BRICK_REQUESTED_FLAG_OFFSET 1
#define SUPER_BRICK_IS_EMPTY_FLAG_OFFSET 2

#define SUPER_BRICK_POINTER_OFFSET 4
#define SUPER_BRICK_POINTER_SIZE 12

struct Brick {
    uint64_t solidMask[8];
    uint32_t colourPointer;
    uint8_t lodR;
    uint8_t lodG;
    uint8_t lodB;
    uint8_t _;
};

layout(buffer_reference, std430) readonly buffer BrickBuffer {
    Brick bricks[];
};

layout(buffer_reference, std430) readonly buffer ColourBuffers {
    vec4 colours[];
};

struct SuperBrick {
    // Empty/Loaded: UNUSED: 8 | LOD: 8 | Pointer: 12 | Flags: 3 | 1
    // Unloaded:     LOD: 8 | LOD: 8 | LOD:     12 | Flags: 3 | 0

    uint32_t data[16 * 16 * 16];
    BrickBuffer bricksBuffer;
    ColourBuffers colourBuffers;
};

struct Loaded {
    ivec3 superBrickIndex;
    bool loadSuperBrick;
    ivec3 brickIndex;
    bool loadBrick;
};

layout(buffer_reference, std430) buffer ToBeLoadedBuffer {
    uint32_t maxSize;
    uint32_t currentPointer;
    Loaded toBeLoaded[];
};

layout(buffer_reference, std430) buffer SuperBrickBuffer {
    SuperBrick superBrick[];
};

struct Chunk {
    // Empty/Loaded: UNUSED: 8 | LOD: 8 | Pointer: 12 | Flags: 3 | 1
    // Unloaded:     LOD: 8 | LOD: 8 | LOD:     12 | Flags: 3 | 0

    uint32_t data[16 * 16 * 16];
    SuperBrickBuffer superBricks;
};

layout(buffer_reference, std430) buffer ChunkBuffer {
    Chunk chunks;
};
