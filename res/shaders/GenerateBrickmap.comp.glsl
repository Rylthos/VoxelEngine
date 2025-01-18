#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_shader_atomic_int64 : enable
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(buffer_reference, std430) buffer ColourBuffer {
    vec4 colours[];
};

layout(buffer_reference, std430) buffer GenerationData {
    uint32_t numSolidVoxels;
    uint64_t solidMask[8];
};

layout(push_constant) uniform constants {
    // uint32_t p_Seed;
    ivec3 p_BrickIndex;
    int _1;
    ivec3 p_WorldPosition;
    int _2;
    GenerationData p_Data;
    ColourBuffer p_Colours;
};

uint getIndex(in uvec3 position) {
    return position.x + position.z * 8 + position.y * 8 * 8;
}

void setVoxel(in uvec3 position, in vec3 colour) {
    uint mask = position.x + position.z * 8;
    atomicAdd(p_Data.solidMask[position.y], mask);
    p_Colours.colours[getIndex(position)] = vec4(colour, 1.);
    atomicAdd(p_Data.numSolidVoxels, 1);
}

void setAir(in uvec3 position) {
    p_Colours.colours[getIndex(position)] = vec4(0., 0., 0., -1.);
}

void main() {
    uvec3 currentIndex = gl_GlobalInvocationID.xyz;

    ivec3 worldPosition = p_WorldPosition + ivec3(currentIndex);

    if (p_BrickIndex.y % 4 == 0) {
        uint sum = currentIndex.x + currentIndex.y + currentIndex.z;
        if (sum % 5 == 0) {
            setVoxel(currentIndex, vec3(1.));
        } else {
            setAir(currentIndex);
        }
    } else {
        setAir(currentIndex);
    }
}
