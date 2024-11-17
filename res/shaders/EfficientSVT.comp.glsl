#version 460
#extension GL_EXT_shader_explicit_arithmetic_types : enable

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

#define MAX_ITERATIONS 256
#define MIN_T 0.
#define MAX_T 10000.

#include "Ray.other.glsl"

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout(rgba16f, set = 0, binding = 1) uniform image2D o_ComparisonImage;
layout(rgba16f, set = 0, binding = 2) readonly uniform image1D i_Lookup;

struct Node {
    uint32_t childPtr;
    uint8_t unused;
    uint8_t materialIndex;
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
    uint parent;
    int depth;
    int deepest;
    uint8_t materialIndex;
};

struct StackMember
{
    float tMax;
    uint parent;
    vec3 minBound;
};

vec3 calculatePosition(vec3 origin, vec3 direction, float t)
{
    return origin + t * direction;
}

HitRecord castRay(uint root, Ray ray) {
    const int sMax = 10;
    // const float epsilon = exp2(-sMax);

    const vec3 origin = ray.origin;
    const vec3 direction = ray.direction;

    const vec3 dimensions = p_Dimensions * p_Size;
    const vec3 bias = direction * 0.001;

    vec3 position = ray.origin;

    uint parent = 0;

    vec3 minBound = vec3(0.);
    vec3 maxBound = minBound + dimensions;

    int currentStack = -1;
    StackMember stack[sMax + 1];

    HitRecord hit;
    hit.t = -2;
    hit.deepest = -1;

    float tMin, tMax;
    if (!rayBoxIntersect(ray, minBound, maxBound, MIN_T, MAX_T, tMin, tMax)) return hit;

    float t = max(tMin, 0.);

    float scale = 0.5;

    position = calculatePosition(origin, ray.direction, tMin);
    hit.position = position;

    Node node = p_Tree.nodes[parent];

    for (int i = 0; i < MAX_ITERATIONS; i++)
    {
        hit.deepest = (currentStack + 1 > hit.deepest) ? currentStack + 1 : hit.deepest;

        if (t >= tMax) // Ascend, Go up stack
        {
            if (currentStack == -1) break;

            StackMember member = stack[currentStack];
            currentStack--;

            tMax = member.tMax;
            parent = member.parent;
            minBound = member.minBound;

            node = p_Tree.nodes[parent];

            scale *= 2;

            continue;
        }

        hit.depth = currentStack + 1;

        vec3 center = minBound + scale * dimensions;
        vec3 boundOffset = vec3(0);
        int octantMask = 0;
        if (position.x >= center.x) {
            octantMask ^= 1;
            boundOffset.x = dimensions.x;
        }
        if (position.z >= center.z) {
            octantMask ^= 2;
            boundOffset.z = dimensions.z;
        }
        if (position.y >= center.y) {
            octantMask ^= 4;
            boundOffset.y = dimensions.y;
        }

        bool isValid = bool((node.validMask >> octantMask) & 1);
        bool isLeaf = bool((node.leafMask >> octantMask) & 1);

        if (isValid && !isLeaf) // Parent Voxel, Add to stack
        {
            if (node.childPtr == 0)
                break;

            StackMember stackMember;
            stackMember.parent = parent;
            stackMember.tMax = tMax;
            stackMember.minBound = minBound;

            stack[currentStack + 1] = stackMember;
            currentStack++;

            uint count = uint(node.validMask) >> (octantMask + 1);
            parent = parent + node.childPtr + bitCount(count);
            node = p_Tree.nodes[parent];

            minBound += boundOffset * scale;

            maxBound = minBound + scale * dimensions;

            if (!rayBoxIntersect(ray, minBound, maxBound, 0., tMax, tMin, tMax)) break;

            node = p_Tree.nodes[parent];

            scale *= 0.5;

            continue;
        }

        if (isValid && isLeaf) // Solid Voxel
        {
            uint nodeIndex = parent + node.childPtr + bitCount(uint(node.validMask) >> (octantMask + 1));
            uint8_t materialIndex = p_Tree.nodes[nodeIndex].materialIndex;

            hit.t = t;
            hit.position = calculatePosition(origin, direction, t);
            hit.parent = parent;
            hit.normal = vec3(1., 0., 0.);
            hit.materialIndex = materialIndex;
            hit.depth = currentStack + 1;
            hit.deepest += 1;
            return hit;
        }

        if (!isValid)
        {
            vec3 octantMinBound = minBound + boundOffset * scale;
            vec3 octantMaxBound = octantMinBound + scale * dimensions;

            float t0, t1;
            if (!rayBoxIntersect(ray, octantMinBound, octantMaxBound, tMin, tMax, t0, t1)) break;

            t = t1 + 0.0001;
            position = calculatePosition(origin, direction, t);

            continue;
        }
    }

    hit.t = -1;
    hit.depth = currentStack + 1;
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

        // imageStore(o_Image, texelCoord, lookupColour);
        imageStore(o_Image, texelCoord, lookupColour);
    }

    if (hit.deepest >= 0)
        imageStore(o_ComparisonImage, texelCoord, vec4((hit.deepest) / 5.));
}
