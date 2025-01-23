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
// layout(rgba16f, set = 0, binding = 2) readonly uniform image1D i_Lookup;

layout(buffer_reference, std430) buffer FeedbackBuffer {
    ivec3 superBrickIndex;
    int hasHitBrick;
    ivec3 brickIndex;
    int hasHitVoxel;
    ivec3 voxelIndex;
    int _3;
    ivec3 voxelNormal;
    int _4;
};

layout(push_constant) uniform constants {
    vec3 p_CameraPosition;
    float p_AspectRatio;

    vec4 p_CameraFront;
    vec4 p_CameraRight;
    vec4 p_CameraUp;

    vec4 p_SunDir;

    float p_Size;
    uint32_t p_MaxDepthShown;
    uint32_t p_LOD;
    uint32_t _1;

    uint32_t p_MaxHeatShown;
    uint32_t p_Flags;
    uint32_t p_MaxIterations;
    uint32_t _2;

    ToBeLoadedBuffer p_ToBeLoaded;
    SuperBrickBuffer p_SuperBrick;
    FeedbackBuffer p_Feedback;
};

struct HitRecord {
    bool hasHitBrick;
    bool hasHitVoxel;
    ivec3 voxelHitIndex;
    ivec3 brickHitIndex;
    ivec3 normal;
    vec4 colour;
    int comparisons;
};

const vec4 cursorColour = vec4(vec3(0.3), 1.);
const float cursorWidth = 20.;

HitRecord emptyHit()
{
    HitRecord hit;
    hit.hasHitBrick = false;
    hit.hasHitVoxel = false;
    hit.comparisons = -1;
    hit.colour = vec4(1., 0., 1., 1.);

    hit.voxelHitIndex = ivec3(0);
    hit.brickHitIndex = ivec3(0);
    hit.normal = ivec3(0);
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

ivec3 calculateNormalFromBounds(Ray ray, float t, vec3 minBound, vec3 maxBound) {
    vec3 position = calculatePosition(ray.origin, ray.direction, t);

    bvec3 minBoundHit = lessThanEqual(position - minBound, vec3(0.0001));
    bvec3 maxBoundHit = greaterThanEqual(position - maxBound, vec3(0.0001));

    if (minBoundHit.x) return ivec3(-1, 0, 0);
    if (minBoundHit.y) return ivec3(0, -1, 0);
    if (minBoundHit.z) return ivec3(0, 0, -1);

    if (maxBoundHit.x) return ivec3(1, 0, 0);
    if (maxBoundHit.y) return ivec3(0, 1, 0);
    if (maxBoundHit.z) return ivec3(0, 0, 1);

    return ivec3(0.);
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
    return p_SuperBrick.superBrick.colourBuffers.colours[brick.colourPointer + index];
}

void traverseBrick(Ray ray, uint32_t pointer, vec3 minBound, inout int iterations, inout HitRecord hit)
{
    Brick brick = p_SuperBrick.superBrick.bricksBuffer.bricks[pointer];

    const vec3 maxBound = minBound + vec3(BRICK_SIZE);

    float tMin, tMax;
    bool intersectBound = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000000.0, tMin, tMax);

    if (!intersectBound) {
        hit.hasHitBrick = false;
        return;
    }

    vec3 invDir = ray.invDir;

    vec3 rayStart = ray.origin + ray.direction * max(tMin + 0.0001, 0.);

    vec3 entryPos = (rayStart - minBound) / 1.;

    ivec3 voxelIndex = clamp(ivec3(entryPos), ivec3(0), ivec3(BRICK_SIZE));
    ivec3 stepDirection = dir_sign(ray.direction);
    vec3 stepSize = invDir * stepDirection;
    vec3 nextDist = (voxelIndex - entryPos + max(stepDirection, 0)) * invDir;
    ivec3 stepAxis = ivec3(1, 0, 0);

    vec3 totalDistTraveled = calculatePosition(ray.origin, ray.direction, tMin);
    ivec3 normal = hit.normal;

    int count = 0;
    for (; iterations < p_MaxIterations; iterations++)
    {
        bvec3 lower = lessThan(voxelIndex, ivec3(0));
        bvec3 higher = greaterThanEqual(voxelIndex, ivec3(BRICK_SIZE));
        if (lower.x || lower.y || lower.z || higher.x || higher.y || higher.z)
            break;

        hit.comparisons++;
        count++;

        int y = voxelIndex.y;
        int bitMask = voxelIndex.z * BRICK_SIZE
                + voxelIndex.x;

        if (((brick.solidMask[y] >> bitMask) & 0x1) == 1)
        {
            hit.colour = calculateColour(brick, voxelIndex);
            hit.voxelHitIndex = voxelIndex;
            hit.hasHitBrick = true;
            hit.hasHitVoxel = true;
            hit.normal = normal;
            return;
        }

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        totalDistTraveled += stepSize * stepAxis;
        nextDist += stepSize * stepAxis;
        voxelIndex += stepDirection * stepAxis;
        normal = ivec3(-(stepDirection * stepAxis));
    }

    hit.hasHitBrick = false;
    return;
}

