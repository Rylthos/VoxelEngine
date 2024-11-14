#version 460

#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "Voxel.other.glsl"
#include "Ray.other.glsl"
#include "Tree.other.glsl"

layout (local_size_x = 16, local_size_y = 16) in;

layout (rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout (rgba16f, set = 0, binding = 1) uniform image2D o_ComparisonImage;
layout (rgba16f, set = 0, binding = 2) readonly uniform image1D i_Lookup;

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


    const vec3 clearColour = vec3(0.1);
    imageStore(o_ComparisonImage, texelCoord, vec4(clearColour, 0.0));
    imageStore(o_Image, texelCoord, vec4(clearColour, 1.0));

    Ray ray = generateRay(uv,
                        vec3(p_CameraPosition), vec3(p_CameraFront),
                        vec3(p_CameraRight),
                        vec3(p_CameraUp));

    // Grid grid = generateGrid(vec3(0.), p_Dimensions, p_Size, p_Voxels);

    uvec3 hitIndex;
    Voxel hitVoxel;
    vec3 normal;
    int comparisons;
    // bool didHit = traverse(ray, grid,
    //         hitIndex, hitVoxel, normal, comparisons);

    // vec3 hitPosition = worldPositionFromIndex(grid, hitIndex);
    //
    // if (comparisons >= 0)
    // {
    //     const vec4 noComparisons = vec4(1., 0., 1., 0.2);
    //     const vec4 maxComparisons = vec4(1., 1., 0., 0.2);
    //     vec4 comparisonColour = mix(noComparisons, maxComparisons,
    //             float(comparisons) / MAX_COMPARISONS);
    //
    //     imageStore(o_ComparisonImage, texelCoord, comparisonColour);
    //     // imageStore(o_ComparisonImage, texelCoord, vec4(abs(normal), comparisons));
    // }
    //
    // if (didHit)
    // {
    //     vec4 lookupColour = imageLoad(i_Lookup, hitVoxel.lookupIndex);
    //
    //     const vec3 lightPosition = vec3(128., 128, 128.);
    //     const vec4 lightColour = vec4(1.);
    //
    //     const vec3 lightDir = normalize(lightPosition - hitPosition);
    //     float diff = max(dot(normal, lightDir), 0.);
    //
    //     const float ambientStrength = 0.5;
    //     vec4 ambient = lightColour * ambientStrength;
    //     vec4 diffuse = lightColour * diff;
    //
    //     vec4 colour = (ambient + diffuse) * lookupColour;
    //
    //     imageStore(o_Image, texelCoord, colour);
    // }
}
