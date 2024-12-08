#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

#define MIN_T 0.000001
#define MAX_T 10000.

#include "Ray.other.glsl"

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout(rgba16f, set = 0, binding = 1) uniform image2D o_ComparisonImage;
layout(rgba16f, set = 0, binding = 2) readonly uniform image1D i_Lookup;

struct Brickmap {
    uint64_t solidMask[8];
    uint32_t colourPointer;
    uint32_t lodColour;
};

layout(buffer_reference, std430) readonly buffer BrickBuffer {
    Brickmap brick;
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

    BrickBuffer p_Brick;
};

struct HitRecord {
    float t;
    ivec3 hitIndex;
    int comparisons;
};

HitRecord emptyHit()
{
    HitRecord hit;
    hit.t = -1;
    hit.comparisons = -1;
    return hit;
}

HitRecord traverseBrick(Ray ray, Brickmap brick)
{
    HitRecord hit = emptyHit();

    float tMin, tMax;
    const vec3 minBound = vec3(0);
    const vec3 maxBound = vec3(8);
    bool intersectGrid = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000000.0, tMin, tMax);

    if (!intersectGrid) return hit;

    hit.comparisons = 0;

    vec3 invDir = ray.invDir;
    if (isinf(invDir.x)) invDir.x = 0.;
    if (isinf(invDir.y)) invDir.y = 0.;
    if (isinf(invDir.z)) invDir.z = 0.;

    vec3 rayStart = ray.origin + ray.direction * max(tMin, 0);
    vec3 rayEnd = ray.origin + ray.direction * tMax;

    ivec3 brickIndex = ivec3(max(vec3(0.), floor(rayStart - minBound)));
    brickIndex = clamp(brickIndex, ivec3(0), ivec3(8 - 1));

    ivec3 stepDirection = clamp(ivec3(sign(ray.direction)), ivec3(-1), ivec3(1));
    vec3 stepSize = vec3(invDir * stepDirection);
    vec3 nextDist = abs((brickIndex + max(stepDirection, 0) - ray.origin) * ray.invDir);

    ivec3 endIndex = ivec3(max(vec3(0.), floor(rayEnd - minBound)));
    endIndex = clamp(endIndex, ivec3(0), ivec3(8 - 1));
    endIndex += stepDirection;

    for (int i = 0; i < p_MaxIterations; i++)
    {
        hit.comparisons++;

        bvec3 lower = lessThan(brickIndex, ivec3(0));
        bvec3 higher = greaterThanEqual(brickIndex, ivec3(8));
        if (lower.x || lower.y || lower.z || higher.x || higher.y || higher.z)
            break;

        int index = brickIndex.y * 64 + brickIndex.z * 8 + brickIndex.x;

        int y = brickIndex.y;
        int bitMask = brickIndex.z * 8 + brickIndex.x;

        if (((brick.solidMask[y] >> bitMask) & 0x1) == 1)
        {
            hit.hitIndex = brickIndex;
            hit.t = 1;
            return hit;
        }

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        brickIndex += stepDirection * stepAxis;
        nextDist += stepSize * stepAxis;
        // normal = -stepDirection * stepAxis;
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
    HitRecord hit = traverseBrick(ray, p_Brick.brick);

    if (hit.t > 0) {
        imageStore(o_Image, texelCoord, vec4(1.));
    }

    if (hit.comparisons >= 0) {
        vec4 lowestHitColour = vec4(0.5, 0., 0.5, 1.0);
        vec4 highestHitColour = vec4(1., 1., 0., 1.0);
        float mixAmount = hit.comparisons / float(p_MaxHeatShown);

        imageStore(o_ComparisonImage, texelCoord, mix(lowestHitColour, highestHitColour, mixAmount));
    }
}
