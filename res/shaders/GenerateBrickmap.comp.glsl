#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_atomic_int64 : enable
#extension GL_GOOGLE_include_directive : require

#include "Hash.other.glsl"
#include "Noise.other.glsl"

#define BRICK_SIZE 8

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(constant_id = 0) const int NUM_BRICKS = 6;

layout(buffer_reference, std430) buffer ColourBuffer {
    vec4 colours[];
};

layout(buffer_reference, std430) buffer GenerationData {
    float colourSumR;
    float colourSumG;
    float colourSumB;
};

layout(push_constant) uniform constants {
    vec4 p_BrickPosition[NUM_BRICKS];
    ColourBuffer p_Colours;
};

uint getIndex(in int writeOffset, in uvec3 position) {
    return writeOffset + position.x + position.z * BRICK_SIZE + position.y * BRICK_SIZE * BRICK_SIZE;
}

void setVoxel(in int writeOffset, in uvec3 position, in vec3 colour) {
    p_Colours.colours[getIndex(writeOffset, position)] = vec4(colour, 1.);
}

void setAir(in int writeOffset, in uvec3 position) {
    p_Colours.colours[getIndex(writeOffset, position)] = vec4(0., 0., 0., -1.);
}

float height(vec3 pos)
{
    float noiseValue = (16 / 30.) * snoise(pos.xz) +
            (8 / 30.) * snoise(pos.xz * 2.0) +
            (4 / 30.) * snoise(pos.xz * 4.0) +
            (2 / 30.) * snoise(pos.xz * 8.0);

    return noiseValue;
}

void main() {
    uvec3 currentIndex = gl_LocalInvocationID.xyz;

    const uint target = gl_WorkGroupID.x;
    const vec3 worldPosition = p_BrickPosition[target].xyz + currentIndex * 0.125;
    const int writeTarget = int(p_BrickPosition[target].w);

    const int writeOffset = BRICK_SIZE * BRICK_SIZE * BRICK_SIZE * writeTarget;

    vec3 uv = vec3(worldPosition);
    uv.y -= 60.;
    uv.y = -(uv.y / 1.5);

    uv.xz /= 50.;

    float value = height(uv) * 20.;

    if (uv.y < value) {
        // setVoxel(writeOffset, currentIndex, vec3(currentIndex.x / 7., currentIndex.y / 7., currentIndex.z / 7.));
        setVoxel(writeOffset, currentIndex, vec3(1.));
    } else {
        setAir(writeOffset, currentIndex);
    }
}
