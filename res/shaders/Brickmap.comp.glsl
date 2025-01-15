#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

#include "Ray.other.glsl"
#include "BrickmapData.other.glsl"

layout(local_size_x = 16, local_size_y = 16) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout(rgba16f, set = 0, binding = 1) uniform image2D o_ComparisonImage;
layout(rgba16f, set = 0, binding = 2) readonly uniform image1D i_Lookup;

layout(push_constant) uniform constants {
    vec3 p_CameraPosition;
    float p_AspectRatio;

    vec3 p_CameraFront;
    int _1;

    vec3 p_CameraRight;
    int _2;

    vec3 p_CameraUp;
    int _3;

    uint32_t _4;
    float p_Size;
    uint32_t p_MaxDepthShown;
    uint32_t p_LOD;

    uint32_t p_MaxHeatShown;
    uint32_t p_Flags;
    uint32_t p_MaxIterations;
    uint32_t p_InitialParent;

    ToBeLoadedBuffer p_ToBeLoaded;
    SuperBrickBuffer p_SuperBrick;
};

struct HitRecord {
    bool hasHit;
    bool hasHitVoxel;
    vec3 brickHitPosition;
    ivec3 brickHitIndex;
    ivec3 superBrickHitIndex;
    vec3 normal;
    vec4 colour;
    int comparisons;
};

HitRecord emptyHit()
{
    HitRecord hit;
    hit.hasHit = false;
    hit.hasHitVoxel = false;
    hit.comparisons = -1;
    hit.colour = vec4(1., 0., 1., 1.);
    return hit;
}

int getsign(float f)
{
    if (f < 0)
        return -1;
    else
        return 1;
}

ivec3 dir_sign(vec3 v)
{
    return ivec3(getsign(v.x), getsign(v.y), getsign(v.z));
}

vec3 calculateNormalFromBounds(Ray ray, float t, vec3 minBound, vec3 maxBound) {
    vec3 position = calculatePosition(ray.origin, ray.direction, t);

    bvec3 minBoundHit = lessThanEqual(position - minBound, vec3(0.0001));
    bvec3 maxBoundHit = greaterThanEqual(position - maxBound, vec3(0.0001));

    if (minBoundHit.x) return vec3(-1, 0, 0);
    if (minBoundHit.y) return vec3(0, -1, 0);
    if (minBoundHit.z) return vec3(0, 0, -1);

    if (maxBoundHit.x) return vec3(1, 0, 0);
    if (maxBoundHit.y) return vec3(0, 1, 0);
    if (maxBoundHit.z) return vec3(0, 0, 1);

    return vec3(0.);
}

vec4 calculateColour(in Brick brick, in ivec3 brickIndex) {
    uint index = 0;
    for (int i = 0; i < brickIndex.y; i++)
    {
        index += bitCount(uint(brick.solidMask[i] & 0xFFFFFFFF));
        index += bitCount(uint((brick.solidMask[i] >> 32) & 0xFFFFFFFF));
    }
    uint bitMask = brickIndex.z * BRICK_SIZE
            + brickIndex.x;
    uint64_t data = brick.solidMask[brickIndex.y];
    uint lower = uint(data & 0xFFFFFFFF);
    uint higher = uint((data >> 32) & 0xFFFFFFFF);
    if (bitMask >= 32) {
        uint higherBitMask = bitMask - 32;
        index += bitCount(lower) + bitCount(((higher >> higherBitMask) << higherBitMask) ^ higher);
    } else {
        index += bitCount(((lower >> bitMask) << bitMask) ^ lower);
    }

    // return vec4(index, vec3(brickIndex));
    return p_SuperBrick.superBrick.colourBuffers.colour[brick.colourPointer].colours[index];
}

