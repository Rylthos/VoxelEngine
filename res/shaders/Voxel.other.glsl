struct Voxel
{
    vec4 colour;
};

layout (buffer_reference, std430) readonly buffer VoxelBuffer
{
    Voxel voxels[];
};
