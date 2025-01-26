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

const vec4 cursorColour = vec4(vec3(0.3), 1.);
const float cursorWidth = 20.;

layout(push_constant) uniform constants {
    vec4 p_SunDir;
    vec4 p_SkyColour;
    vec4 p_LightColour;
};

bool shouldColourCursor(vec2 uv, vec2 pixelSize) {
    uv = abs(uv - vec2(0.5));

    vec2 pixelConversion = vec2(uv / pixelSize);
    bool canRender = true;

    bvec2 outsideBounds = greaterThan(abs(pixelConversion), ivec2(cursorWidth));
    if (outsideBounds.x || outsideBounds.y) {
        canRender = false;
    }

    bool outsideOuterCircle = length(vec2(pixelConversion)) > float(cursorWidth);
    if (outsideOuterCircle)
        canRender = false;

    bool insideOuterCircle = length(vec2(pixelConversion)) < float(cursorWidth * 0.8);
    if (insideOuterCircle)
        canRender = false;

    float seperation = 0.4;
    if (pixelConversion.x < cursorWidth * seperation || pixelConversion.y < cursorWidth * seperation)
    {
        canRender = false;
    }

    float middlePointerWidth = 0.055;
    float middlePointerHeight = 0.60;
    if (pixelConversion.x < cursorWidth * middlePointerWidth && pixelConversion.y < cursorWidth * middlePointerHeight)
    {
        canRender = true;
    }

    if (pixelConversion.y < cursorWidth * middlePointerWidth && pixelConversion.x < cursorWidth * middlePointerHeight)
    {
        canRender = true;
    }

    return canRender;
}

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Position);
    vec2 uv = vec2(texelCoord) / vec2(size);

    vec4 p = imageLoad(o_Position, texelCoord);
    ivec4 n = imageLoad(o_Normal, texelCoord);
    vec4 colour = imageLoad(o_Colour, texelCoord);

    bool hitVoxel = p.a > 0.;
    vec3 position = p.xyz;

    ivec3 normal = n.xyz;
    bool inShadow = n.a > 0;

    if (hitVoxel) {
        float diff = max(dot(normal, p_SunDir.xyz), 0.);
        vec4 diffuse = p_LightColour * diff;

        const float ambientStrength = 0.7;
        vec4 ambient = p_LightColour * ambientStrength;

        float diffStrength = 1.;
        if (inShadow) {
            diffStrength = 0.1;
        }

        colour = (ambient + diffuse * diffStrength) * colour;
    } else if (colour.a < 1.) {
        colour = p_SkyColour;
    }

    if (shouldColourCursor(uv, vec2(1.) / vec2(size))) {
        colour = mix(colour, cursorColour, 0.7);
        // imageStore(o_Image, texelCoord, mix(colour, cursorColour, 0.7));
    }

    imageStore(o_TargetImage, texelCoord, colour);
}
