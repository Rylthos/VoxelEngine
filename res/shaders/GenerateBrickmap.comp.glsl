#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_atomic_int64 : enable
#extension GL_GOOGLE_include_directive : require

#include "Hash.other.glsl"
#include "Noise.other.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(buffer_reference, std430) buffer ColourBuffer {
    vec4 colours[];
};

layout(buffer_reference, std430) buffer GenerationData {
    int colourSumR;
    int colourSumG;
    int colourSumB;
};

layout(push_constant) uniform constants {
    vec3 p_BrickPosition;
    int _2;
    GenerationData p_Data;
    ColourBuffer p_Colours;
};

uint getIndex(in uvec3 position) {
    return position.x + position.z * 8 + position.y * 8 * 8;
}

void setVoxel(in uvec3 position, in vec3 colour) {
    // uint mask = position.x + position.z * 8;
    // atomicAdd(p_Data.solidMask[position.y], mask);
    p_Colours.colours[getIndex(position)] = vec4(colour, 1.);
    // atomicAdd(p_Data.numSolidVoxels, 1);

    atomicAdd(p_Data.colourSumR, int(clamp(colour.r * 255., 0., 255.)));
    atomicAdd(p_Data.colourSumG, int(clamp(colour.g * 255., 0., 255.)));
    atomicAdd(p_Data.colourSumB, int(clamp(colour.b * 255., 0., 255.)));
}

void setAir(in uvec3 position) {
    p_Colours.colours[getIndex(position)] = vec4(0., 0., 0., -1.);
}

float height(vec3 pos)
{
    float noiseValue = (16 / 30.) * snoise(pos.xz) +
            (8 / 30.) * snoise(pos.xz * 2.0) +
            (4 / 30.) * snoise(pos.xz * 4.0) +
            (2 / 30.) * snoise(pos.xz * 8.0);
    // noiseValue *= 10;

    return noiseValue;
}

void main() {
    uvec3 currentIndex = gl_GlobalInvocationID.xyz;

    vec3 worldPosition = p_BrickPosition + currentIndex * 0.125;
    vec3 uv = vec3(worldPosition);
    uv.y -= 60.;
    uv.y = -(uv.y / 1.5);

    uv.xz /= 50.;

    float value = height(uv) * 20.;

    // setVoxel(currentIndex, vec3(currentIndex.x / 7., currentIndex.y / 7., currentIndex.z / 7.));

    if (uv.y < value) {
        setVoxel(currentIndex, vec3(currentIndex.x / 7., currentIndex.y / 7., currentIndex.z / 7.));
    } else {
        setAir(currentIndex);
    }
}
