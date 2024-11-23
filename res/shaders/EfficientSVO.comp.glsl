#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

#define MIN_T 0.000001
#define MAX_T 10000.

#include "Ray.other.glsl"
#include "Octree.other.glsl"

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout(rgba16f, set = 0, binding = 1) uniform image2D o_ComparisonImage;
layout(rgba16f, set = 0, binding = 2) readonly uniform image1D i_Lookup;

#define FLAGS_SHOW_HEAT_MAP 1

layout(push_constant) uniform constants {
    vec3 p_CameraPosition;
    float p_AspectRatio;
    vec4 p_CameraFront;
    vec4 p_CameraRight;
    vec4 p_CameraUp;
    uint32_t p_Dimension;
    float p_Size;
    uint32_t p_MaxDepthShown;
    uint32_t p_LOD;
    uint32_t p_MaxHeatShown;
    uint32_t p_Flags;
    uint32_t p_MaxIterations;
    uint32_t p_InitialParent;
    SVONodeBuffer p_Tree;
};

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Image);
    vec2 uv = vec2(texelCoord) / vec2(size);

    const vec3 clearColour = vec3(0.1);
    imageStore(o_ComparisonImage, texelCoord, vec4(clearColour, 0.0));
    imageStore(o_Image, texelCoord, vec4(clearColour, 0.0));

    Ray ray = generateRay(uv,
            p_CameraPosition, vec3(p_CameraFront),
            vec3(p_CameraRight),
            vec3(p_CameraUp), p_AspectRatio);

    HitRecord hit = castRay(p_Tree, ray,
            p_Dimension, p_Size, p_MaxIterations, p_LOD);

    if (hit.t >= 0)
    {
        vec4 lookupColour = imageLoad(i_Lookup, int(hit.materialIndex));

        const vec3 lightPosition = vec3(p_Dimension / 2., -100., p_Dimension / 2.);
        const vec4 lightColour = vec4(1.);

        const vec3 lightDir = normalize(lightPosition - hit.position);

        float diff = max(dot(hit.normal, lightDir), 0.);
        vec4 diffuse = lightColour * diff;

        Ray shadowRay;
        shadowRay.origin = calculatePosition(ray.origin, ray.direction, hit.t - MIN_T);
        shadowRay.direction = lightPosition - shadowRay.origin;

        HitRecord shadow = castRay(p_Tree, shadowRay,
                p_Dimension, p_Size, p_MaxIterations, p_LOD);

        const float ambientStrength = 0.5;
        vec4 ambient = lightColour * ambientStrength;

        float diffStrength = 1.;
        if (shadow.t >= 0.)
            diffStrength = 0.1;

        vec4 colour = (ambient + diffuse * diffStrength) * lookupColour;

        imageStore(o_Image, texelCoord, colour);
    }

    if (hit.deepest >= 0)
    {
        vec4 lowestHitColour = vec4(0.5, 0., 0.5, 1.0);
        vec4 highestHitColour = vec4(1., 1., 0., 1.0);
        float mixAmount = 0;
        if ((p_Flags & FLAGS_SHOW_HEAT_MAP) != 0x0)
        {
            mixAmount = hit.heatMap / float(p_MaxHeatShown);
        }
        else
        {
            mixAmount = hit.deepest / float(p_MaxDepthShown);
        }
        imageStore(o_ComparisonImage, texelCoord, mix(lowestHitColour, highestHitColour, mixAmount));
    }
}
