#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

#define VOXEL_IS_SOLID 1
#define VOXEL_IS_PARENT 2
#define VOXEL_IS_AIR 4

layout(local_size_x = 2, local_size_y = 2, local_size_z = 2) in;

layout(rgba16ui, set = 0, binding = 0) uniform uimage3D o_Generated[];

layout(std430, set = 1, binding = 0) buffer SharedData {
    uint counter;
} o_Shared;

struct SVONode {
    uint32_t childPtr;
    uint8_t unused;
    uint8_t materialIndex;
    uint8_t validMask;
    uint8_t leafMask;
};

layout(buffer_reference, std430) readonly buffer SVONodeBuffer {
    SVONode nodes[];
};

layout(push_constant) uniform constants {
    uint32_t p_CurrentMip;
    SVONodeBuffer p_Nodes;
};

void writeNode(uint32_t nodePlacement, uvec4 data)
{
    p_Nodes.nodes[nodePlacement].childPtr = 0;
    p_Nodes.nodes[nodePlacement].unused = int8_t((data.b >> 8) & 0xFF);
    p_Nodes.nodes[nodePlacement].materialIndex = uint8_t(data.b & 0xFF);
    p_Nodes.nodes[nodePlacement].validMask = uint8_t((data.a >> 8) & 0xFF);
    p_Nodes.nodes[nodePlacement].leafMask = uint8_t(data.a & 0xFF);
}

void main()
{
    // Write Node to list at position based on location in image
    // Increment atomic based on children, store children in correct order
    // Set childptr of current to be difference between old atomic and position
    // Change childrens ptr in image to where they were stored

    const uvec3 mipImageSize = imageSize(o_Generated[p_CurrentMip]);
    if (gl_GlobalInvocationID.x >= mipImageSize.x || gl_GlobalInvocationID.y >= mipImageSize.y || gl_GlobalInvocationID.z >= mipImageSize.z)
        return;

    uvec4 data = imageLoad(o_Generated[p_CurrentMip], ivec3(gl_GlobalInvocationID.xyz));
    uint32_t placement = (uint32_t(data.r) << 16) | uint32_t(data.g);
    uint validFlags = (data.a >> 8) & 0xFF;
    uint chunkFlags = (data.b >> 8) & 0xFF;

    if (mipImageSize != uvec3(1, 1, 1) && placement == 0)
        return;

    writeNode(placement, data);

    int childCount = bitCount(validFlags);
    if ((chunkFlags & (VOXEL_IS_SOLID | VOXEL_IS_AIR)) != 0)
        childCount = 0;

    uint childrenPlacement = atomicAdd(o_Shared.counter, childCount);

    uint32_t newChildPtr = childrenPlacement - placement;
    p_Nodes.nodes[placement].childPtr = newChildPtr;
    // p_Nodes.nodes[0].childPtr = 1;

    uvec3 childStart = gl_GlobalInvocationID.xyz * 2;
    debugPrintfEXT("Child: %v3u, Placement: %d, Flags: %x, Childcnt: %d, childPtr: %d", childStart, placement, validFlags, childCount, newChildPtr);

    if ((chunkFlags & VOXEL_IS_SOLID) != 0)
        return;

    int childOffset = childCount - 1;
    for (int y = 0; y <= 1; y++)
    {
        for (int z = 0; z <= 1; z++)
        {
            for (int x = 0; x <= 1; x++)
            {
                uint bitFlag = (0x4 * y) + (0x2 * z) + (0x1 * x);

                debugPrintfEXT("    PREV: bitFlag: %x, validFlags: %d, Bit: %d", bitFlag, validFlags, validFlags >> bitFlag & 0x1);

                if (((validFlags >> bitFlag) & 0x1) == 0)
                    continue;

                uvec4 childData = imageLoad(o_Generated[p_CurrentMip - 1], ivec3(childStart + uvec3(x, y, z)));

                uint32_t childPlacement = childrenPlacement + childOffset;

                writeNode(childPlacement, childData);

                debugPrintfEXT("    Child: BitFlag: %d, Placement: %d", bitFlag, childPlacement);

                childData.r = (childPlacement >> 16) & 0xFFFF;
                childData.g = childPlacement & 0xFFFF;
                imageStore(o_Generated[p_CurrentMip - 1], ivec3(childStart + uvec3(x, y, z)), childData);

                childOffset--;
            }
        }
    }
}
