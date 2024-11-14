#version 460
#extension GL_EXT_shader_explicit_arithmetic_types : enable

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

#include "Ray.other.glsl"

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout(rgba16f, set = 0, binding = 1) uniform image2D o_ComparisonImage;
layout(rgba16f, set = 0, binding = 2) readonly uniform image1D i_Lookup;

struct Node {
    uint32_t data;
};

layout(buffer_reference, std430) readonly buffer NodeBuffer {
    Node nodes[];
};

layout(push_constant) uniform constants {
    vec4 p_CameraPosition;
    vec4 p_CameraFront;
    vec4 p_CameraRight;
    vec4 p_CameraUp;
    uvec3 p_Dimensions;
    float p_Size;
    NodeBuffer p_Tree;
};

struct HitRecord {
    float t;
    vec3 position;
};

HitRecord castRay(uint root, Ray ray) {
    const int sMax = 23;
    const float epsilon = pow(2., -sMax);

    vec3 position = ray.origin;
    vec3 direction = ray.direction;

    uvec2 stack[sMax + 1];

    if (abs(direction.x) < epsilon)
        direction.x = sign(direction.x) * epsilon;

    if (abs(direction.y) < epsilon)
        direction.y = sign(direction.y) * epsilon;

    if (abs(direction.z) < epsilon)
        direction.z = sign(direction.z) * epsilon;

    vec3 t_coef = 1.0 / -abs(direction);
    vec3 t_bias = t_coef * position;

    int octant_mask = 7;
    if (direction.x > 0.0) {
        octant_mask ^= 1;
        t_bias.x = 3.0 * t_coef.x - t_bias.x;
    }

    if (direction.y > 0.0) {
        octant_mask ^= 2;
        t_bias.y = 3.0 * t_coef.y - t_bias.y;
    }

    if (direction.z > 0.0) {
        octant_mask ^= 4;
        t_bias.z = 3.0 * t_coef.z - t_bias.z;
    }

    float tMin = max(max(2. * t_coef.x - t_bias.x, 2. * t_coef.y - t_bias.y),
            2. * t_coef.z - t_bias.z);

    float tMax = min(min(t_coef.x - t_bias.x, t_coef.y - t_bias.y),
            t_coef.z - t_bias.z);

    float h = tMax;
    tMin = max(tMin, 0.0f);
    tMax = min(tMax, 1.0f);

    uint parent = root;
    uint32_t childDescriptor = 0;
    int idx = 0;
    vec3 pos = vec3(1.0f);
    int scale = sMax - 1;
    float scaleExp2 = 0.5;

    vec3 test = 1.5 * t_coef - t_bias;
    if (test.x > tMin) {
        idx ^= 1;
        pos.x = 1.5;
    }

    if (test.y > tMin) {
        idx ^= 2;
        pos.y = 1.5;
    }

    if (test.z > tMin) {
        idx ^= 4;
        pos.z = 1.5;
    }

    while (scale < sMax)
    {
        if (childDescriptor == 0)
            childDescriptor = p_Tree.nodes[parent].data;

        vec3 t_corner = pos * t_coef - t_bias;
        float tc_max = min(min(t_corner.x, t_corner.y), t_corner.z);

        int childShift = idx ^ octant_mask;
        uint childMasks = childDescriptor << childShift;
        if ((childMasks & 0x8000) != 0 && tMin < tMax)
        {
            float tv_max = min(tMax, tc_max);
            float halfScale = scaleExp2 * 0.5;
            vec3 center = halfScale * t_coef + t_corner;

            if (tMin <= tv_max)
            {
                if ((childMasks & 0x0080) == 0)
                    break;

                // PUSH
                // Write Parent to stack
                if (tc_max < h)
                    stack[scale] = uvec2(parent, floatBitsToInt(tMax));

                h = tc_max;

                uint ofs = uint(childDescriptor >> 17);
                if ((childDescriptor & 0x10000) != 0) // Far
                    ofs = p_Tree.nodes[parent + ofs * 2].data;

                ofs += bitCount(childMasks & 0x7F);
                parent += ofs * 2;

                idx = 0;
                scale--;
                scaleExp2 = halfScale;

                if (center.x > tMin) {
                    idx ^= 1;
                    pos.x += scaleExp2;
                }

                if (center.y > tMin) {
                    idx ^= 2;
                    pos.y += scaleExp2;
                }

                if (center.z > tMin) {
                    idx ^= 4;
                    pos.z += scaleExp2;
                }

                tMax = tv_max;
                childDescriptor = 0;
                continue;
            }
        }

        // ADVANCEk
        // Step along the ray
        int stepMask = 0;
        if (t_corner.x <= tc_max) {
            stepMask ^= 1;
            pos.x -= scaleExp2;
        }

        if (t_corner.y <= tc_max) {
            stepMask ^= 2;
            pos.y -= scaleExp2;
        }

        if (t_corner.z <= tc_max) {
            stepMask ^= 4;
            pos.z -= scaleExp2;
        }

        tMin = tc_max;
        idx ^= stepMask;

        if ((idx & stepMask) != 0)
        {
            // POP
            uint differingBits = 0;
            if ((stepMask & 1) != 0)
                differingBits |= floatBitsToInt(pos.x) ^ floatBitsToInt(pos.x + scaleExp2);

            if ((stepMask & 2) != 0)
                differingBits |= floatBitsToInt(pos.y) ^ floatBitsToInt(pos.y + scaleExp2);

            if ((stepMask & 4) != 0)
                differingBits |= floatBitsToInt(pos.z) ^ floatBitsToInt(pos.z + scaleExp2);

            scale = (floatBitsToInt(float(differingBits)) >> 23) - 127;
            scaleExp2 = intBitsToFloat((scale - sMax + 127) << 23);

            uvec2 stackEntry = stack[scale];
            parent = stackEntry.x;
            tMax = uintBitsToFloat(stackEntry.y);

            ivec3 sh = floatBitsToInt(pos) >> scale;
            pos = intBitsToFloat(sh << scale);
            idx = (sh.x & 1) | ((sh.y & 1) << 1) | ((sh.z & 1) << 2);

            h = 0.0f;
            childDescriptor = 0;
        }
    }

    if (scale >= sMax)
        tMin = 2.0;

    if ((octant_mask & 1) == 0) pos.x = 3.0f - scaleExp2 - pos.x;
    if ((octant_mask & 2) == 0) pos.y = 3.0f - scaleExp2 - pos.y;
    if ((octant_mask & 4) == 0) pos.z = 3.0f - scaleExp2 - pos.z;

    HitRecord hit;
    hit.t = tMin;
    hit.position.x = min(max(position.x + tMin * direction.x, position.x + epsilon), pos.x + scaleExp2 - epsilon);
    hit.position.y = min(max(position.y + tMin * direction.y, position.y + epsilon), pos.y + scaleExp2 - epsilon);
    hit.position.z = min(max(position.z + tMin * direction.z, position.z + epsilon), pos.z + scaleExp2 - epsilon);

    return hit;
}

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

    HitRecord hit = castRay(0, ray);

    imageStore(o_Image, texelCoord, vec4(hit.position, hit.t));
}
