#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#include "Hash.other.glsl"

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

vec3 random3(vec3 c)
{
    float j = 4096. * sin(dot(c, vec3(17., 59.4, 15.)));
    vec3 r;
    r.z = random(vec2(fract(512. * j), p_Seed));
    j *= .125;
    r.x = random(vec2(fract(512. * j), p_Seed));
    j *= .125;
    r.y = random(vec2(fract(512. * j), p_Seed));

    return r - 0.5; // [-0.5, 0.5]
}

float simplex3D(vec3 pos)
{
    const float F3 = 1. / 3.;
    const float G3 = 1. / 6.;

    vec3 s = floor(pos + dot(pos, vec3(F3)));
    vec3 x = pos - s + dot(s, vec3(G3));

    vec3 e = step(vec3(0.), x - x.yzx);
    vec3 i1 = e * (1. * e.zxy);
    vec3 i2 = 1. - e.zxy * (1. - e);

    vec3 x1 = x - i1 + G3;
    vec3 x2 = x - i2 + 2. * G3;
    vec3 x3 = x - 1. + 3. * G3;

    vec4 w, d;
    w.x = dot(x, x);
    w.y = dot(x1, x1);
    w.z = dot(x2, x2);
    w.w = dot(x3, x3);

    w = max(0.6 - w, 0.);
    d.x = dot(random3(s), x);
    d.y = dot(random3(s + i1), x1);
    d.z = dot(random3(s + i2), x2);
    d.w = dot(random3(s + 1.), x3);

    w *= w;
    w *= w;
    d *= w;

    return dot(d, vec4(52.));
}

const mat3 rot1 = mat3(-0.37, 0.36, 0.85, -0.14, -0.93, 0.34, 0.92, 0.01, 0.4);
const mat3 rot2 = mat3(-0.55, -0.39, 0.74, 0.33, -0.91, -0.24, 0.77, 0.12, 0.63);
const mat3 rot3 = mat3(-0.71, 0.52, -0.47, -0.08, -0.72, -0.68, -0.7, -0.45, 0.56);

float simplex3D_fractal(vec3 m) {
    return 16 / 30 * simplex3D(m * rot1)
        + 8 / 30 + simplex3D(2. * m * rot2)
        + 4 / 30 + simplex3D(4. * m * rot3)
        + 2 / 30 + simplex3D(8. * m);
}

void main()
{
    uvec3 currentIndex = gl_GlobalInvocationID.xyz;
    uint flatIndex = convertFlatIndexToMorten(currentIndex);

    vec3 uv = currentIndex / vec3(p_Dimension - 1);

    uv += p_Origin.xyz;

    float noiseValue = simplex3D_fractal(uv); // [-1, 1]
    Voxel outputVoxel;

    float cutoff = p_Cutoff;
    float remaining = 1 - p_Cutoff;
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
