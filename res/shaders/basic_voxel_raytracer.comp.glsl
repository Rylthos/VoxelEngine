#version 460

#extension GL_EXT_buffer_reference : enable

layout (local_size_x = 16, local_size_y = 16) in;

layout (rgba16f, set = 0, binding = 0) uniform image2D o_Image;

struct Voxel
{
    vec4 colour;
};

struct Ray
{
    vec3 origin;
    vec3 direction;
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

float hit(Ray ray, Voxel voxel, ivec3 position)
{
    vec3 center = voxelOrigin + vec3(position) * p_Size;
    vec3 minBound = center;
    vec3 maxBound = center + vec3(p_Size);

    vec3 invDir = 1. / ray.direction;

    float t1 = (minBound.x - ray.origin.x) * invDir.x;
    float t2 = (maxBound.x - ray.origin.x) * invDir.x;

    float tmin = min(t1, t2);
    float tmax = max(t1, t2);

    float t3 = (minBound.y - ray.origin.y) * invDir.y;
    float t4 = (maxBound.y - ray.origin.y) * invDir.y;

    tmin = max(tmin, min(t3, t4));
    tmax = min(tmax, max(t3, t4));

    float t5 = (minBound.z - ray.origin.z) * invDir.z;
    float t6 = (maxBound.z - ray.origin.z) * invDir.z;

    tmin = max(tmin, min(t5, t6));
    tmax = min(tmax, max(t5, t6));

    if (tmax >= tmin)
        if (tmin > 0.)
            return tmin;
        else
            return tmax;
    else
        return -1.0f;
}

void main()
{
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(o_Image);

    vec2 uv = vec2(texelCoord) / vec2(size - 1);

    float viewportWidth = 2.0;
    float viewportHeight = 2.0;
    float viewportDepth = 1.0;

    vec3 viewportTopLeft = vec3(p_CameraPosition + viewportDepth * p_CameraForward - (p_CameraRight * viewportWidth / 2.) + (p_CameraUp * viewportHeight / 2.));
    vec3 deltaRight = vec3(p_CameraRight * viewportWidth);
    vec3 deltaDown = vec3(-p_CameraUp * viewportHeight);

    vec3 origin = vec3(p_CameraPosition);
    vec3 target = viewportTopLeft + uv.x * deltaRight + uv.y * deltaDown;
    vec3 direction = normalize(target - origin);

    Ray ray;
    ray.origin = origin;
    ray.direction = direction;

    float minHit = 10000.;
    Voxel minHitVoxel;
    bool hasHit = false;

    for (int y = 0; y < p_Dimensions.y; y++)
    {
        for (int z = 0; z < p_Dimensions.z; z++)
        {
            for (int x = 0; x < p_Dimensions.x; x++)
            {
                uint index = x + z * p_Dimensions.x + y * p_Dimensions.x * p_Dimensions.z;
                Voxel voxel = p_Voxels.voxels[index];

                float hitPos = hit(ray, voxel, ivec3(x, y, z));

                if (hitPos > 0. && hitPos < minHit)
                {
                    minHit = hitPos;
                    minHitVoxel = voxel;
                    hasHit = true;
                    // break;
                }
            }
        }
    }

    if (hasHit)
        imageStore(o_Image, texelCoord, minHitVoxel.colour);
    else
        imageStore(o_Image, texelCoord, vec4(0.));
}
