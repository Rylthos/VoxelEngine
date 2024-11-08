struct Ray
{
    vec3 origin;
    vec3 direction;
    vec3 invDir;
};

struct Grid
{
    vec3 minBound;
    vec3 maxBound;
    uvec3 dimensions;
    float voxelSize;
    VoxelBuffer voxels;
};

Grid generateGrid(vec3 origin, uvec3 dimensions, float voxelSize, VoxelBuffer voxels)
{
    Grid grid;
    grid.minBound = origin;
    grid.maxBound = origin + dimensions * voxelSize;
    grid.dimensions = dimensions;
    grid.voxelSize = voxelSize;
    grid.voxels = voxels;
    return grid;
}

int indexFromGridPosition(Grid grid, uvec3 position)
{
    return int(position.x
               + position.z * grid.dimensions.x
               + position.y * grid.dimensions.x * grid.dimensions.z);
}

bool indexWithinBounds(Grid grid, uvec3 position)
{
    bvec3 less = lessThanEqual(position, grid.dimensions - 1);
    bvec3 greater = greaterThanEqual(position, uvec3(0));
    return less.x && less.y && less.z && greater.x && greater.y && greater.z;
}

Ray generateRay(vec2 uv, vec3 position, vec3 front, vec3 right, vec3 up)
{
    const float viewportWidth = 2.0;
    const float viewportHeight = 2.0;
    const float viewportDepth = 1.0;

    const float viewportHalfWidth = viewportWidth / 2.0;
    const float viewportHalfHeight = viewportHeight / 2.0;

    const vec3 viewportTopLeft = position
                            + front * viewportDepth
                            - right * viewportHalfWidth
                            + up * viewportHalfHeight;

    const vec3 deltaRight = right * viewportWidth;
    const vec3 deltaDown = -up * viewportHeight;

    vec3 target = viewportTopLeft + uv.x * deltaRight + uv.y * deltaDown;
    vec3 direction = normalize(target - position);

    Ray ray;
    ray.origin = position;
    ray.direction = direction;
    ray.invDir = 1. / direction;

    return ray;
}

bool rayBoxIntersect(Ray ray, vec3 minBound, vec3 maxBound, float t0, float t1,
                     out float tMin, out float tMax)
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

bool traverse(Ray ray, Grid grid, float t0, float t1,
            out ivec3 gridIndex, out Voxel voxel, out vec3 normal, out int comparisons,
            out float t)
{
    comparisons = -1;

    float tMin, tMax;
    const vec3 minBound = grid.minBound;
    const vec3 maxBound = grid.maxBound;
    bool intersectGrid = rayBoxIntersect(ray, minBound, maxBound, t0, t1, tMin, tMax);
    if (!intersectGrid) return false;

    comparisons = 1;

    tMin = max(tMin, t0);
    tMax = min(tMax, t1);

    t = tMin;

    vec3 rayStart = ray.origin + ray.direction * tMin;
    vec3 rayEnd = ray.origin + ray.direction * tMax;

    gridIndex = ivec3(max(vec3(0.), floor(rayStart - minBound / grid.voxelSize)));
    ivec3 endIndex = ivec3(max(vec3(0.), floor(rayEnd - minBound / grid.voxelSize)));

    gridIndex = clamp(gridIndex, ivec3(0), ivec3(grid.dimensions - 1));
    endIndex = clamp(endIndex, ivec3(0), ivec3(grid.dimensions - 1));

    ivec3 stepDirection = clamp(ivec3(sign(ray.direction)), ivec3(-1), ivec3(1));
    vec3 stepSize = vec3(grid.voxelSize * ray.invDir * stepDirection);
    vec3 nextDist = abs((gridIndex + max(stepDirection, 0) - ray.origin) * ray.invDir);

    endIndex += stepDirection;

    normal = vec3(0.);

    while (gridIndex.x != endIndex.x && gridIndex.y != endIndex.y && gridIndex.z != endIndex.z)
    {
        comparisons += 1;

        if (!indexWithinBounds(grid, gridIndex)) return false;
        int index = indexFromGridPosition(grid, gridIndex);

        voxel = grid.voxels.voxels[index];
        if (voxel.colour.w > 0) return true;

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        t += dot(stepSize, stepAxis);
        gridIndex += stepDirection * stepAxis;
        nextDist += stepSize * stepAxis;
        normal = normalize(stepDirection * stepAxis);
    }

    comparisons += 1;

    if (!indexWithinBounds(grid, gridIndex)) return false;

    int index = indexFromGridPosition(grid, gridIndex);

    voxel = grid.voxels.voxels[index];

    return (voxel.colour.w > 0) ? true : false;
}
