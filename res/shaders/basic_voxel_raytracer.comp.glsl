#version 460

#extension GL_EXT_buffer_reference : enable

layout (local_size_x = 16, local_size_y = 16) in;

layout (rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout (rgba16f, set = 0, binding = 1) uniform image2D o_RayImage;

#define MAX_COMPARISONS 128

ivec2 texelCoord;

struct Voxel
{
    vec4 colour;
};

struct Ray
{
    vec3 origin;
    vec3 direction;
    vec3 invDir;
};

layout (buffer_reference, std430) readonly buffer VoxelBuffer
{
    Voxel voxels[];
};

layout (push_constant) uniform constants
{
    vec4 p_CameraPosition;
    vec4 p_CameraForward;
    vec4 p_CameraRight;
    vec4 p_CameraUp;
    uvec3 p_Dimensions;
    float p_Size;
    VoxelBuffer p_Voxels;
};

const vec3 voxelOrigin = vec3(0., 0., 0.);

void getVoxelBounds(out vec3 minBound, out vec3 maxBound)
{
    minBound = voxelOrigin;
    maxBound = voxelOrigin + p_Dimensions * p_Size;
}

bool hitBox(Ray ray, vec3 minBound, vec3 maxBound, float t0, float t1, out float tMin, out float tMax)
{
    if (ray.invDir.x >= 0.)
    {
        tMin = (minBound.x - ray.origin.x) * ray.invDir.x;
        tMax = (maxBound.x - ray.origin.x) * ray.invDir.x;
    }
    else
    {
        tMax = (minBound.x - ray.origin.x) * ray.invDir.x;
        tMin = (maxBound.x - ray.origin.x) * ray.invDir.x;
    }

    float tYMin, tYMax;
    if (ray.invDir.y >= 0.)
    {
        tYMin = (minBound.y - ray.origin.y) * ray.invDir.y;
        tYMax = (maxBound.y - ray.origin.y) * ray.invDir.y;
    }
    else
    {
        tYMax = (minBound.y - ray.origin.y) * ray.invDir.y;
        tYMin = (maxBound.y - ray.origin.y) * ray.invDir.y;
    }

    if (tMin > tYMax || tYMin > tMax) return false;
    if (tYMin > tMin) tMin = tYMin;
    if (tYMax < tMax) tMax = tYMax;

    float tZMin, tZMax;
    if (ray.invDir.z >= 0.)
    {
        tZMin = (minBound.z - ray.origin.z) * ray.invDir.z;
        tZMax = (maxBound.z - ray.origin.z) * ray.invDir.z;
    }
    else
    {
        tZMax = (minBound.z - ray.origin.z) * ray.invDir.z;
        tZMin = (maxBound.z - ray.origin.z) * ray.invDir.z;
    }

    if (tMin > tZMax || tZMin > tMax) return false;
    if (tZMin > tMin) tMin = tZMin;
    if (tZMax < tMax) tMax = tZMax;

    return ((tMin < t1) && (tMax > t0));
}

int indexFromPosition(uvec3 pos)
{
    return int(pos.x + pos.z * p_Dimensions.x + pos.y * p_Dimensions.x * p_Dimensions.z);
}

Ray generateRay()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Image);

    vec2 uv = vec2(texelCoord) / vec2(size - 1);

    const float viewportWidth = 2.0;
    const float viewportHeight = 2.0;
    const float viewportDepth = 1.0;

    vec3 viewportTopLeft = vec3(p_CameraPosition + p_CameraForward * viewportDepth - (p_CameraRight * viewportWidth / 2.) + (p_CameraUp * viewportHeight / 2.));
    vec3 deltaRight = vec3(p_CameraRight * viewportWidth);
    vec3 deltaDown = vec3(-p_CameraUp * viewportHeight);

    vec3 origin = vec3(p_CameraPosition);
    vec3 target = viewportTopLeft + uv.x * deltaRight + uv.y * deltaDown;
    vec3 direction = normalize(target - origin);

    Ray ray;
    ray.origin = origin;
    ray.direction = direction;
    ray.invDir = 1. / ray.direction;

    return ray;
}

bool withinBounds(uvec3 index)
{
    bvec3 less = lessThanEqual(index, p_Dimensions - 1);
    bvec3 greater = greaterThanEqual(index, uvec3(0));
    return less.x && less.y && less.z && greater.x && greater.y && greater.z;
}

bool traverse(Ray ray, float t0, float t1, out Voxel voxel, out int comparisons)
{
    comparisons = -1;

    vec3 minBound, maxBound;
    getVoxelBounds(minBound, maxBound);
    float tMin, tMax;
    bool intersectGrid = hitBox(ray, minBound, maxBound, t0, t1, tMin, tMax);
    if (!intersectGrid) return false;

    comparisons = 1;

    tMin = max(tMin, t0);
    tMax = min(tMax, t1);

    vec3 rayStart = ray.origin + ray.direction * tMin;
    vec3 rayEnd = ray.origin + ray.direction * tMax;

    ivec3 currentIndex = ivec3(max(vec3(0.), floor(rayStart - minBound / p_Size)));
    ivec3 endIndex = ivec3(max(vec3(0.), floor(rayEnd - minBound / p_Size)));

    currentIndex = clamp(currentIndex, ivec3(0), ivec3(p_Dimensions - 1));
    endIndex = clamp(endIndex, ivec3(0), ivec3(p_Dimensions - 1));

    ivec3 stepDirection = clamp(ivec3(sign(ray.direction)), ivec3(-1), ivec3(1));
    vec3 stepSize = vec3(p_Size * ray.invDir * stepDirection);
    // vec3 nextDist = (vec3(stepDirection) * 0.5 + 0.5 - fract(rayStart)) * ray.invDir;
    vec3 nextDist = abs((currentIndex + max(stepDirection, 0) - ray.origin) * ray.invDir);

    endIndex += stepDirection;

    while (currentIndex.x != endIndex.x && currentIndex.y != endIndex.y && currentIndex.z != endIndex.z)
    {
        comparisons += 1;

        if (!withinBounds(currentIndex)) return false;
        int index = indexFromPosition(currentIndex);

        voxel = p_Voxels.voxels[index];
        if (voxel.colour.w > 0) return true;

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));
        currentIndex += stepDirection * stepAxis;
        nextDist += stepSize * stepAxis;
    }

    comparisons += 1;

    if (!withinBounds(currentIndex)) return false;

    int index = indexFromPosition(currentIndex);

    voxel = p_Voxels.voxels[index];
    if (voxel.colour.w > 0) return true;

    return false;
}

void main()
{
    // ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Image);

    Ray ray = generateRay();

    imageStore(o_RayImage, texelCoord, vec4(0.2));

    Voxel hitVoxel;
    int comparisons;
    bool hasHit = traverse(ray, 0., 1000., hitVoxel, comparisons);

    imageStore(o_Image, texelCoord, vec4(0.2));

    const vec4 noComp = vec4(1., 0., 1., 0.2);
    const vec4 maxComp = vec4(1., 1., 0., 0.2);

    if (comparisons >= 0)
    {
        vec4 colour = mix(noComp, maxComp, float(comparisons) / MAX_COMPARISONS);
        imageStore(o_RayImage, texelCoord, colour);
    }

    if (hasHit)
    {
        imageStore(o_Image, texelCoord, vec4(hitVoxel.colour.xyz, 1.));
    }
}
