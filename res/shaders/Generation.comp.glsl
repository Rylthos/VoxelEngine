#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#define VOXEL_IS_SOLID 1
#define VOXEL_IS_PARENT 2
#define VOXEL_IS_AIR 4

// R: OFFSET_H
// G: OFFSET_L
// B: FLAG COLOUR
// A: VALID LEAF
layout(rgba16ui, set = 0, binding = 0) uniform uimage3D o_Generated[];

#include "Hash.other.glsl"
#include "Noise.other.glsl"

layout(push_constant) uniform constants {
    uint32_t p_Dimension;
    float p_Size;
    uint32_t p_Seed;
    uint32_t _;
    float p_Cutoff;
    int p_P10;
    int p_P50;
    int p_P100;
    ivec4 p_Origin;
};

float height(vec3 pos)
{
    float noiseValue = (16 / 30.) * snoise(pos.xz) +
            (8 / 30.) * snoise(pos.xz * 2.0) +
            (4 / 30.) * snoise(pos.xz * 4.0) +
            (2 / 30.) * snoise(pos.xz * 8.0);
    // noiseValue *= 10;

    return noiseValue;
}

void main()
{
    uvec3 currentIndex = gl_GlobalInvocationID.xyz;

    const uvec3 mipImageSize = imageSize(o_Generated[0]);
    if (gl_GlobalInvocationID.x >= mipImageSize.x || gl_GlobalInvocationID.y >= mipImageSize.y || gl_GlobalInvocationID.z >= mipImageSize.z)
        return;

    vec3 uv = currentIndex / vec3(mipImageSize);

    uv += p_Origin.xyz;

    uv /= 2;

    float heightValue = height(uv / 5.); // [-1, 1]

    int type = -1;

    float cutoff = 1 - uv.y;
    float remaining = 1 - cutoff;
    float p10 = cutoff + remaining * 0.1;
    float p50 = cutoff + remaining * 0.5;
    float p100 = cutoff + remaining;

    type = p_P100;
    if (heightValue <= cutoff)
    {
        type = -1;
    }
    else if (heightValue <= p10)
    {
        type = p_P10;
    }
    else if (heightValue <= p50)
    {
        type = p_P50;
    }

    type = 4;
    if ((gl_GlobalInvocationID.x + gl_GlobalInvocationID.y + gl_GlobalInvocationID.z) % 2 == 0)
        type = 20;

    uint flags = VOXEL_IS_SOLID;
    if (type < 0) {
        flags |= VOXEL_IS_AIR;
        type = 0;
    }

    uvec4 data = uvec4(0, 0, ((flags & 0xFF) << 8) | (type & 0xFF), 0);
    imageStore(o_Generated[0], ivec3(currentIndex), data);
}
