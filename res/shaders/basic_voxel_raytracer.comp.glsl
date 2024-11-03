#version 460

#extension GL_EXT_buffer_reference : enable

layout (local_size_x = 16, local_size_y = 16) in;

layout (rgba16f, set = 0, binding = 0) uniform image2D o_Image;
layout (rgba16f, set = 0, binding = 1) uniform image2D o_RayImage;

#define MAX_COMPARISONS 128

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

    return (tMin < t1 && tMax > t0);
}

int indexFromPosition(ivec3 pos)
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

    vec3 viewportTopLeft = vec3(p_CameraPosition + viewportDepth * p_CameraForward - (p_CameraRight * viewportWidth / 2.) + (p_CameraUp * viewportHeight / 2.));
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

bool traverse(Ray ray, float t0, float t1, out Voxel voxel, out int comparisons)
{
    comparisons = 0;

    vec3 minBound, maxBound;
    getVoxelBounds(minBound, maxBound);
    float tMin, tMax;
    bool intersectGrid = hitBox(ray, minBound, maxBound, t0, t1, tMin, tMax);
    if (!intersectGrid) return false;

    comparisons = 1;

    tMin = max(tMin, t0);
    tMax = max(tMax, t1);

    vec3 rayStart = ray.origin + ray.direction * tMin;
    vec3 rayEnd = ray.origin + ray.direction * tMax;

    int currentXIndex = int(max(0, floor(rayStart.x - minBound.x / p_Size)));
    int endXIndex = int(max(0, floor(rayStart.x - minBound.x / p_Size)));
    int stepX;
    float tDeltaX;
    float tMaxX;
    if (ray.direction.x > 0.)
    {
        stepX = 1;
        tDeltaX = p_Size * ray.invDir.x;
        tMaxX = tMin + (minBound.x + currentXIndex * p_Size - rayStart.x) * ray.invDir.x;
    }
    else if (ray.direction.x < 0.)
    {
        stepX = -1;
        tDeltaX = -p_Size * ray.invDir.x;
        int previousXIndex = currentXIndex - 1;
        tMaxX = tMin + (minBound.x + previousXIndex * p_Size - rayStart.x) * ray.invDir.x;
    }
    else
    {
        stepX = 0;
        tDeltaX = tMax;
        tMaxX = tMax;
    }

    int currentYIndex = int(max(0, floor(rayStart.y - minBound.y / p_Size)));
    int endYIndex = int(max(0, floor(rayStart.y - minBound.y / p_Size)));
    int stepY;
    float tDeltaY;
    float tMaxY;
    if (ray.direction.y > 0.)
    {
        stepY = 1;
        tDeltaY = p_Size * ray.invDir.y;
        tMaxY = tMin + (minBound.y + currentYIndex * p_Size - rayStart.y) * ray.invDir.y;
    }
    else if (ray.direction.y < 0.)
    {
        stepY = -1;
        tDeltaY = -p_Size * ray.invDir.y;
        int previousYIndex = currentYIndex - 1;
        tMaxY = tMin + (minBound.y + previousYIndex * p_Size - rayStart.y) * ray.invDir.y;
    }
    else
    {
        stepY = 0;
        tDeltaY = tMax;
        tMaxY = tMax;
    }

    int currentZIndex = int(max(0, floor(rayStart.z - minBound.z / p_Size)));
    int endZIndex = int(max(0, floor(rayStart.z - minBound.z / p_Size)));
    int stepZ;
    float tDeltaZ;
    float tMaxZ;
    if (ray.direction.z > 0.)
    {
        stepZ = 1;
        tDeltaZ = p_Size * ray.invDir.z;
        tMaxZ = tMin + (minBound.z + currentYIndex * p_Size - rayStart.z) * ray.invDir.z;
    }
    else if (ray.direction.z < 0.)
    {
        stepZ = -1;
        tDeltaZ = -p_Size * ray.invDir.z;
        int previousZIndex = currentZIndex - 1;
        tMaxZ = tMin + (minBound.z + previousZIndex * p_Size - rayStart.z) * ray.invDir.z;
    }
    else
    {
        stepZ = 0;
        tDeltaZ = tMax;
        tMaxZ = tMax;
    }

    while (currentXIndex != endXIndex || currentYIndex != endYIndex || currentZIndex != endZIndex)
    {
        comparisons += 1;
        if (tMaxX < tMaxY && tMaxX < tMaxZ)
        {
            currentXIndex += stepX;
            tMaxX += tDeltaX;
        }
        else if (tMaxY < tMaxZ)
        {
            currentYIndex += stepY;
            tMaxY += tDeltaY;
        }
        else
        {
            currentZIndex += stepZ;
            tMaxZ += tDeltaZ;
        }
    }

    int index = indexFromPosition(ivec3(currentXIndex, currentYIndex, currentZIndex));

    voxel = p_Voxels.voxels[index];
    return true;
}

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Image);

    Ray ray = generateRay();


    Voxel hitVoxel;
    int comparisons;
    bool hasHit = traverse(ray, 0., 1000., hitVoxel, comparisons);

    const vec4 noComp = vec4(1., 0., 1., 1.);
    const vec4 maxComp = vec4(1., 1., 0., 1.);
    vec4 colour = mix(noComp, maxComp, float(comparisons) / MAX_COMPARISONS);


    if (hasHit)
    {
        imageStore(o_RayImage, texelCoord, colour);
        imageStore(o_Image, texelCoord, hitVoxel.colour);
    }
    else
    {
        imageStore(o_RayImage, texelCoord, vec4(0.));
        imageStore(o_Image, texelCoord, vec4(0.));
    }
}
