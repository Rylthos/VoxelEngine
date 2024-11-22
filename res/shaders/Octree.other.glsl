struct SVONode {
    uint32_t childPtr;
    uint8_t unused;
    uint8_t materialIndex;
    uint8_t validMask;
    uint8_t leafMask;
};

layout(buffer_reference, std430) readonly buffer SVONodeBuffer {
    SVONode nodes[];
};

struct HitRecord {
    float t;
    vec3 position;
    vec3 normal;
    uint parent;
    int deepest;
    int heatMap;
    uint8_t materialIndex;
};

struct StackMember
{
    float tMax;
    uint parent;
    vec3 minBound;
};

vec3 normalFromBounds(vec3 position, vec3 minBound, vec3 maxBound)
{
    bvec3 minBoundHit = lessThanEqual(position - minBound, vec3(0.001));
    bvec3 maxBoundHit = lessThanEqual(position - maxBound, vec3(0.001));

    if (minBoundHit.x) return vec3(-1, 0, 0);
    if (minBoundHit.y) return vec3(0, -1, 0);
    if (minBoundHit.z) return vec3(0, 0, -1);

    if (maxBoundHit.x) return vec3(1, 0, 0);
    if (maxBoundHit.y) return vec3(0, 1, 0);
    if (maxBoundHit.z) return vec3(0, 0, 1);

    return vec3(0.);
}

HitRecord castRay(SVONodeBuffer nodes, Ray ray, uint voxelDimensions, float voxelSize,
    uint maxIterations, uint maxLOD) {
    const int sMax = 13;
    const float epsilon = exp2(-sMax);

    vec3 direction = ray.direction;
    const vec3 origin = ray.origin + direction * epsilon;

    const vec3 dimensions = vec3(voxelDimensions) * voxelSize;
    const vec3 bias = direction * 0.001;

    vec3 position = ray.origin;

    uint parent = 0;

    vec3 minBound = vec3(0.);
    vec3 maxBound = minBound + voxelDimensions;

    int currentStack = -1;
    StackMember stack[sMax + 1];

    HitRecord hit;
    hit.t = -2;
    hit.deepest = -1;
    hit.heatMap = -1;
    hit.position = vec3(100);

    vec3 invDir = 1. / direction;

    float tMin, tMax;
    if (!rayBoxIntersect(origin, invDir, minBound, maxBound, 0., MAX_T, tMin, tMax)) return hit;

    float t = tMin;

    float scale = 0.5;

    position = calculatePosition(origin, direction, tMin);
    hit.position = position;

    SVONode node = nodes.nodes[parent];

    for (int i = 0; i < maxIterations; i++)
    {
        hit.heatMap = i;
        hit.deepest = (currentStack + 1 > hit.deepest) ? currentStack + 1 : hit.deepest;

        if (t >= tMax) // Ascend, Go up stack
        {
            if (currentStack == -1) break;

            StackMember member = stack[currentStack];
            currentStack--;

            tMax = member.tMax;
            parent = member.parent;
            minBound = member.minBound;

            node = nodes.nodes[parent];

            scale *= 2;

            continue;
        }

        vec3 center = minBound + scale * dimensions;
        vec3 boundOffset = vec3(0);
        int octantMask = 0;

        if (position.x > center.x || (position.x == center.x && direction.x > 0)) {
            octantMask ^= 1;
            boundOffset.x = dimensions.x;
        }
        if (position.z > center.z || (position.z == center.z && direction.z > 0)) {
            octantMask ^= 2;
            boundOffset.z = dimensions.z;
        }
        if (position.y > center.y || (position.y == center.y && direction.y > 0)) {
            octantMask ^= 4;
            boundOffset.y = dimensions.y;
        }

        bool isValid = bool((node.validMask >> octantMask) & 1);
        bool isLeaf = bool((node.leafMask >> octantMask) & 1);

        if (currentStack + 1 >= maxLOD) // Out of LOD
        {
            uint8_t materialIndex = nodes.nodes[parent].materialIndex;

            float voxelScale = scale;
            vec3 voxelMinBound = minBound + boundOffset * voxelScale;
            vec3 voxelMaxBound = voxelMinBound + dimensions * voxelScale;

            hit.t = t;
            hit.position = calculatePosition(origin, direction, t);
            hit.parent = parent;
            hit.normal = normalFromBounds(position, voxelMinBound, voxelMaxBound);
            hit.materialIndex = materialIndex;
            hit.deepest += 1;

            return hit;
        }

        if (isValid)
        {
            if (isLeaf) // Solid Voxel
            {
                uint nodeIndex = parent + node.childPtr + bitCount(uint(node.validMask) >> (octantMask + 1));
                uint8_t materialIndex = nodes.nodes[nodeIndex].materialIndex;

                float voxelScale = scale;
                vec3 voxelMinBound = minBound + boundOffset * voxelScale;
                vec3 voxelMaxBound = voxelMinBound + dimensions * voxelScale;

                hit.t = t;
                hit.position = calculatePosition(origin, direction, t);
                hit.parent = parent;
                hit.normal = normalFromBounds(position, voxelMinBound, voxelMaxBound);
                hit.materialIndex = materialIndex;
                hit.deepest += 1;

                return hit;
            }
            else // Parent Voxel, Save State, Descend
            {
                if (node.childPtr == 0) break;

                StackMember stackMember;
                stackMember.parent = parent;
                stackMember.tMax = tMax;
                stackMember.minBound = minBound;

                stack[currentStack + 1] = stackMember;
                currentStack++;

                uint count = uint(node.validMask) >> (octantMask + 1);
                parent = parent + node.childPtr + bitCount(count);
                node = nodes.nodes[parent];

                minBound += boundOffset * scale;
                maxBound = minBound + scale * dimensions;

                if (!rayBoxIntersect(origin, invDir, minBound, maxBound, tMin, tMax, tMin, tMax)) break;

                node = nodes.nodes[parent];

                scale *= 0.5;

                continue;
            }
        }
        else // Traversing through air
        {
            vec3 octantMinBound = minBound + boundOffset * scale;
            vec3 octantMaxBound = octantMinBound + scale * dimensions;

            float t0;
            if (!rayBoxIntersect(origin, invDir, octantMinBound, octantMaxBound, tMin, tMax, t0, t)) break;

            t += epsilon;
            position = calculatePosition(origin, direction, t);

            continue;
        }
    }

    hit.t = -1;
    return hit;
}
