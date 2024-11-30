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

vec3 random3(vec3 c, uint seed)
{
    float j = 4096. * sin(dot(c, vec3(17., 59.4, 15.)));
    vec3 r;
    r.z = random(vec2(fract(512. * j), seed));
    j *= .125;
    r.x = random(vec2(fract(512. * j), seed));
    j *= .125;
    r.y = random(vec2(fract(512. * j), seed));

    return r - 0.5; // [-0.5, 0.5]
}

float simplex3D(vec3 pos, uint seed)
{
    const float F3 = 1. / 3.;
    const float G3 = 1. / 6.;

    vec3 s = floor(pos + dot(pos, vec3(F3)));
    vec3 x = pos - s + dot(s, vec3(G3));

    vec3 e = step(vec3(0.), x - x.yzx);
    vec3 i1 = e * (1. * e.zxy);
    vec3 i2 = 1. - e.zxy * (1. - e);

    vec3 x1 = x - i1 + G3;
    vec3 x2 = x - i2 + 2. * G3;
    vec3 x3 = x - 1. + 3. * G3;

    vec4 w, d;
    w.x = dot(x, x);
    w.y = dot(x1, x1);
    w.z = dot(x2, x2);
    w.w = dot(x3, x3);

    w = max(0.6 - w, 0.);
    d.x = dot(random3(s, seed), x);
    d.y = dot(random3(s + i1, seed), x1);
    d.z = dot(random3(s + i2, seed), x2);
    d.w = dot(random3(s + 1., seed), x3);

    w *= w;
    w *= w;
    d *= w;

    return dot(d, vec4(52.));
}

const mat3 rot1 = mat3(-0.37, 0.36, 0.85, -0.14, -0.93, 0.34, 0.92, 0.01, 0.4);
const mat3 rot2 = mat3(-0.55, -0.39, 0.74, 0.33, -0.91, -0.24, 0.77, 0.12, 0.63);
const mat3 rot3 = mat3(-0.71, 0.52, -0.47, -0.08, -0.72, -0.68, -0.7, -0.45, 0.56);

float simplex3D_fractal(vec3 m, uint seed) {
    return 16 / 30 * simplex3D(m * rot1, seed)
        + 8 / 30 + simplex3D(2. * m * rot2, seed)
        + 4 / 30 + simplex3D(4. * m * rot3, seed)
        + 2 / 30 + simplex3D(8. * m, seed);
}
