#define MAX_ITERATIONS 256

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

bool indexWithinBounds(Grid grid, ivec3 position)
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

bool rayBoxIntersect(Ray ray, vec3 minBound, vec3 maxBound, float minT, float maxT,
                     out float tMin, out float tMax)
{
    vec3 tbot = ray.invDir * (minBound - ray.origin);
    vec3 ttop = ray.invDir * (maxBound - ray.origin);

    vec3 tmin = min(ttop, tbot);
    vec3 tmax = max(ttop, tbot);

    vec2 t = max(tmin.xx, tmin.yz);
    float t0 = max(t.x, t.y);
    t = min(tmax.xx, tmax.yz);
    float t1 = min(t.x, t.y);
    tMin = t0;
    tMax = t1;
    return t1 > max(t0, 0.0) && tMax > minT && tMin < maxT;
}

vec3 normalFromBounds(vec3 position, vec3 minBound, vec3 maxBound)
{
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

bool traverse(Ray ray, Grid grid,
            out ivec3 gridIndex, out Voxel voxel, out vec3 normal, out int comparisons,
            out float t)
{
    comparisons = -1;

    float tMin, tMax;
    const vec3 minBound = grid.minBound;
    const vec3 maxBound = grid.maxBound;
    bool intersectGrid = rayBoxIntersect(ray, minBound, maxBound, 0.0, 1000.0, tMin, tMax);

    if (!intersectGrid) return false;

    comparisons = 0;

    vec3 invDir = ray.invDir;
    if (isinf(invDir.x)) invDir.x = 0.;
    if (isinf(invDir.y)) invDir.y = 0.;
    if (isinf(invDir.z)) invDir.z = 0.;

    t = max(tMin, 0.);

    vec3 rayStart = ray.origin + ray.direction * max(tMin, 0);
    vec3 rayEnd = ray.origin + ray.direction * tMax;

    gridIndex = ivec3(max(vec3(0.), floor(rayStart - minBound / grid.voxelSize)));
    gridIndex = clamp(gridIndex, ivec3(0), ivec3(grid.dimensions - 1));

    ivec3 stepDirection = clamp(ivec3(sign(ray.direction)), ivec3(-1), ivec3(1));
    vec3 stepSize = vec3(grid.voxelSize * invDir * stepDirection);
    vec3 nextDist = abs((gridIndex + max(stepDirection, 0) - ray.origin) * ray.invDir);

    ivec3 endIndex = ivec3(max(vec3(0.), floor(rayEnd - minBound / grid.voxelSize)));
    endIndex = clamp(endIndex, ivec3(0), ivec3(grid.dimensions - 1));
    endIndex += stepDirection;

    normal = normalFromBounds(rayStart, minBound, maxBound);

    for (int i = 0; i < MAX_ITERATIONS; i++)
    {
        comparisons++;

        if (!indexWithinBounds(grid, gridIndex)) return false;

        int index = indexFromGridPosition(grid, gridIndex);

        voxel = grid.voxels.voxels[index];
        if (voxel.lookupIndex >= 0) return true;

        float closestDist = min(min(nextDist.x, nextDist.y), nextDist.z);
        ivec3 stepAxis = ivec3(lessThanEqual(nextDist, vec3(closestDist)));

        t += dot(stepAxis, vec3(closestDist));
        gridIndex += stepDirection * stepAxis;
        nextDist += stepSize * stepAxis;
        normal = -stepDirection * stepAxis;
    }

    return false;
}
