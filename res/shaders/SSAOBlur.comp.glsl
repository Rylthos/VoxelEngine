#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

layout(local_size_x = 16, local_size_y = 16) in;

#include "GBufferLayout.other.glsl"

layout(r32f, set = 1, binding = 0) uniform image2D i_Input;

layout(push_constant) uniform constants {
    int p_Axis;
    int p_BlurSize;
};

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);

    ivec2 blurDirection[2] = ivec2[](ivec2(1., 0.), ivec2(0., 1.));

    int totalSamples = p_BlurSize * 2 + 1;
    float weight = 1. / totalSamples;

    float value = 0;
    if (p_Axis == 0)
        value = imageLoad(o_Occlusion, texelCoord).x * weight;
    else
        value = imageLoad(i_Input, texelCoord).x * weight;

    for (int i = 1; i <= p_BlurSize; i++)
    {
        ivec2 offsetValue = i * blurDirection[p_Axis];
        if (p_Axis == 0) {
            value += (imageLoad(o_Occlusion, texelCoord + offsetValue).x) * weight;
            value += (imageLoad(o_Occlusion, texelCoord - offsetValue).x) * weight;
        } else {
            value += (imageLoad(i_Input, texelCoord + offsetValue).x) * weight;
            value += (imageLoad(i_Input, texelCoord - offsetValue).x) * weight;
        }
    }
    if (p_Axis == 0) {
        imageStore(i_Input, texelCoord, vec4(value));
    } else {
        imageStore(o_Occlusion, texelCoord, vec4(value));
    }
}