HitRecord traverseSuperBrick(in Ray ray)
{
    HitRecord hit = emptyHit();

    float tMin, tMax;
    const vec3 minBound = vec3(0);
    const vec3 maxBound = minBound + vec3(SUPER_BRICK_SIZE) * BRICK_SIZE;
    bool intersectBound = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000000.0, tMin, tMax);

    if (!intersectBound) return hit;

    hit.comparisons = 0;

    vec3 invDir = ray.invDir;

    vec3 rayStart = ray.origin + ray.direction * max(tMin + 0.0001, 0);
    vec3 rayEnd = ray.origin + ray.direction * tMax;

    vec3 entryPos = (rayStart - minBound) / BRICK_SIZE;

    ivec3 brickIndex = clamp(ivec3(entryPos), ivec3(0), ivec3(SUPER_BRICK_SIZE));
    ivec3 stepDirection = dir_sign(ray.direction);
    vec3 stepSize = invDir * stepDirection;
    vec3 nextDist = (brickIndex - entryPos + max(stepDirection, 0)) * invDir;

    ivec3 normal = calculateNormalFromBounds(ray, tMin, minBound, maxBound);

    for (int iterations = 0; iterations < p_MaxIterations; iterations++)
    {
        bvec3 lower = lessThan(brickIndex, vec3(0));
        bvec3 higher = greaterThanEqual(brickIndex, vec3(SUPER_BRICK_SIZE));
        if (lower.x || lower.y || lower.z || higher.x || higher.y || higher.z)
            break;

        hit.comparisons++;

        uint32_t index = brickIndex.y * SUPER_BRICK_SIZE * SUPER_BRICK_SIZE
                + brickIndex.z * SUPER_BRICK_SIZE
                + brickIndex.x;

        if (index >= SUPER_BRICK_SIZE * SUPER_BRICK_SIZE * SUPER_BRICK_SIZE) {
            break;
        }

        uint32_t data = p_SuperBrick.superBrick.data[index];

        uint32_t is_loaded = bitfieldExtract(data, SUPER_BRICK_IS_LOADED_OFFSET, SUPER_BRICK_IS_LOADED_SIZE);
        uint32_t is_empty = bitfieldExtract(data, SUPER_BRICK_IS_EMPTY_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);

        uint32_t brickPointer = bitfieldExtract(data, SUPER_BRICK_POINTER_OFFSET, SUPER_BRICK_POINTER_SIZE);

        if (is_loaded == 0) {
            Brick brick = p_SuperBrick.superBrick.bricksBuffer.bricks[brickPointer];

            hit.colour = vec4(brick.lodR / 255., brick.lodG / 255., brick.lodB / 255., 1.);
            hit.hasHitBrick = true;

            if (p_ToBeLoaded.currentPointer >= p_ToBeLoaded.maxSize) {
                return hit;
            }

            uint32_t new_data = bitfieldInsert(data, 1, SUPER_BRICK_REQUESTED_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);
            uint32_t previous = atomicExchange(p_SuperBrick.superBrick.data[index], new_data);

            uint32_t previously_requested = bitfieldExtract(previous, SUPER_BRICK_REQUESTED_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);
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
            if (is_empty == 0) // Not Empty
            {
                vec3 brickMinBound = brickIndex * BRICK_SIZE;

                hit.normal = normal;
                traverseBrick(ray, brickPointer, brickMinBound, iterations, hit);

                if (hit.hasHitBrick) {
                    hit.brickHitIndex = brickIndex;
                    return hit;
                }
            }
        }

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        brickIndex += stepDirection * stepAxis;
        nextDist += stepSize * stepAxis;

        normal = -(stepDirection * stepAxis);
    }

    hit.hasHitBrick = false;
    return hit;
}