void traverseBrick(Ray ray, uint32_t pointer, vec3 minBound, inout int iterations, inout HitRecord hit)
{
    Brick brick = p_SuperBrick.superBrick.bricksBuffer.bricks[pointer];

    const vec3 maxBound = minBound + vec3(BRICK_SIZE);

    float tMin, tMax;
    bool intersectBound = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000000.0, tMin, tMax);

    if (!intersectBound) {
        hit.hasHit = false;
        return;
    }

    vec3 invDir = ray.invDir;
    if (isinf(invDir.x)) invDir.x = 0.;
    if (isinf(invDir.y)) invDir.y = 0.;
    if (isinf(invDir.z)) invDir.z = 0.;

    vec3 rayStart = ray.origin + ray.direction * max(tMin + 0.0001, 0.);

    vec3 entryPos = (rayStart - minBound) / 1.;

    ivec3 brickIndex = clamp(ivec3(entryPos), ivec3(0), ivec3(BRICK_SIZE));
    ivec3 stepDirection = dir_sign(ray.direction);
    vec3 stepSize = invDir * stepDirection;
    vec3 nextDist = (brickIndex - entryPos + max(stepDirection, 0)) * invDir;
    ivec3 stepAxis = ivec3(1, 0, 0);

    vec3 totalDistTraveled = calculatePosition(ray.origin, ray.direction, tMin) - minBound;
    vec3 normal = hit.normal;

    int count = 0;
    for (; iterations < p_MaxIterations; iterations++)
    {
        hit.comparisons++;
        count++;

        int y = brickIndex.y;
        int bitMask = brickIndex.z * BRICK_SIZE
                + brickIndex.x;

        if (((brick.solidMask[y] >> bitMask) & 0x1) == 1)
        {
            hit.colour = calculateColour(brick, brickIndex);
            hit.brickHitPosition = totalDistTraveled;
            hit.brickHitIndex = brickIndex;
            hit.hasHit = true;
            hit.hasHitVoxel = true;
            hit.normal = normal;
            return;
        }

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        totalDistTraveled += stepSize * stepAxis;
        nextDist += stepSize * stepAxis;
        brickIndex += stepDirection * stepAxis;
        normal = -stepAxis;

        bvec3 lower = lessThan(brickIndex, ivec3(0));
        bvec3 higher = greaterThanEqual(brickIndex, ivec3(BRICK_SIZE));
        if (lower.x || lower.y || lower.z || higher.x || higher.y || higher.z)
            break;
    }

    hit.hasHit = false;
    return;
}

