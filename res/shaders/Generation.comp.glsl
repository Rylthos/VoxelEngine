#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#include "Hash.other.glsl"
#include "Noise.other.glsl"

#define AIR int16_t(-1)

struct Voxel
{
    int16_t type;
};

layout(buffer_reference, std430) writeonly buffer VoxelBuffer {
    Voxel voxels[];
};

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
    VoxelBuffer p_TargetBuffer;
};

int64_t splitBy3(uint32_t a)
{
    int64_t x = int64_t(a);
    x &= 0x000003ff; // x = ---- ---- ---- ---- ---- --98 7654 3210
    x = (x ^ (x << 16)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x << 8)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x << 4)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x << 2)) & 0x09249249; // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    return x;
}

uint32_t compactBy3(int64_t a)
{
    int64_t x = a;
    x &= 0x09249249; // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
    x = (x ^ (x >> 2)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
    x = (x ^ (x >> 4)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
    x = (x ^ (x >> 8)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
    x = (x ^ (x >> 16)) & 0x000003ff; // x = ---- ---- ---- ---- ---- --98 7654 3210
    return uint32_t(x);
}

uvec3 mortenDecode(int64_t code)
{
    uvec3 position;
    position.x = compactBy3(code >> 0);
    position.y = compactBy3(code >> 2);
    position.z = compactBy3(code >> 1);

    return position;
}

int64_t mortenEncode(uvec3 position)
{
    return (splitBy3(position.x) | (splitBy3(position.y) << 2) | splitBy3(position.z) << 1);
}

uint convertFlatIndexToMorten(uvec3 position)
{
    int64_t morten = mortenEncode(position);
    return uint(morten);
}

void main()
{
    uvec3 currentIndex = gl_GlobalInvocationID.xyz;
    uint flatIndex = convertFlatIndexToMorten(currentIndex);

    vec3 uv = currentIndex / vec3(p_Dimension - 1);

    uv += p_Origin.xyz;

    uv /= 2;

    float noiseValue = snoise(uv.xz); // [-1, 1]
    noiseValue *= 0.5;
    Voxel outputVoxel;

    outputVoxel.type = AIR;
    // if (uv.y > noiseValue)
    //     outputVoxel.type = int16_t(p_P10);

    float cutoff = 1 - uv.y;
    float remaining = 1 - cutoff;
    float p10 = cutoff + remaining * 0.1;
    float p50 = cutoff + remaining * 0.5;
    float p100 = cutoff + remaining;

    outputVoxel.type = int16_t(p_P100);
    if (noiseValue <= cutoff)
    {
        outputVoxel.type = AIR;
    }
    else if (noiseValue <= p10)
    {
        outputVoxel.type = int16_t(p_P10);
    }
    else if (noiseValue <= p50)
    {
        outputVoxel.type = int16_t(p_P50);
    }

    p_TargetBuffer.voxels[flatIndex] = outputVoxel;
}