vec3 calculateHitPosition(in HitRecord hit) {
    return hit.brickHitIndex * BRICK_SIZE + hit.voxelHitIndex;
}

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
    ivec2 size = imageSize(o_Image);
    vec2 uv = vec2(texelCoord) / vec2(size);

    bool middle = false;
    if (texelCoord.x == size.x / 2 && texelCoord.y == size.y / 2) {
        middle = true;
    }

    const vec3 clearColour = vec3(0.1);
    imageStore(o_ComparisonImage, texelCoord, vec4(clearColour, 0.0));
    imageStore(o_Image, texelCoord, vec4(clearColour, 0.0));

    Ray ray = generateRay(uv,
            p_CameraPosition, vec3(p_CameraFront),
            vec3(p_CameraRight),
            vec3(p_CameraUp), p_AspectRatio);

    ivec3 brickIndex;
    HitRecord hit = traverseSuperBrick(ray);

    vec4 colour = vec4(clearColour, 0.);
    if (hit.hasHitBrick) {
        vec3 hitPosition = calculateHitPosition(hit);
        vec4 lookupColour = hit.colour;

        colour = lookupColour;
        if (hit.hasHitVoxel) {
            const vec4 lightColour = vec4(1.);

            float diff = max(dot(hit.normal, p_SunDir.xyz), 0.);
            vec4 diffuse = lightColour * diff;

            Ray shadowRay;
            shadowRay.origin = hitPosition - (ray.direction * 0.001);
            shadowRay.direction = p_SunDir.xyz;
            shadowRay.invDir = 1. / shadowRay.direction;

            HitRecord shadow = traverseSuperBrick(shadowRay);

            const float ambientStrength = 0.7;
            vec4 ambient = lightColour * ambientStrength;

            float diffStrength = 1.;
            if (shadow.hasHitBrick || shadow.hasHitVoxel)
                diffStrength = 0.;

            colour = (ambient + diffuse * diffStrength) * colour;

            // if (shadow.hasHitBrick || shadow.hasHitVoxel)
            //     colour = vec4(1.);
            // else
            //     colour = vec4(0.);
        }
        imageStore(o_Image, texelCoord, colour);
    }

    if (hit.comparisons >= 0) {
        vec4 lowestHitColour = vec4(0.5, 0., 0.5, 1.0);
        vec4 highestHitColour = vec4(1., 1., 0., 1.0);
        float mixAmount = hit.comparisons / float(p_MaxHeatShown);

        imageStore(o_ComparisonImage, texelCoord, mix(lowestHitColour, highestHitColour, mixAmount));
    }

    if (shouldColourCursor(uv, vec2(1.) / vec2(size))) {
        imageStore(o_Image, texelCoord, mix(colour, cursorColour, 0.7));
    }

    if (middle) {
        p_Feedback.hasHitBrick = int(hit.hasHitBrick);
        p_Feedback.hasHitVoxel = int(hit.hasHitVoxel);
        p_Feedback.superBrickIndex = ivec3(0);
        p_Feedback.brickIndex = hit.brickHitIndex;
        p_Feedback.voxelIndex = hit.voxelHitIndex;
        p_Feedback.voxelNormal = hit.normal;
    }
}
