#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 2, local_size_y = 2, local_size_z = 2) in;

#define VOXEL_IS_SOLID 1
#define VOXEL_IS_PARENT 2
#define VOXEL_IS_AIR 4

layout(rgba16ui, set = 0, binding = 0) uniform uimage3D o_Generated[8];

#include "Morten.other.glsl"

layout(push_constant) uniform constants {
    int p_SourceLevel;
};

void main()
{
    const uvec3 mipImageSize = imageSize(o_Generated[p_SourceLevel + 1u]);
    if (gl_GlobalInvocationID.x > mipImageSize.x || gl_GlobalInvocationID.y > mipImageSize.y || gl_GlobalInvocationID.z > mipImageSize.z)
        return;

    uvec3 startIndex = 2 * gl_GlobalInvocationID;

    uvec3 topLeft = startIndex;
    uvec3 bottomRight = startIndex + uvec3(1);

    bool allAir = true;
    for (int y = 0; y <= 1; y++)
    {
        for (int z = 0; z <= 1; z++)
        {
            for (int x = 0; x <= 1; x++)
            {
                uvec4 data = imageLoad(o_Generated[p_SourceLevel], ivec3(topLeft + ivec3(x, y, z)));
                if (((data.b >> 8) & VOXEL_IS_AIR) == 0)
                    allAir = false;
            }
        }
    }

    int flags = VOXEL_IS_PARENT;
    if (allAir) flags |= (VOXEL_IS_SOLID | VOXEL_IS_AIR);

    uvec4 data = uvec4(1, 0, (flags << 8) & 0xFF | 0, 0);
    // uvec4 data = uvec4(gl_GlobalInvocationID.xyz, 0);
    // imageStore(o_Generated[], ivec3(gl_GlobalInvocationID), uvec4(1));
    imageStore(o_Generated[p_SourceLevel + 1u], ivec3(gl_GlobalInvocationID), data);
}
