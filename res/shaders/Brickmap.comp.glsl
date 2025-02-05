#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_debug_printf : enable

#include "Ray.other.glsl"
#include "BrickmapData.other.glsl"

#include "GBufferLayout.other.glsl"

#define PER_PIXEL

layout(local_size_x = 8, local_size_y = 8) in;

layout(rgba16f, set = 1, binding = 0) uniform image2D o_HeatImage;

layout(buffer_reference, std430) buffer FeedbackBuffer {
    ivec3 chunkIndex;
    bool hasHitChunk;
    ivec3 superBrickIndex;
    bool hasHitSuperBrick;
    ivec3 brickIndex;
    bool hasHitBrick;
    ivec3 voxelIndex;
    bool hasHitVoxel;
    ivec3 voxelNormal;
};

layout(push_constant) uniform constants {
    vec3 p_CameraPosition;
    float p_AspectRatio;

    vec3 p_CameraFront;
    int p_ShouldLoadVoxels;

    vec4 p_CameraRight;
    vec4 p_CameraUp;

    vec4 p_SunDir;

    float p_SuperBrickLODDistance;
    float p_BrickLODDistance;
    uint32_t p_MaxHeatShown;
    uint32_t p_MaxIterations;

    ToBeLoadedBuffer p_ToBeLoaded;
    ChunkBuffer p_Chunk;
    FeedbackBuffer p_Feedback;
};

struct HitRecord {
    bool hasHitVoxel;
    bool hasHitBrick;
    bool hasHitSuperBrick;
    bool hasHitChunk;
    ivec3 voxelHitIndex;
    ivec3 brickHitIndex;
    ivec3 superBrickHitIndex;
    ivec3 chunkHitIndex;
    ivec3 normal;
    vec4 colour;
    vec3 position;
    int comparisons;
};

struct PreviousHit {
    bool checkHit;
    bool correctChunk;
    bool correctSuperBrick;
    bool correctBrick;
    ivec3 chunkIndex;
    ivec3 superBrickIndex;
    ivec3 brickIndex;
    ivec3 voxelIndex;
};

HitRecord emptyHit()
{
    HitRecord hit;

    hit.hasHitVoxel = false;
    hit.hasHitBrick = false;
    hit.hasHitSuperBrick = false;
    hit.hasHitChunk = false;

    hit.comparisons = -1;
    hit.colour = vec4(1., 0., 1., 1.);

    hit.voxelHitIndex = ivec3(0);
    hit.brickHitIndex = ivec3(0);
    hit.superBrickHitIndex = ivec3(0);
    hit.chunkHitIndex = ivec3(0);

    hit.normal = ivec3(0);

    return hit;
}

PreviousHit emptyPrevious() {
    PreviousHit prev;
    prev.checkHit = false;
    prev.correctChunk = true;
    prev.chunkIndex = ivec3(0);

    return prev;
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

vec4 calculateColour(in uint32_t superBrickIndex, in Brick brick, in ivec3 brickIndex) {
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

    return p_Chunk.chunks.superBricks.superBrick[superBrickIndex].colourBuffers.colours[brick.colourPointer + index];
}

vec3 removeInf(vec3 a)
{
    return vec3(
        (isinf(a.x) || isnan(a.x)) ? 0 : a.x,
        (isinf(a.y) || isnan(a.y)) ? 0 : a.y,
        (isinf(a.z) || isnan(a.z)) ? 0 : a.z
    );
}

void traverseBrick(in Ray ray, in uint32_t superBrickIndex, in uint32_t brickPointer, in vec3 minBound, inout int iterations, inout HitRecord hit, inout PreviousHit prev)
{
    const vec3 maxBound = minBound + vec3(BRICK_SIZE * VOXEL_SIZE);

    float tMin, tMax;
    bool intersectBound = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000000.0, tMin, tMax);

    if (!intersectBound) return;

    hit.hasHitBrick = true;

    vec3 invDir = ray.invDir;

    vec3 rayStart = ray.origin + ray.direction * max(tMin + 0.001, 0.);

    vec3 entryPos = (rayStart - minBound) / VOXEL_SIZE;

    ivec3 voxelIndex = clamp(ivec3(entryPos), ivec3(0), ivec3(BRICK_SIZE));
    const ivec3 stepDirection = dir_sign(ray.direction);
    const vec3 stepSize = invDir * stepDirection;
    vec3 nextDist = (voxelIndex - entryPos + max(stepDirection, 0)) * invDir;

    bool hasStepped = false;
    ivec3 normal = hit.normal;
    float traversal = 0.;

    const Brick brick = p_Chunk.chunks.superBricks.superBrick[superBrickIndex].bricksBuffer.bricks[brickPointer];

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
            if (prev.checkHit && prev.correctBrick && prev.voxelIndex == voxelIndex) {
                continue;
            }

            hit.colour = calculateColour(superBrickIndex, brick, voxelIndex);
            hit.hasHitVoxel = true;
            hit.voxelHitIndex = voxelIndex;
            hit.normal = normal;

            hit.position = rayStart + removeInf(ray.direction * traversal * VOXEL_SIZE);
            return;
        }

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        traversal = dot(removeInf(nextDist * stepAxis), vec3(1.));
        nextDist += stepSize * stepAxis;
        voxelIndex += stepDirection * stepAxis;
        normal = ivec3(-(stepDirection * stepAxis));
        hasStepped = true;
    }

    hit.hasHitBrick = false;
    return;
}

