#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 2, local_size_y = 2, local_size_z = 2) in;

#define VOXEL_IS_SOLID 1
#define VOXEL_IS_PARENT 2
#define VOXEL_IS_AIR 4

layout(rgba16ui, set = 0, binding = 0) uniform uimage3D o_Generated[4];

layout(std430, set = 1, binding = 0) buffer SharedData {
    uint32_t counter;
} o_Shared;

layout(push_constant) uniform constants {
    int p_SourceLevel;
};

int countColour(uvec3 start, uint8_t currentColour)
{
    int count = 0;
    for (int y = 0; y <= 1; y++)
    {
        for (int z = 0; z <= 1; z++)
        {
            for (int x = 0; x <= 1; x++)
            {
                uvec4 childData = imageLoad(o_Generated[p_SourceLevel], ivec3(start + ivec3(x, y, z)));
                uint8_t flags = uint8_t((childData.b >> 8) & 0xFF);
                uint8_t colour = uint8_t(childData.b & 0xFF);

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

    uvec3 startIndex = 2 * gl_GlobalInvocationID;

    uvec3 topLeft = startIndex;
    uvec3 bottomRight = startIndex + uvec3(1);

    bool allAir = true;
    bool allSolid = true;
    uint flags = VOXEL_IS_PARENT;
    uint valid = 0x0;
    uint leaf = 0x0;

    int maxCount = 0;
    int bestColour = 0;

    int childCount = 0;
    for (int y = 0; y <= 1; y++)
    {
        for (int z = 0; z <= 1; z++)
        {
            for (int x = 0; x <= 1; x++)
            {
                uvec4 childData = imageLoad(o_Generated[p_SourceLevel], ivec3(topLeft + ivec3(x, y, z)));
                uint8_t childFlags = uint8_t((childData.b >> 8) & 0xFF);
                uint8_t colour = uint8_t(childData.b & 0xFF);

                valid <<= 1;
                leaf <<= 1;

                if ((childFlags & VOXEL_IS_AIR) == 0) // Not Air
                {
                    allAir = false;
                    int count = countColour(topLeft, colour);
                    if (count > maxCount) {
                        bestColour = colour;
                        maxCount = count;
                    }

                    valid |= 1;
                }

                if ((childFlags & VOXEL_IS_SOLID) != 0) // Is solid
                    leaf |= 1;
                else
                    allSolid = false;
            }
        }
    }

    // if (maxCount == 8 && allSolid)
    // {
    //     flags |= VOXEL_IS_SOLID;
    //     valid = 0;
    // }

    if (allAir) {
        flags |= (VOXEL_IS_AIR | VOXEL_IS_SOLID);
        valid = 0;
    }

    atomicAdd(o_Shared.counter, bitCount(valid));

    uvec4 data = uvec4(0, 0, ((flags & 0xFF) << 8) | (bestColour & 0xFF), ((valid & 0xFF) << 8) | (leaf));
    // uvec4 data = uvec4(bottomRight, 0);

    // imageStore(o_Generated[p_SourceLevel + 1u], ivec3(gl_GlobalInvocationID), uvec4(gl_GlobalInvocationID, 0));
    imageStore(o_Generated[p_SourceLevel + 1u], ivec3(gl_GlobalInvocationID), data);
}
