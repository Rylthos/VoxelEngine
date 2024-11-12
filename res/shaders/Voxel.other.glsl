struct GroupedVoxel
{
    int groupedIndex;
};

struct Voxel
{
    int lookupIndex;
};

layout (buffer_reference, std430) readonly buffer VoxelBuffer
{
    GroupedVoxel voxels[];
};

uint getGroupedIndex(uvec3 dimensions, uvec3 index)
{
    return (index.x / 4) +
        (index.z * (dimensions.x / 4)) +
        (index.y * (dimensions.x / 4) * dimensions.z);
}

GroupedVoxel getGroupedVoxel(VoxelBuffer voxels, uvec3 dimensions, uvec3 index)
{
    uint groupedIndex = getGroupedIndex(dimensions, index);
    return voxels.voxels[groupedIndex];
}

Voxel voxelFromGroup(GroupedVoxel grouped, uint offset)
{
    Voxel voxel;

    voxel.lookupIndex = (grouped.groupedIndex >> (8 * offset)) & 0xFF;

    return voxel;
}

Voxel getVoxelFromGrid(VoxelBuffer voxels, uvec3 dimensions, uvec3 index)
{
    GroupedVoxel grouped = getGroupedVoxel(voxels, dimensions, index);
    uint offset = index.x % 4;
    return voxelFromGroup(grouped, offset);
}
