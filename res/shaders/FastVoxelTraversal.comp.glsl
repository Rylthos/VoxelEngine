#version 460

#extension GL_EXT_buffer_reference : enable
#extension GL_GOOGLE_include_directive : require

#include "Voxel.other.glsl"
#include "RayGrid.other.glsl"

#define MAX_COMPARISONS 256

layout (local_size_x = 16, local_size_y = 16) in;

layout (rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout (rgba16f, set = 0, binding = 1) uniform image2D o_ComparisonImage;

layout (push_constant) uniform constants
{
    vec4 p_CameraPosition;
    vec4 p_CameraFront;
    vec4 p_CameraRight;
    vec4 p_CameraUp;
    uvec3 p_Dimensions;
    float p_Size;
    VoxelBuffer p_Voxels;
};

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Image);
    vec2 uv = vec2(texelCoord) / vec2(size);


    const vec3 clearColour = vec3(0.2);
    imageStore(o_ComparisonImage, texelCoord, vec4(clearColour, 1.0));
    imageStore(o_Image, texelCoord, vec4(clearColour, 1.0));

    Ray ray = generateRay(uv,
                        vec3(p_CameraPosition),
                        vec3(p_CameraFront),
                        vec3(p_CameraRight),
                        vec3(p_CameraUp));

    Grid grid = generateGrid(vec3(0.), p_Dimensions, p_Size, p_Voxels);

    uvec3 hitIndex;
    Voxel hitVoxel;
    vec3 normal;
    int comparisons;
    bool didHit = traverse(ray, grid, 0., 1000.,
            hitIndex, hitVoxel, normal, comparisons);

    if (comparisons >= 0)
    {
        const vec4 noComparisons = vec4(1., 0., 1., 0.2);
        const vec4 maxComparisons = vec4(1., 1., 0., 0.2);
        vec4 comparisonColour = mix(noComparisons, maxComparisons,
                float(comparisons) / MAX_COMPARISONS);
        imageStore(o_ComparisonImage, texelCoord, comparisonColour);
    }

    if (didHit)
    {
        imageStore(o_Image, texelCoord, vec4(abs(normal), 1.0));
    }
}