HitRecord traverseSuperBrick(Ray ray)
{
    HitRecord hit = emptyHit();

    float tMin, tMax;
    const vec3 minBound = vec3(0);
    const vec3 maxBound = minBound + vec3(SUPER_BRICK_SIZE);
    bool intersectBound = rayBoxIntersect(ray, minBound, maxBound * BRICK_SIZE, 0.0, 1000000.0, tMin, tMax);

    if (!intersectBound) return hit;

    hit.comparisons = 0;

    vec3 invDir = ray.invDir;
    if (isinf(invDir.x)) invDir.x = 0.;
    if (isinf(invDir.y)) invDir.y = 0.;
    if (isinf(invDir.z)) invDir.z = 0.;

    vec3 rayStart = ray.origin + ray.direction * max(tMin + 0.0001, 0);
    vec3 rayEnd = ray.origin + ray.direction * tMax;

    vec3 entryPos = (rayStart - minBound) / BRICK_SIZE;

    ivec3 superBrickIndex = clamp(ivec3(entryPos), ivec3(0), ivec3(SUPER_BRICK_SIZE));
    ivec3 stepDirection = dir_sign(ray.direction);
    vec3 stepSize = invDir * stepDirection;
    vec3 nextDist = (superBrickIndex - entryPos + max(stepDirection, 0)) * invDir;

    vec3 normal = calculateNormalFromBounds(ray, tMin, minBound, maxBound);

    for (int iterations = 0; iterations < p_MaxIterations; iterations++)
    {
        hit.comparisons++;

        uint32_t index = superBrickIndex.y * SUPER_BRICK_SIZE * SUPER_BRICK_SIZE
                + superBrickIndex.z * SUPER_BRICK_SIZE
                + superBrickIndex.x;

        if (index >= SUPER_BRICK_SIZE * SUPER_BRICK_SIZE * SUPER_BRICK_SIZE) {
            break;
        }

        uint32_t data = p_SuperBrick.superBrick.data[index];
        uint32_t flags = bitfieldExtract(data, 1, 3);

        uint32_t is_valid = bitfieldExtract(data, 0, 1);
        uint32_t unused = bitfieldExtract(data, 16, 16);

        uint32_t brickPointer = bitfieldExtract(data, 4, 12);

        if (is_valid == 0) {
            Brick brick = p_SuperBrick.superBrick.bricksBuffer.bricks[brickPointer];

            hit.colour = vec4(brick.lodR / 255., brick.lodG / 255., brick.lodB / 255., 1.);
            hit.hasHit = true;

            if (p_ToBeLoaded.currentPointer >= p_ToBeLoaded.maxSize) {
                return hit;
            }

            uint32_t new_data = bitfieldInsert(data, 1, 1, 1);
            uint32_t previous = atomicExchange(p_SuperBrick.superBrick.data[index], new_data);

            uint32_t previously_requested = bitfieldExtract(previous, 1, 1);
            if (previously_requested == 0) {
                uint32_t writePointer = atomicAdd(p_ToBeLoaded.currentPointer, 1);
                if (writePointer < p_ToBeLoaded.maxSize) {
                    p_ToBeLoaded.toBeLoaded[writePointer] = index;
                } else {
                    atomicExchange(p_SuperBrick.superBrick.data[index], previous);
                }
            }

            return hit;
        } else { // Brick is already loaded
            if ((flags & LOADED_BRICK_FLAG_EMPTY) == 0) // Not Empty
            {
                vec3 brickMinBound = superBrickIndex * BRICK_SIZE;

                hit.normal = normal;
                traverseBrick(ray, brickPointer, brickMinBound, iterations, hit);

                if (hit.hasHit) {
                    hit.superBrickHitIndex = superBrickIndex;
                    return hit;
                }
            }
        }

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        superBrickIndex += stepDirection * stepAxis;
        nextDist += stepSize * stepAxis;

        normal = -stepAxis;

        bvec3 lower = lessThan(superBrickIndex, vec3(0));
        bvec3 higher = greaterThanEqual(superBrickIndex, vec3(SUPER_BRICK_SIZE));
        if (lower.x || lower.y || lower.z || higher.x || higher.y || higher.z)
            break;
    }

    hit.hasHit = false;
    return hit;
}

vec3 calculateHitPosition(in HitRecord hit) {
    return hit.superBrickHitIndex * SUPER_BRICK_SIZE * BRICK_SIZE + hit.brickHitPosition;
}

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

    ivec3 brickIndex;
    HitRecord hit = traverseSuperBrick(ray);

    if (hit.hasHit) {
        vec3 hitPosition = calculateHitPosition(hit);
        vec4 lookupColour = hit.colour;

        vec4 colour = lookupColour;
        if (hit.hasHitVoxel) {
            const vec3 lightPosition = vec3(0, -100., 0);
            const vec4 lightColour = vec4(1.);

            const vec3 lightDir = normalize(lightPosition - hitPosition);

            float diff = max(dot(hit.normal, lightDir), 0.);
            vec4 diffuse = lightColour * diff;

            Ray shadowRay;
            shadowRay.origin = hitPosition - (ray.direction * 0.001);
            shadowRay.direction = lightPosition - shadowRay.origin;

            HitRecord shadow = traverseSuperBrick(ray);

            const float ambientStrength = 0.7;
            vec4 ambient = lightColour * ambientStrength;

            float diffStrength = 1.;
            if (shadow.hasHit)
                diffStrength = 0.1;

            colour = (ambient + diffuse * diffStrength) * colour;
        }
        // imageStore(o_Image, texelCoord, hit.colour);
        imageStore(o_Image, texelCoord, colour);
    }

    if (hit.comparisons >= 0) {
        vec4 lowestHitColour = vec4(0.5, 0., 0.5, 1.0);
        vec4 highestHitColour = vec4(1., 1., 0., 1.0);
        float mixAmount = hit.comparisons / float(p_MaxHeatShown);

        imageStore(o_ComparisonImage, texelCoord, mix(lowestHitColour, highestHitColour, mixAmount));
    }
}
