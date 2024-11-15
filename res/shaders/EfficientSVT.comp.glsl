#version 460
#extension GL_EXT_shader_explicit_arithmetic_types : enable

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

#define MAX_ITERATIONS 10
#define MIN_T 0.
#define MAX_T 10000.

#include "Ray.other.glsl"

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout(rgba16f, set = 0, binding = 1) uniform image2D o_ComparisonImage;
layout(rgba16f, set = 0, binding = 2) readonly uniform image1D i_Lookup;

struct Node {
    uint16_t childPtr;
    uint8_t validMask;
    uint8_t leafMask;
};

layout(buffer_reference, std430) readonly buffer NodeBuffer {
    Node nodes[];
};

layout(push_constant) uniform constants {
    vec4 p_CameraPosition;
    vec4 p_CameraFront;
    vec4 p_CameraRight;
    vec4 p_CameraUp;
    uvec3 p_Dimensions;
    float p_Size;
    NodeBuffer p_Tree;
};

struct HitRecord {
    float t;
    vec3 position;
    vec3 normal;
    int parent;
    uint8_t materialIndex;
};

struct StackMember
{
    float tMax;
    uint parent;
    float scale;

    vec3 minBound;
};

vec3 calculatePosition(vec3 origin, vec3 direction, float t)
{
    return origin + direction * t;
}

HitRecord castRay(uint root, Ray ray) {
    const int sMax = 23;
    int currentStack = 0;
    StackMember stack[sMax + 1];

    HitRecord hit;
    hit.t = -1;
    hit.parent = -1;

    const float epsilon = 0.000001;

    vec3 origin = ray.origin;
    vec3 position = ray.origin;
    vec3 direction = ray.direction;

    uint parent = 0;

    vec3 dimensions = p_Dimensions * p_Size;

    vec3 minBound = vec3(0.);
    vec3 maxBound = minBound + dimensions;

    vec3 bias = direction * epsilon;

    float tMin, tMax;
    bool didHit = rayBoxIntersect(ray, minBound, maxBound, MIN_T, MAX_T, tMin, tMax);

    if (!didHit)
        return hit;

    float t = max(tMin, 0.);

    float scale = 0.5;

    position = calculatePosition(origin, ray.direction, tMin);

    Node node = p_Tree.nodes[parent];

    for (int i = 0; i < MAX_ITERATIONS; i++)
    {
        if (t >= tMax) // Ascend, Go up stack
        {
            if (currentStack == 0) break;

            StackMember member = stack[--currentStack];
            tMax = member.tMax;
            parent = member.parent;
            scale = member.scale;
            minBound = member.minBound;

            node = p_Tree.nodes[parent];
        }

        vec3 center = minBound + scale * dimensions;
        int octantMask = 0;
        if (position.x >= center.x) octantMask ^= 1;
        if (position.z >= center.z) octantMask ^= 2;
        if (position.y >= center.y) octantMask ^= 4;

        bool isValid = bool((node.validMask >> octantMask) & 1);
        bool isLeaf = bool((node.leafMask >> octantMask) & 1);

        if (isValid && isLeaf) // Solid Voxel
        {
            uint nodeIndex = parent + node.childPtr + bitCount(uint(node.validMask) >> (octantMask + 1));
            uint8_t materialIndex = p_Tree.nodes[nodeIndex].leafMask;

            hit.t = t;
            hit.position = calculatePosition(origin, direction, t);
            hit.parent = int(parent);
            hit.normal = vec3(1., 0., 0.);
            hit.materialIndex = materialIndex;
            return hit;
        }

        if (isValid && !isLeaf) // Parent Voxel, Add to stack
        {
            if (node.childPtr == 0)
                break;

            StackMember stackMember;
            stackMember.parent = parent;
            stackMember.tMax = tMax;
            stackMember.scale = scale;
            stackMember.minBound = minBound;

            stack[currentStack++] = stackMember;

            uint count = uint(node.validMask) >> (octantMask + 1);
            parent = parent + node.childPtr + bitCount(count);
            node = p_Tree.nodes[parent];

            if ((octantMask & 0x1) != 0)
                minBound.x += scale * dimensions.x;
            if ((octantMask & 0x2) != 0)
                minBound.z += scale * dimensions.z;
            if ((octantMask & 0x4) != 0)
                minBound.y += scale * dimensions.y;

            maxBound = minBound + scale * dimensions;

            if (!rayBoxIntersect(ray, minBound, maxBound, tMin, tMax, tMin, tMax)) break;

            node = p_Tree.nodes[parent];

            scale *= 0.5;
        }

        if (!isValid)
        {
            vec3 octantMinBound = minBound;
            if ((octantMask & 0x1) != 0)
                octantMinBound.x += scale * dimensions.x;
            if ((octantMask & 0x2) != 0)
                octantMinBound.z += scale * dimensions.z;
            if ((octantMask & 0x4) != 0)
                octantMinBound.y += scale * dimensions.y;

            vec3 octantMaxBound = octantMinBound + scale * dimensions;

            float t0, t1;
            if (!rayBoxIntersect(ray, octantMinBound, octantMaxBound, tMin, tMax, t0, t1)) break;

            t = t1;
            position = calculatePosition(origin, direction, t) + bias;
        }
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
            vec3(p_CameraPosition), vec3(p_CameraFront),
            vec3(p_CameraRight),
            vec3(p_CameraUp));

    HitRecord hit = castRay(0, ray);

    if (hit.t >= 0)
    {
        vec4 lookupColour = imageLoad(i_Lookup, int(hit.materialIndex));

        imageStore(o_Image, texelCoord, lookupColour);
    }
}
