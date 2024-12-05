#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_debug_printf : enable

layout(local_size_x = 2, local_size_y = 2, local_size_z = 2) in;

#define VOXEL_IS_SOLID 1
#define VOXEL_IS_PARENT 2
#define VOXEL_IS_AIR 4

layout(rgba16ui, set = 0, binding = 0) uniform uimage3D o_Generated[];

layout(std430, set = 1, binding = 0) buffer SharedData {
    uint32_t counter;
} o_Shared;

layout(push_constant) uniform constants {
    int p_SourceLevel;
};

int countColour(uvec3 start, uint currentColour)
{
    int count = 0;
    for (int y = 0; y <= 1; y++)
    {
        for (int z = 0; z <= 1; z++)
        {
            for (int x = 0; x <= 1; x++)
            {
                uvec4 childData = imageLoad(o_Generated[p_SourceLevel], ivec3(start + ivec3(x, y, z)));
                uint flags = (childData.b >> 8) & 0xFF;
                uint colour = childData.b & 0xFF;

                if ((flags & VOXEL_IS_AIR) != 0) continue;

                if (colour == currentColour) count++;
            }
        }
    }

    return count;
}

void main()
{
    const uvec3 mipImageSize = imageSize(o_Generated[p_SourceLevel + 1u]);
    if (gl_GlobalInvocationID.x >= mipImageSize.x || gl_GlobalInvocationID.y >= mipImageSize.y || gl_GlobalInvocationID.z >= mipImageSize.z)
        return;

    const uvec3 topLeft = 2 * gl_GlobalInvocationID.xyz;

    bool allAir = true;
    bool allSolid = true;
    uint flags = VOXEL_IS_PARENT;
    uint valid = 0x0;
    uint leaf = 0x0;

    int maxCount = 0;
    int bestColour = -1;
    for (int y = 0; y <= 1; y++)
    {
        for (int z = 0; z <= 1; z++)
        {
            for (int x = 0; x <= 1; x++)
            {
                int childOffset = (0x4 * y) + (0x2 * z) + (0x1 * x);
                int bitFlag = 1 << childOffset;

                uvec4 childData = imageLoad(o_Generated[p_SourceLevel], ivec3(topLeft + ivec3(x, y, z)));
                uint childFlags = (childData.b >> 8) & 0xFF;
                uint colour = (childData.b & 0xFF);

                debugPrintfEXT("    Child: %v3u, Bitflag: %d, Colour: %d, validFlags: %x", uvec3(topLeft + ivec3(x, y, z)), bitFlag, uint8_t(colour), childFlags);

                if ((childFlags & VOXEL_IS_AIR) == 0) // Not Air
                {
                    allAir = false;
                    int count = countColour(topLeft, colour);
                    if (count > maxCount) {
                        bestColour = int(colour);
                        maxCount = count;
                    }

                    valid |= bitFlag;
                }

                if ((childFlags & VOXEL_IS_SOLID) != 0) // Is solid
                    leaf |= bitFlag;
                else
                    allSolid = false;
            }
        }
    }

    if (maxCount == 8 && allSolid)
    {
        flags = VOXEL_IS_SOLID;
        valid = 0;
        leaf = 0;
    }

    if (allAir) {
        flags = VOXEL_IS_AIR | VOXEL_IS_SOLID;
        valid = 0;
        leaf = 0;
        bestColour = 0;
    }

    atomicAdd(o_Shared.counter, bitCount(valid));

    debugPrintfEXT("Node: %v3u, Material: %d, Flags: %x, Leaf: %x, Valid: %x", gl_GlobalInvocationID, bestColour, flags, leaf, valid);

    uvec4 data = uvec4(0, 0, ((flags & 0xFF) << 8) | (bestColour & 0xFF), ((valid & 0xFF) << 8) | (leaf));
    // uvec4 data = uvec4(bottomRight, 0);

    // imageStore(o_Generated[p_SourceLevel + 1u], ivec3(gl_GlobalInvocationID), uvec4(gl_GlobalInvocationID, 0));
    imageStore(o_Generated[p_SourceLevel + 1u], ivec3(gl_GlobalInvocationID), data);
}
