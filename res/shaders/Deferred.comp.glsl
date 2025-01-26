#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Position;
layout(rgba8i, set = 0, binding = 1) uniform iimage2D o_Normal;
layout(rgba16f, set = 0, binding = 2) uniform image2D o_Colour;
layout(rgba16f, set = 1, binding = 0) uniform image2D o_TargetImage;

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Position);
    vec2 uv = vec2(texelCoord) / vec2(size);

    const vec3 clearColour = vec3(0.1);

    vec4 p = imageLoad(o_Position, texelCoord);
    ivec4 n = imageLoad(o_Normal, texelCoord);
    vec4 colour = imageLoad(o_Colour, texelCoord);

    bool hitVoxel = p.a > 0.;
    vec3 position = p.xyz;

    ivec3 normal = n.xyz;
    bool inShadow = n.a > 0;

    if (hitVoxel) {
        vec3 sunDir = vec3(0., -1., 0.);
        vec4 lightColour = vec4(1.);

        float diff = max(dot(normal, sunDir), 0.);
        vec4 diffuse = lightColour * diff;

        const float ambientStrength = 0.7;
        vec4 ambient = lightColour * ambientStrength;

        float diffStrength = 1.;
        if (inShadow) {
            diffStrength = 0.1;
        }

        colour = (ambient + diffuse * diffStrength) * colour;
    }

    imageStore(o_TargetImage, texelCoord, colour);
}