void traverseSuperBrick(in Ray ray, in ivec3 superBrickPosition, in uint32_t superBrickIndex, in vec3 minBound, inout int iterations, inout HitRecord hit, inout PreviousHit prev)
{
    const vec3 maxBound = minBound + vec3(SUPER_BRICK_SIZE) * BRICK_SIZE * VOXEL_SIZE;

    float tMin, tMax;
    bool intersectBound = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000000.0, tMin, tMax);

    if (!intersectBound) return;

    hit.hasHitSuperBrick = true;

    const vec3 invDir = ray.invDir;

    const vec3 rayStart = ray.origin + ray.direction * max(tMin + 0.0001, 0);

    vec3 entryPos = (rayStart - minBound) / (BRICK_SIZE * VOXEL_SIZE);

    ivec3 brickIndex = clamp(ivec3(entryPos), ivec3(0), ivec3(SUPER_BRICK_SIZE));
    const ivec3 stepDirection = dir_sign(ray.direction);
    const vec3 stepSize = invDir * stepDirection;
    vec3 nextDist = (brickIndex - entryPos + max(stepDirection, 0)) * invDir;

    ivec3 normal = hit.normal;

    for (; iterations < p_MaxIterations; iterations++)
    {
        bvec3 lower = lessThan(brickIndex, ivec3(0));
        bvec3 higher = greaterThanEqual(brickIndex, ivec3(SUPER_BRICK_SIZE));
        if (lower.x || lower.y || lower.z || higher.x || higher.y || higher.z)
        {
            break;
        }

        hit.comparisons++;

        uint32_t index = brickIndex.y * SUPER_BRICK_SIZE * SUPER_BRICK_SIZE
                + brickIndex.z * SUPER_BRICK_SIZE
                + brickIndex.x;

        uint32_t data = p_Chunk.chunks.superBricks.superBrick[superBrickIndex].data[index];

        uint32_t is_loaded = bitfieldExtract(data, SUPER_BRICK_IS_LOADED_OFFSET, SUPER_BRICK_IS_LOADED_SIZE);
        uint32_t requested = bitfieldExtract(data, SUPER_BRICK_REQUESTED_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);

        uint32_t is_empty = bitfieldExtract(data, SUPER_BRICK_IS_EMPTY_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);

        uint32_t brickPointer = bitfieldExtract(data, SUPER_BRICK_POINTER_OFFSET, SUPER_BRICK_POINTER_SIZE);

        if (is_loaded == 0) {
            if (p_ShouldLoadVoxels == 0 || requested == 1 || p_ToBeLoaded.currentPointer >= p_ToBeLoaded.maxSize) {
                return;
            }

            uint32_t mask = bitfieldInsert(0, 1, SUPER_BRICK_REQUESTED_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);
            uint32_t previous = atomicOr(p_Chunk.chunks.superBricks.superBrick[superBrickIndex].data[index], mask);

            uint32_t previously_requested = bitfieldExtract(previous, SUPER_BRICK_REQUESTED_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);

            if (previously_requested == 0) {
                uint32_t writePointer = atomicAdd(p_ToBeLoaded.currentPointer, 1);
                if (writePointer < p_ToBeLoaded.maxSize) {
                    p_ToBeLoaded.toBeLoaded[writePointer].brickIndex = ivec4(brickIndex, 1);
                    p_ToBeLoaded.toBeLoaded[writePointer].superBrickIndex = ivec4(superBrickPosition, 0);
                } else {
                    atomicExchange(p_Chunk.chunks.superBricks.superBrick[superBrickIndex].data[index], previous);
                }
            }

            return;
        } else { // Brick is already loaded
            if (is_empty == 0) // Not Empty
            {
                const vec3 brickMinBound = minBound + brickIndex * BRICK_SIZE * VOXEL_SIZE;

                hit.normal = normal;
                hit.brickHitIndex = brickIndex;
                const vec3 center = brickMinBound + vec3(BRICK_SIZE * VOXEL_SIZE) / 2.;
                if (length(center - p_CameraPosition) > p_BrickLODDistance)
                {
                    const Brick brick = p_Chunk.chunks.superBricks.superBrick[superBrickIndex].bricksBuffer.bricks[brickPointer];
                    hit.hasHitBrick = true;
                    hit.colour = vec4(brick.lodR / 255., brick.lodG / 255., brick.lodB / 255., 1.);

                    return;
                }

                if (prev.checkHit && prev.correctSuperBrick) {
                    prev.correctBrick = (prev.brickIndex == brickIndex);
                }

                traverseBrick(ray, superBrickIndex, brickPointer, brickMinBound, iterations, hit, prev);

                if (hit.hasHitBrick) {
                    return;
                }
            }
        }

        const float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        const ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        brickIndex += stepDirection * stepAxis;
        nextDist += stepSize * stepAxis;
        normal = -(stepDirection * stepAxis);
    }

    hit.hasHitSuperBrick = false;
}

