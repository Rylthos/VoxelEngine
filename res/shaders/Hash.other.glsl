uint hash(uint x)
{
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

uint hash(uvec2 v)
{
    return hash(v.x ^ hash(v.y));
}

uint hash(uvec3 v)
{
    return hash(v.x ^ hash(v.y) ^ hash(v.z));
}

uint hash(uvec4 v)
{
    return hash(v.x ^ hash(v.y) ^ hash(v.z) ^ hash(v.w));
}

float randomFromHash(uint h)
{
    const uint mantissaMask = 0x007FFFFFu;
    const uint one = 0x3F800000u;

    h &= mantissaMask;
    h |= one;

    float r2 = uintBitsToFloat(h);
    return r2 - 1.0; // [0, 1]
}

float random(float f)
{
    return randomFromHash(hash(floatBitsToUint(f)));
}

float random(vec2 f)
{
    return randomFromHash(hash(floatBitsToUint(f)));
}

float random(vec3 f)
{
    return randomFromHash(hash(floatBitsToUint(f)));
}

float random(vec4 f)
{
    return randomFromHash(hash(floatBitsToUint(f)));
}
