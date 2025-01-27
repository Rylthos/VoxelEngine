#version 460

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Position;
layout(rgba8i, set = 0, binding = 1) uniform iimage2D o_Normal;
layout(rgba16f, set = 0, binding = 2) uniform image2D o_Colour;
layout(r32f, set = 0, binding = 3) uniform image2D o_Occlusion;

layout(set = 1, binding = 0) uniform sampler2D i_Noise;

layout(push_constant) uniform constants
{
    vec4 p_Samples[64];
};

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Position);
    vec2 uv = vec2(texelCoord) / vec2(size);

    vec2 noiseScale = size / 4.;
}
