#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

layout(constant_id = 0) const int KERNEL_SIZE = 64;

layout(local_size_x = 16, local_size_y = 16) in;

#include "GBufferLayout.other.glsl"

layout(set = 1, binding = 0) uniform sampler2D i_Noise;

layout(buffer_reference, std430) readonly buffer SampleBuffer {
    vec4 samples[KERNEL_SIZE];
};

layout(push_constant) uniform constants
{
    vec4 p_CameraFront;
    vec4 p_CameraRight;
    vec4 p_CameraUp;
    SampleBuffer p_Samples;
    float p_Radius;
    float p_Bias;
};

vec3 changeBasis(vec3 v) {
    return vec3(
        dot(p_CameraRight.xyz, v),
        dot(p_CameraUp.xyz, v),
        dot(p_CameraFront.xyz, v)
    );
}

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Occlusion);
    vec2 uv = vec2(texelCoord) / vec2(size);

    vec2 noiseScale = size / 4.;

    vec4 p = imageLoad(o_Position, texelCoord);
    vec3 pos = p.xyz;
    if (p.a < 1.)
    {
        return;
    }

    vec3 viewPos = changeBasis(pos);

    vec3 normal = imageLoad(o_Normal, texelCoord).xyz;

    vec3 randomVec = texture(i_Noise, uv * noiseScale).xyz;

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(tangent, normal);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.;
    vec3 samplePos;
    float t;
    for (int i = 0; i < KERNEL_SIZE; i++)
    {
        samplePos = TBN * p_Samples.samples[i].xyz;
        samplePos = changeBasis(pos + samplePos * p_Radius);

        vec3 normSample = normalize(samplePos);

        const vec3 normal = vec3(0, 0, 1);
        const vec3 planeCenter = vec3(0, 0, 1);
        float denom = dot(normal, normSample);
        float t = dot(planeCenter, normal) / denom;
        vec3 planeIntersection = normSample * t;

        const float viewportWidth = 2.;
        const float viewportHeight = viewportWidth * float(size.y) / float(size.x);
        const float viewportDepth = 1.;

        vec3 topLeft = vec3(1, 0, 0) * (viewportWidth / 2.)
                + vec3(0, 1, 0) * (viewportHeight / 2.)
                + vec3(0, 0, 1) * viewportDepth;
        vec2 uv = (topLeft.xy - planeIntersection.xy) / vec2(viewportWidth, viewportHeight);
        uv.x = 1. - uv.x;
        uv = clamp(uv, vec2(0.), vec2(1.));

        ivec2 texel = ivec2(uv * size);
        float sampleDepth = changeBasis(imageLoad(o_Position, texel).xyz).z;

        float rangeCheck = smoothstep(0., 1., p_Radius / abs(viewPos.z - sampleDepth));
        occlusion += ((sampleDepth < samplePos.z + p_Bias) ? 1. : 0.) * rangeCheck;
    }
    occlusion = 1. - (occlusion / KERNEL_SIZE);
    imageStore(o_Occlusion, texelCoord, vec4(occlusion));
}
