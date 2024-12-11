#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

#define BRICK_SIZE 8
#define BRICK_GRID_SIZE 16

#include "Ray.other.glsl"

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout(rgba16f, set = 0, binding = 1) uniform image2D o_ComparisonImage;
layout(rgba16f, set = 0, binding = 2) readonly uniform image1D i_Lookup;

struct Brick {
    uint64_t solidMask[8];
    uint8_t colourPointer;
    uint8_t lodColour;
    uint16_t _;
};

layout(buffer_reference, std430) readonly buffer BrickBuffer {
    Brick bricks[];
};

#define BRICK_GRID_IS_VALID_BIT 0x1

#define LOADED_BRICK_FLAGS_OFFSET 0x1
#define LOADED_BRICK_FLAGS_BITMASK 0x7

#define LOADED_BRICK_FLAG_EMPTY 0x1

#define LOADED_BRICK_POINTER_OFFSET 0x4
#define LOADED_BRICK_POINTER_BITMASK 0xFFF

#define LOADED_BRICK_LOD_OFFSET 0x10
#define LOADED_BRICK_LOD_BITMASK 0xFF

struct BrickGrid {
    // Empty/Loaded: UNUSED: 8 | LOD: 8 | Pointer: 12 | Flags: 3 | 1
    // Flags: Rendered | Empty

    // Unloaded:     LOD: 8 | LOD: 8 | LOD:     12 | Flags: 3 | 0
    // Flags: NOT_LOADED | REQUESTED

    uint32_t data[16 * 16 * 16];
    BrickBuffer bricksBuffer;
};

layout(buffer_reference, std430) readonly buffer BrickGridBuffer {
    BrickGrid grid;
};

layout(push_constant) uniform constants {
    vec3 p_CameraPosition;
    float p_AspectRatio;

    vec3 p_CameraFront;
    int _1;

    vec3 p_CameraRight;
    int _2;

    vec3 p_CameraUp;
    int _3;

    uint32_t _4;
    float p_Size;
    uint32_t p_MaxDepthShown;
    uint32_t p_LOD;

    uint32_t p_MaxHeatShown;
    uint32_t p_Flags;
    uint32_t p_MaxIterations;
    uint32_t p_InitialParent;

    BrickGridBuffer p_BrickGrid;
};

struct HitRecord {
    float t;
    ivec3 brickHitIndex;
    ivec3 gridHitIndex;
    vec3 temp;
    int comparisons;
};

HitRecord emptyHit()
{
    HitRecord hit;
    hit.t = -1;
    hit.comparisons = -1;
    return hit;
}

int getsign(float f)
{
    if (f < 0)
        return -1;
    else
        return 1;
}

ivec3 dir_sign(vec3 v)
{
    return ivec3(getsign(v.x), getsign(v.y), getsign(v.z));
}

void traverseBrick(Ray ray, uint32_t pointer, vec3 minBound, inout int iterations, inout HitRecord hit)
{
    Brick brick = p_BrickGrid.grid.bricksBuffer.bricks[pointer];

    const vec3 maxBound = minBound + vec3(BRICK_SIZE);

    float tMin, tMax;
    bool intersectGrid = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000000.0, tMin, tMax);

    if (!intersectGrid) {
        hit.t = -1;
        return;
    }

    vec3 invDir = ray.invDir;
    if (isinf(invDir.x)) invDir.x = 0.;
    if (isinf(invDir.y)) invDir.y = 0.;
    if (isinf(invDir.z)) invDir.z = 0.;

    vec3 rayStart = ray.origin + ray.direction * max(tMin + 0.0001, 0.);

    vec3 entryPos = (rayStart - minBound) / 1.;

    ivec3 brickIndex = clamp(ivec3(entryPos), ivec3(0), ivec3(BRICK_SIZE));
    ivec3 stepDirection = dir_sign(ray.direction);
    vec3 stepSize = invDir * stepDirection;
    vec3 nextDist = (brickIndex - entryPos + max(stepDirection, 0)) * invDir;

    int count = 0;
    for (; iterations < p_MaxIterations; iterations++)
    {
        hit.comparisons++;
        count++;

        int y = brickIndex.y;
        int bitMask = brickIndex.z * BRICK_SIZE
                + brickIndex.x;

        if (((brick.solidMask[y] >> bitMask) & 0x1) == 1)
        {
            hit.brickHitIndex = brickIndex;
            hit.temp = brickIndex;
            hit.t = 1;
            return;
        }

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        nextDist += stepSize * stepAxis;
        brickIndex += stepDirection * stepAxis;

        bvec3 lower = lessThan(brickIndex, ivec3(0));
        bvec3 higher = greaterThanEqual(brickIndex, ivec3(BRICK_SIZE));
        if (lower.x || lower.y || lower.z || higher.x || higher.y || higher.z)
            break;
    }

    hit.t = -1;
    return;
}

