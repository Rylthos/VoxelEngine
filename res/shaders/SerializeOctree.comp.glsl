#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : enable

layout(local_size_x = 2, local_size_y = 2, local_size_z = 2) in;

layout(rgba16ui, set = 0, binding = 0) uniform uimage3D o_Generated[4];

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
    uint32_t p_NumNodes;
    SVONodeBuffer p_Nodes;
};

void writeNode(uint32_t placement, uvec4 data)
{
    p_Nodes.nodes[placement].childPtr = 0;
    p_Nodes.nodes[placement].unused = int8_t((data.b >> 8) & 0xFF);
    p_Nodes.nodes[placement].materialIndex = uint8_t(data.b & 0xFF);
    p_Nodes.nodes[placement].validMask = uint8_t((data.a >> 8) & 0xFF);
    p_Nodes.nodes[placement].leafMask = uint8_t(data.a & 0xFF);
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
    uint32_t placement = uint32_t(data.r << 16) | data.g;
    uint validFlags = (data.a >> 8) & 0xFF;

    int childCount = bitCount(validFlags);
    uint childrenPlacement = atomicAdd(o_Shared.counter, childCount);

    writeNode(placement, data);
    uint32_t newChildPtr = childrenPlacement - placement;
    p_Nodes.nodes[placement].childPtr = newChildPtr;

    uvec3 childStart = gl_GlobalInvocationID.xyz * 2;

    int childOffset = childCount - 1;
    for (int y = 0; y <= 1; y++)
    {
        for (int z = 0; z <= 1; z++)
        {
            for (int x = 0; x <= 1; x++)
            {
                int bitFlag = (0x4 * y) + (0x2 * z) + (0x1 * x);

                uvec4 data = imageLoad(o_Generated[p_CurrentMip - 1], ivec3(childStart + uvec3(x, y, z)));

                if (((validFlags >> bitFlag) & 0x1) == 0)
                    continue;

                uint32_t childPlacement = childrenPlacement + childOffset;
                writeNode(childPlacement, data);

                data.r = (placement >> 16) & 0xFFFF;
                data.g = childPlacement & 0xFFFF;
                imageStore(o_Generated[p_CurrentMip - 1], ivec3(childStart + uvec3(x, y, z)), data);

                childOffset--;
            }
        }
    }
}
