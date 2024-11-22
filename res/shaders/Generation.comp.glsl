#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_buffer_reference : enable

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

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
    ivec2 _;
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

void sphereScene(uvec3 position, uint flatIndex)
{
    const float R = p_Dimension / 2.;

    vec3 center = vec3(p_Dimension / 2.);

    vec3 modifiedPosition = position - center;

    Voxel outputVoxel;
    outputVoxel.type = AIR;
    if (dot(modifiedPosition, modifiedPosition) < R * R)
    {
        outputVoxel.type = int16_t(1);
    }

    p_TargetBuffer.voxels[flatIndex] = outputVoxel;
}

vec3 grad3[] = {
        vec3(0, 1, 1),
        vec3(0, 1, -1),
        vec3(0, -1, 1),
        vec3(0, -1, -1),
        vec3(1, 0, 1),
        vec3(1, 0, -1),
        vec3(-1, 0, 1),
        vec3(-1, 0, -1),
        vec3(1, 1, 0),
        vec3(1, -1, 0),
        vec3(-1, 1, 0),
        vec3(-1, -1, 0)
    };

const int perm[] = {
        151,
        160,
        137,
        91,
        90,
        15,
        131,
        13,
        201,
        95,
        96,
        53,
        194,
        233,
        7,
        225,
        140,
        36,
        103,
        30,
        69,
        142,
        8,
        99,
        37,
        240,
        21,
        10,
        23,
        190,
        6,
        148,
        247,
        120,
        234,
        75,
        0,
        26,
        197,
        62,
        94,
        252,
        219,
        203,
        117,
        35,
        11,
        32,
        57,
        177,
        33,
        88,
        237,
        149,
        56,
        87,
        174,
        20,
        125,
        136,
        171,
        168,
        68,
        175,
        74,
        165,
        71,
        134,
        139,
        48,
        27,
        166,
        77,
        146,
        158,
        231,
        83,
        111,
        229,
        122,
        60,
        211,
        133,
        230,
        220,
        105,
        92,
        41,
        55,
        46,
        245,
        40,
        244,
        102,
        143,
        54,
        65,
        25,
        63,
        161,
        1,
        216,
        80,
        73,
        209,
        76,
        132,
        187,
        208,
        89,
        18,
        169,
        200,
        196,
        135,
        130,
        116,
        188,
        159,
        86,
        164,
        100,
        109,
        198,
        173,
        186,
        3,
        64,
        52,
        217,
        226,
        250,
        124,
        123,
        5,
        202,
        38,
        147,
        118,
        126,
        255,
        82,
        85,
        212,
        207,
        206,
        59,
        227,
        47,
        16,
        58,
        17,
        182,
        189,
        28,
        42,
        223,
        183,
        170,
        213,
        119,
        248,
        152,
        2,
        44,
        154,
        163,
        70,
        221,
        153,
        101,
        155,
        167,
        43,
        172,
        9,
        129,
        22,
        39,
        253,
        19,
        98,
        108,
        110,
        79,
        113,
        224,
        232,
        178,
        185,
        112,
        104,
        218,
        246,
        97,
        228,
        251,
        34,
        242,
        193,
        238,
        210,
        144,
        12,
        191,
        179,
        162,
        241,
        81,
        51,
        145,
        235,
        249,
        14,
        239,
        107,
        49,
        192,
        214,
        31,
        181,
        199,
        106,
        157,
        184,
        84,
        204,
        176,
        115,
        121,
        50,
        45,
        127,
        4,
        150,
        254,
        138,
        236,
        205,
        93,
        222,
        114,
        67,
        29,
        24,
        72,
        243,
        141,
        128,
        195,
        78,
        66,
        215,
        61,
        156,
        180,
        151,
        160,
        137,
        91,
        90,
        15,
        131,
        13,
        201,
        95,
        96,
        53,
        194,
        233,
        7,
        225,
        140,
        36,
        103,
        30,
        69,
        142,
        8,
        99,
        37,
        240,
        21,
        10,
        23,
        190,
        6,
        148,
        247,
        120,
        234,
        75,
        0,
        26,
        197,
        62,
        94,
        252,
        219,
        203,
        117,
        35,
        11,
        32,
        57,
        177,
        33,
        88,
        237,
        149,
        56,
        87,
        174,
        20,
        125,
        136,
        171,
        168,
        68,
        175,
        74,
        165,
        71,
        134,
        139,
        48,
        27,
        166,
        77,
        146,
        158,
        231,
        83,
        111,
        229,
        122,
        60,
        211,
        133,
        230,
        220,
        105,
        92,
        41,
        55,
        46,
        245,
        40,
        244,
        102,
        143,
        54,
        65,
        25,
        63,
        161,
        1,
        216,
        80,
        73,
        209,
        76,
        132,
        187,
        208,
        89,
        18,
        169,
        200,
        196,
        135,
        130,
        116,
        188,
        159,
        86,
        164,
        100,
        109,
        198,
        173,
        186,
        3,
        64,
        52,
        217,
        226,
        250,
        124,
        123,
        5,
        202,
        38,
        147,
        118,
        126,
        255,
        82,
        85,
        212,
        207,
        206,
        59,
        227,
        47,
        16,
        58,
        17,
        182,
        189,
        28,
        42,
        223,
        183,
        170,
        213,
        119,
        248,
        152,
        2,
        44,
        154,
        163,
        70,
        221,
        153,
        101,
        155,
        167,
        43,
        172,
        9,
        129,
        22,
        39,
        253,
        19,
        98,
        108,
        110,
        79,
        113,
        224,
        232,
        178,
        185,
        112,
        104,
        218,
        246,
        97,
        228,
        251,
        34,
        242,
        193,
        238,
        210,
        144,
        12,
        191,
        179,
        162,
        241,
        81,
        51,
        145,
        235,
        249,
        14,
        239,
        107,
        49,
        192,
        214,
        31,
        181,
        199,
        106,
        157,
        184,
        84,
        204,
        176,
        115,
        121,
        50,
        45,
        127,
        4,
        150,
        254,
        138,
        236,
        205,
        93,
        222,
        114,
        67,
        29,
        24,
        72,
        243,
        141,
        128,
        195,
        78,
        66,
        215,
        61,
        156,
        180
    };

