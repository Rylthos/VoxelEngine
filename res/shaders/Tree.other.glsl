#define MAX_ITERATIONS 128

struct Node
{
    uint packedData[3];
};

bool nodeIsLeaf(Node node)
{
    return bool((node.packedData[0] >> 31) & 0x1);
}

uint nodeChildPtr(Node node)
{
    return (node.packedData[0] & 0x7FFFFFFF);
}

uint64_t nodeChildMask(Node node)
{
    return (node.packedData[1] | uint64_t(node.packedData[2]) << 32);
}

layout (buffer_reference, std430) readonly buffer NodeBuffer
{
    Node nodes[];
};

layout (buffer_reference, std430) readonly buffer LeafData
{
    uint8_t leafData[];
};

struct VoxelTree
{
    NodeBuffer nodePool;
    LeafData leafData;
};

struct HitInfo
{
    vec3 position;
};

uint getNodeCellIndex(vec3 pos, int scaleExp)
{
    uvec3 cellPos = uvec3(pos) >> scaleExp & 3;
    return cellPos.x + cellPos.z * 4 + cellPos.y * 16;
}

vec3 floorScale(vec3 pos, int scaleExp)
{
    uint mask = ~0u << scaleExp;
    return vec3(uvec3(pos) & mask);
}

// Count number of set bits in variable length [0..width]
uint popcnt64(uint64_t mask, uint width)
{
    uint himask = uint(mask);
    uint count = 0;
    if (width >= 32)
    {
        count = bitCount(himask);
        himask = uint(mask >> 32);
    }
    uint m = 1u << (width & 31u);
    count += bitCount(himask & (m - 1u));
    return count;
}

HitInfo rayCast(VoxelTree tree, Ray ray, bool coarse)
{
    uint stack[11];
    int scaleExp = 21;

    uint nodeIdx = 0;
    Node node = tree.nodePool.nodes[nodeIdx];

    if (abs(ray.direction.x) < 0.0001) ray.direction.x = 0.0001;
    if (abs(ray.direction.y) < 0.0001) ray.direction.y = 0.0001;
    if (abs(ray.direction.z) < 0.0001) ray.direction.z = 0.0001;

    ray.invDir = 1. / ray.direction;

    vec3 pos = clamp(ray.origin, 1.0f, 1.9999999f);
    vec3 sideDist;

    for (int i = 0; i < MAX_ITERATIONS; i++)
    {
        if (coarse && i > 20 && nodeIsLeaf(node)) break;

        uint childIdx = getNodeCellIndex(pos, scaleExp);

        while (!nodeIsLeaf(node) && (nodeChildMask(node) >> childIdx & 1) != 0)
        {
            stack[scaleExp >> 1] = nodeIdx;

            nodeIdx = nodeChildPtr(node) +
                popcnt64(nodeChildMask(node), childIdx);
            node = tree.nodePool.nodes[nodeIdx];

            scaleExp -= 2;
            childIdx = getNodeCellIndex(pos, scaleExp);
        }
        if ((nodeChildMask(node) >> childIdx & 1) != 0 && nodeIsLeaf(node)) break;

        // int advScaleExp = scaleExp;
        // if ((nodeChildMask(node) >> (childIdx & 0b101010) & 0x00330033) == 0) advScaleExp++;

        pos = floorScale(pos, scaleExp);
        vec3 prevPos = pos;

        float scale = float((scaleExp - 23 + 127) << 23);
        sideDist = (step(0.0f, ray.direction) * scale + (pos - ray.origin)) * ray.invDir;

        float tMax = min(min(sideDist.x, sideDist.y), sideDist.z);

        // vec3 siblPos0 = select(tMax == sideDist)

        // uvec3 diffPos = uvec3(pos) ^ uvec3(prevPos);

        // vec3 cellMin = floorScale(ray.origin, scaleExp);
        // vec3 cellSize = vec3(scale);
        //
        // pos = origin + dir * tmax;
    }

    HitInfo hit;
    if (nodeIsLeaf(node))
    {
        uint childIdx = getNodeCellIndex(pos, scaleExp);
        hit.position = pos;
    }

    return hit;
}
