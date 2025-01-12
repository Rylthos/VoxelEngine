#define BRICK_SIZE 8
#define SUPER_BRICK_SIZE 16

#define SUPER_BRICK_IS_VALID_BIT 0x1

#define SUPER_BRICK_FLAGS_OFFSET 0x1
#define SUPER_BRICK_FLAGS_BITMASK 0x7

#define LOADED_BRICK_FLAG_EMPTY 0x1

#define UNLOADED_BRICK_FLAG_REQUESTED 0x1

#define LOADED_BRICK_POINTER_OFFSET 0x4
#define LOADED_BRICK_POINTER_BITMASK 0xFFF

#define LOADED_BRICK_LOD_OFFSET 0x10
#define LOADED_BRICK_LOD_BITMASK 0xFF

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

layout(buffer_reference, std430) readonly buffer ColourBuffer {
    vec4 colours[];
};

layout(buffer_reference, std430) readonly buffer ColourBuffers {
    ColourBuffer colour[];
};

struct SuperBrick {
    // Empty/Loaded: UNUSED: 8 | LOD: 8 | Pointer: 12 | Flags: 3 | 1
    // Flags: Empty

    // Unloaded:     LOD: 8 | LOD: 8 | LOD:     12 | Flags: 3 | 0
    // Flags: REQUESTED

    uint32_t data[16 * 16 * 16];
    BrickBuffer bricksBuffer;
    ColourBuffers colourBuffers;
};

layout(buffer_reference, std430) buffer ToBeLoadedBuffer {
    uint32_t maxSize;
    uint32_t currentPointer;
    uint32_t toBeLoaded[];
};

layout(buffer_reference, std430) buffer SuperBrickBuffer {
    SuperBrick superBrick;
};