float noise(vec3 pos)
{
    float n0, n1, n2, n3;

    const float F3 = 1. / 3.;
    float s = (pos.x + pos.y + pos.z) * F3;
    ivec3 skewed = ivec3(floor(pos + s));

    const float G3 = 1. / 6.;
    float t = (skewed.x + skewed.y + skewed.z) * G3;
    vec3 unskew = skewed - t;
    vec3 origin = pos - unskew;

    ivec3 offsetSecond;
    ivec3 offsetThird;

    if (origin.x >= origin.y)
    {
        if (origin.y >= origin.z)
        {
            offsetSecond = ivec3(1, 0, 0);
            offsetThird = ivec3(1, 1, 0);
        }
        else if (origin.x >= origin.z)
        {
            offsetSecond = ivec3(1, 0, 0);
            offsetThird = ivec3(1, 0, 1);
        }
        else
        {
            offsetSecond = ivec3(0, 0, 1);
            offsetThird = ivec3(1, 0, 1);
        }
    }
    else
    {
        if (origin.y < origin.z)
        {
            offsetSecond = ivec3(0, 0, 1);
            offsetThird = ivec3(0, 1, 1);
        }
        else if (origin.x < origin.z)
        {
            offsetSecond = ivec3(0, 0, 1);
            offsetThird = ivec3(0, 1, 1);
        }
        else
        {
            offsetSecond = ivec3(0, 1, 0);
            offsetThird = ivec3(1, 1, 0);
        }
    }

    vec3 offsetSecondXYZ = origin - offsetSecond + G3;
    vec3 offsetThirdXYZ = origin - offsetThird + 2. * G3;
    vec3 offsetFourthXYZ = origin - 1. + 3. * G3;

    ivec3 hash = ivec3(skewed.x & 255, skewed.y & 255, skewed.z & 255);
    ivec4 gi;
    gi.x = perm[hash.x + perm[hash.y + perm[hash.z]]] % 12;
    gi.y = perm[hash.x + offsetSecond.x + perm[hash.y + offsetSecond.y + perm[hash.z + offsetSecond.z]]] % 12;
    gi.z = perm[hash.x + offsetThird.x + perm[hash.y + offsetThird.y + perm[hash.z + offsetThird.z]]] % 12;
    gi.w = perm[hash.x + 1 + perm[hash.y + 1 + perm[hash.z + 1]]] % 12;

    float t0 = 0.5 - dot(origin, origin);
    if (t0 < 0) n0 = 0.;
    else
    {
        t0 *= t0;
        n0 = t0 * t0 * dot(grad3[gi.x], origin);
    }

    float t1 = 0.5 * dot(offsetSecondXYZ, offsetSecondXYZ);
    if (t1 < 0) n1 = 0.;
    else
    {
        t1 *= t1;
        n1 = t1 * t1 * dot(grad3[gi.y], offsetSecondXYZ);
    }

    float t2 = 0.5 * dot(offsetThirdXYZ, offsetThirdXYZ);
    if (t2 < 0) n2 = 0.;
    else
    {
        t2 *= t2;
        n2 = t2 * t2 * dot(grad3[gi.z], offsetThirdXYZ);
    }

    float t3 = 0.5 * dot(offsetFourthXYZ, offsetFourthXYZ);
    if (t3 < 0) n3 = 0.;
    else
    {
        t3 *= t3;
        n3 = t3 * t3 * dot(grad3[gi.w], offsetFourthXYZ);
    }

    return 32. * (n0 + n1 + n2 + n3);
}

void main()
{
    uvec3 currentIndex = gl_GlobalInvocationID.xyz;
    uint flatIndex = convertFlatIndexToMorten(currentIndex);

    vec3 uv = currentIndex / vec3(p_Dimension - 1);

    float noiseValue = noise(uv * 2);
    Voxel outputVoxel;
    outputVoxel.type = AIR;
    if (noiseValue >= 0.2)
    {
        outputVoxel.type = int16_t(1);
    }

    p_TargetBuffer.voxels[flatIndex] = outputVoxel;

    // uint writeIndex = uint(dot(currentIndex * uvec3(1, p_Dimension * p_Dimension, p_Dimension), vec3(1.)));

    // uint sum = currentIndex.x + currentIndex.y + currentIndex.z;
    // sphereScene(currentIndex, flatIndex);
    // Voxel temp;
    // temp.type = int16_t(writeIndex);
    // if (currentIndex.y % 2 == 0)
    // {
    //     if (sum % 2 == 0)
    //         temp.type = int16_t(0);
    //     else
    //         temp.type = AIR;
    // }
    // else
    // {
    //     if (sum % 2 == 0)
    //         temp.type = int16_t(1);
    //     else
    //         temp.type = AIR;
    // }

    // p_TargetBuffer.voxels[flatIndex] = temp;
}
