#version 460

#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Position;
layout(rgba8i, set = 0, binding = 1) uniform iimage2D o_Normal;
layout(rgba16f, set = 0, binding = 2) uniform image2D o_Colour;
layout(r32f, set = 0, binding = 3) uniform image2D o_Occlusion;

layout(set = 1, binding = 0) uniform sampler2D i_Noise;

#define KERNEL_SIZE 64

layout(buffer_reference, std430) readonly buffer SampleBuffer {
    vec4 samples[];
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
    ivec2 size = imageSize(o_Position);
    vec2 uv = vec2(texelCoord) / vec2(size);

    vec2 noiseScale = size / 4.;

    vec4 p = imageLoad(o_Position, texelCoord);
    vec3 pos = p.xyz;
    if (p.a < 1.)
    {
        imageStore(o_Occlusion, texelCoord, vec4(-1));
        return;
    }

    vec3 normal = imageLoad(o_Normal, texelCoord).xyz;

    vec3 randomVec = texture(i_Noise, uv * noiseScale).xyz;

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(changeBasis(tangent), changeBasis(bitangent), changeBasis(normal));

    float occlusion = 0.;
    const int steps = 1;
    int included = 0;
    for (int i = 0; i < steps; i++)
    {
        vec3 samplePos = TBN * p_Samples.samples[i].xyz;
        samplePos = pos + samplePos * p_Radius;

        float denom = dot(vec3(0, 0, 1), samplePos);
        float t = 1. / denom;

        vec2 uv = samplePos.xy * t;
        uv.y = -uv.y;
        uv = (uv + 1.) / 2.;

        // if (uv.x < 0. || uv.y < 0. || uv.x > 1. || uv.y > 1.)
        //     continue;

        ivec2 texel = ivec2(uv * size);
        float sampleDepth = imageLoad(o_Position, texel).z;

        imageStore(o_Colour, texelCoord, vec4(texel, 0., 1.));

        const float bias = 0.;

        // float rangeCheck = smoothstep(0., 1., p_Radius / abs(pos.z - sampleDepth));
        // occlusion += ((sampleDepth >= samplePos.z + bias) ? 1. : 0.) * rangeCheck;
        occlusion += ((sampleDepth < samplePos.z + bias) ? 1. : 0.);
        included += 1;
    }
    occlusion = 1. - (occlusion / included);
    if (occlusion < 1.)
        debugPrintfEXT("occlusion: %f | Normal: %v3f | Random : %v3f", occlusion, normal, randomVec);
    imageStore(o_Occlusion, texelCoord, vec4(occlusion));
}