HitRecord traverseChunk(in Ray ray, inout PreviousHit prev)
{
    HitRecord hit = emptyHit();

    float tMin, tMax;
    const vec3 minBound = vec3(0);
    const vec3 maxBound = minBound + vec3(CHUNK_SIZE) * SUPER_BRICK_SIZE * BRICK_SIZE * VOXEL_SIZE;
    bool intersectBound = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000000.0, tMin, tMax);

    hit.position = vec3(-1.);
    if (!intersectBound) return hit;
    hit.hasHitChunk = true;

    hit.comparisons = 0;

    const vec3 invDir = ray.invDir;

    const vec3 rayStart = ray.origin + ray.direction * max(tMin + 0.0001, 0);
    const vec3 entryPos = (rayStart - minBound) / (SUPER_BRICK_SIZE * BRICK_SIZE * VOXEL_SIZE);

    ivec3 brickIndex = clamp(ivec3(entryPos), ivec3(0), ivec3(CHUNK_SIZE));
    const ivec3 stepDirection = dir_sign(ray.direction);
    vec3 stepSize = invDir * stepDirection;
    vec3 nextDist = (brickIndex - entryPos + max(stepDirection, 0)) * invDir;

    ivec3 normal = calculateNormalFromBounds(ray, tMin, minBound, maxBound);

    for (int iterations = 0; iterations < p_MaxIterations; iterations++)
    {
        bvec3 lower = lessThan(brickIndex, vec3(0));
        bvec3 higher = greaterThanEqual(brickIndex, vec3(CHUNK_SIZE));
        if (lower.x || lower.y || lower.z || higher.x || higher.y || higher.z)
            break;

        hit.comparisons++;

        uint32_t index = brickIndex.y * CHUNK_SIZE * CHUNK_SIZE
                + brickIndex.z * CHUNK_SIZE
                + brickIndex.x;

        if (index >= CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE) {
            break;
        }

        uint32_t data = p_Chunk.chunks.data[index];

        uint32_t is_loaded = bitfieldExtract(data, SUPER_BRICK_IS_LOADED_OFFSET, SUPER_BRICK_IS_LOADED_SIZE);
        uint32_t requested = bitfieldExtract(data, SUPER_BRICK_REQUESTED_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);
        uint32_t is_empty = bitfieldExtract(data, SUPER_BRICK_IS_EMPTY_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);

        uint32_t pointer = bitfieldExtract(data, SUPER_BRICK_POINTER_OFFSET, SUPER_BRICK_POINTER_SIZE);

        if (is_loaded == 0) {
            if (p_ShouldLoadVoxels == 0 || requested == 1 || p_ToBeLoaded.currentPointer >= p_ToBeLoaded.maxSize) {
                return hit;
            }

            uint32_t mask = bitfieldInsert(0, 1, SUPER_BRICK_REQUESTED_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);
            uint32_t previous = atomicOr(p_Chunk.chunks.data[index], mask);

            uint32_t previously_requested = bitfieldExtract(previous, SUPER_BRICK_REQUESTED_FLAG_OFFSET, SUPER_BRICK_FLAG_SIZE);
            if (previously_requested == 0) {
                uint32_t writePointer = atomicAdd(p_ToBeLoaded.currentPointer, 1);
                if (writePointer < p_ToBeLoaded.maxSize) {
                    p_ToBeLoaded.toBeLoaded[writePointer].brickIndex = ivec4(0);
                    p_ToBeLoaded.toBeLoaded[writePointer].superBrickIndex = ivec4(brickIndex, 1);
                } else {
                    atomicExchange(p_Chunk.chunks.data[index], previous);
                }
            }

            return hit;
        } else { // SuperBrick is already loaded
            if (is_empty == 0) // Not Empty
            {
                const vec3 superbrickMinbound = minBound + brickIndex * SUPER_BRICK_SIZE * BRICK_SIZE * VOXEL_SIZE;

                hit.normal = normal;
                hit.superBrickHitIndex = brickIndex;

                const vec3 center = superbrickMinbound + vec3(SUPER_BRICK_SIZE * BRICK_SIZE * VOXEL_SIZE) / 2.;
                if (length(center - p_CameraPosition) > p_SuperBrickLODDistance)
                {
                    hit.hasHitSuperBrick = true;
                    hit.colour = vec4(1.);
                    return hit;
                }

                if (prev.checkHit && prev.correctChunk) {
                    prev.correctSuperBrick = (prev.superBrickIndex == brickIndex);
                }

                traverseSuperBrick(ray, brickIndex, pointer, superbrickMinbound, iterations, hit, prev);

                if (hit.hasHitSuperBrick) {
                    return hit;
                }
            }
        }

        const float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        const ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        brickIndex += stepDirection * stepAxis;
        nextDist += stepSize * stepAxis;
        normal = -(stepDirection * stepAxis);
    }

    hit.hasHitChunk = false;
    return hit;
}

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Position);
    vec2 uv = vec2(texelCoord) / (vec2(size) - 1);

    bool middle = false;
    if (texelCoord.x == size.x / 2 && texelCoord.y == size.y / 2) {
        middle = true;
    }

    imageStore(o_HeatImage, texelCoord, vec4(0.));
    imageStore(o_Position, texelCoord, vec4(0));
    imageStore(o_Normal, texelCoord, ivec4(0));
    imageStore(o_Colour, texelCoord, vec4(0));
    imageStore(o_Occlusion, texelCoord, vec4(1));

    Ray ray = generateRay(uv,
            p_CameraPosition, vec3(p_CameraFront),
            vec3(p_CameraRight),
            vec3(p_CameraUp), p_AspectRatio);

    PreviousHit prev = emptyPrevious();
    HitRecord hit = traverseChunk(ray, prev);

    vec4 colour = vec4(0.);
    if (hit.hasHitSuperBrick || hit.hasHitBrick) {
        bool inShadow = false;

        if (hit.hasHitVoxel) {
            Ray shadowRay = createRay(hit.position, p_SunDir.xyz);

            prev.checkHit = true;
            prev.chunkIndex = hit.chunkHitIndex;
            prev.superBrickIndex = hit.superBrickHitIndex;
            prev.brickIndex = hit.brickHitIndex;
            prev.voxelIndex = hit.voxelHitIndex;

            HitRecord shadow = traverseChunk(shadowRay, prev);

            inShadow = shadow.hasHitVoxel;

            if (inShadow) {
                debugPrintfEXT("Shadow SuperBrick Hit: %v3d | Prev SuperBrick Hit: %v3d | Shadow Brick Hit: %v3d | Prev Brick Hit: %v3d | Shadow voxel hit: %v3d | Prev Voxel Hit: %v3d | Check: %b | Super brick: %b, Brick: %b",
                    shadow.superBrickHitIndex, prev.superBrickIndex, shadow.brickHitIndex, prev.brickIndex, shadow.voxelHitIndex, prev.voxelIndex, prev.checkHit, prev.correctSuperBrick, prev.correctBrick);
            }
        }

        vec3 positionOffset = hit.position - p_CameraPosition;
        imageStore(o_Position, texelCoord, vec4(positionOffset, hit.hasHitVoxel));
        imageStore(o_Normal, texelCoord, ivec4(hit.normal, inShadow));
        imageStore(o_Colour, texelCoord, vec4(hit.colour.rgb, 1.));
    }

    if (hit.comparisons >= 0) {
        vec4 lowestHitColour = vec4(0.5, 0., 0.5, 1.0);
        vec4 highestHitColour = vec4(1., 1., 0., 1.0);
        float mixAmount = hit.comparisons / float(p_MaxHeatShown);

        imageStore(o_HeatImage, texelCoord, mix(lowestHitColour, highestHitColour, mixAmount));
    }

    if (middle) {
        p_Feedback.chunkIndex = hit.chunkHitIndex;
        p_Feedback.hasHitChunk = hit.hasHitChunk;
        p_Feedback.superBrickIndex = hit.superBrickHitIndex;
        p_Feedback.hasHitSuperBrick = hit.hasHitSuperBrick;
        p_Feedback.brickIndex = hit.brickHitIndex;
        p_Feedback.hasHitBrick = hit.hasHitBrick;
        p_Feedback.voxelIndex = hit.voxelHitIndex;
        p_Feedback.hasHitVoxel = hit.hasHitVoxel;
        p_Feedback.voxelNormal = hit.normal;
    }
}