HitRecord traverseBrickGrid(Ray ray)
{
    HitRecord hit = emptyHit();

    float tMin, tMax;
    const vec3 minBound = vec3(0);
    const vec3 maxBound = vec3(BRICK_GRID_SIZE * BRICK_SIZE);
    bool intersectGrid = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000000.0, tMin, tMax);

    if (!intersectGrid) return hit;

    hit.comparisons = 0;

    vec3 invDir = ray.invDir;
    if (isinf(invDir.x)) invDir.x = 0.;
    if (isinf(invDir.y)) invDir.y = 0.;
    if (isinf(invDir.z)) invDir.z = 0.;

    vec3 rayStart = ray.origin + ray.direction * max(tMin + 0.0001, 0);
    vec3 rayEnd = ray.origin + ray.direction * tMax;

    vec3 entryPos = (rayStart - minBound) / BRICK_SIZE;

    ivec3 brickGridIndex = clamp(ivec3(entryPos), ivec3(0), ivec3(BRICK_SIZE));
    ivec3 stepDirection = dir_sign(ray.direction);
    vec3 stepSize = invDir * stepDirection;
    vec3 nextDist = (brickGridIndex - entryPos + max(stepDirection, 0)) * invDir;

    for (int iterations = 0; iterations < p_MaxIterations; iterations++)
    {
        hit.comparisons++;

        bvec3 lower = lessThan(brickGridIndex, vec3(0));
        bvec3 higher = greaterThanEqual(brickGridIndex, vec3(BRICK_GRID_SIZE));
        if (lower.x || lower.y || lower.z || higher.x || higher.y || higher.z)
            break;

        int brickIndex = brickGridIndex.y * BRICK_GRID_SIZE * BRICK_GRID_SIZE
                + brickGridIndex.z * BRICK_GRID_SIZE
                + brickGridIndex.x;

        uint32_t brickData = p_BrickGrid.grid.data[brickIndex];
        if ((brickData & BRICK_GRID_IS_VALID_BIT) != 1) {
            // Unloaded
            // Needs to be added to load queue
        } else { // Brick is already loaded
            uint32_t flags = (brickData >> LOADED_BRICK_FLAGS_OFFSET) & LOADED_BRICK_FLAGS_BITMASK;

            uint32_t pointer = (brickData >> LOADED_BRICK_FLAGS_OFFSET) & LOADED_BRICK_POINTER_BITMASK;

            if ((flags & LOADED_BRICK_FLAG_EMPTY) == 0) // Not Empty
            {
                vec3 brickMinBound = brickGridIndex * BRICK_SIZE;

                traverseBrick(ray, pointer, brickMinBound, iterations, hit);

                if (hit.t >= 0) {
                    hit.gridHitIndex = brickGridIndex;
                    return hit;
                }
            }
        }

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        brickGridIndex += stepDirection * stepAxis;
        nextDist += stepSize * stepAxis;
    }

    hit.t = -1;
    return hit;
}

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Image);
    vec2 uv = vec2(texelCoord) / vec2(size);

    const vec3 clearColour = vec3(0.1);
    imageStore(o_ComparisonImage, texelCoord, vec4(clearColour, 0.0));
    imageStore(o_Image, texelCoord, vec4(clearColour, 0.0));

    Ray ray = generateRay(uv,
            p_CameraPosition, vec3(p_CameraFront),
            vec3(p_CameraRight),
            vec3(p_CameraUp), p_AspectRatio);

    ivec3 brickIndex;
    HitRecord hit = traverseBrickGrid(ray);

    if (hit.t >= 0) {
        // imageStore(o_Image, texelCoord, vec4(hit.t));
        // imageStore(o_Image, texelCoord, vec4(1.));
        imageStore(o_Image, texelCoord, vec4(hit.temp, hit.t));
    }

    if (hit.comparisons >= 0) {
        vec4 lowestHitColour = vec4(0.5, 0., 0.5, 1.0);
        vec4 highestHitColour = vec4(1., 1., 0., 1.0);
        float mixAmount = hit.comparisons / float(p_MaxHeatShown);

        imageStore(o_ComparisonImage, texelCoord, mix(lowestHitColour, highestHitColour, mixAmount));
    }
}
