struct Voxel
{
    int lookupIndex;
};

layout (buffer_reference, std430) readonly buffer VoxelBuffer
{
    Voxel voxels[];
};
