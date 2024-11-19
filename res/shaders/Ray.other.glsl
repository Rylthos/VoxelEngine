struct Ray
{
    vec3 origin;
    vec3 direction;
    vec3 invDir;
};

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

bool rayBoxIntersect(vec3 origin, vec3 invDir, vec3 minBound, vec3 maxBound, in float minT, in float maxT,
    out float tMin, out float tMax)
{
    vec3 tbot = invDir * (minBound - origin);
    vec3 ttop = invDir * (maxBound - origin);

    vec3 tmin = min(ttop, tbot);
    vec3 tmax = max(ttop, tbot);

    bvec3 minInf = isinf(tmin);
    bvec3 maxInf = isinf(tmin);
    tmin = mix(tmin, vec3(-1. / 0.), minInf);
    tmax = mix(tmax, vec3(1. / 0.), maxInf);

    vec2 t_int = max(tmin.xx, tmin.yz);
    float t0 = max(t_int.x, t_int.y);
    t_int = min(tmax.xx, tmax.yz);
    float t1 = min(t_int.x, t_int.y);

    if (t1 > max(t0, 0.))
    {
        tMin = max(t0, minT);
        tMax = min(t1, maxT);
        return true;
    }

    return false;
}

bool rayBoxIntersect(Ray ray, vec3 minBound, vec3 maxBound, in float minT, in float maxT,
    out float tMin, out float tMax)
{
    vec3 tbot = ray.invDir * (minBound - ray.origin);
    vec3 ttop = ray.invDir * (maxBound - ray.origin);

    vec3 tmin = min(ttop, tbot);
    vec3 tmax = max(ttop, tbot);

    float t0 = max(tmin.x, max(tmin.y, tmin.z));
    float t1 = min(tmax.x, min(tmax.y, tmax.z));

    if (t1 >= max(t0, 0.0))
    {
        tMin = max(t0, minT);
        tMax = min(t1, maxT);
        return true;
    }
    return false;
}

vec3 rayPosition(Ray ray, float t)
{
    return ray.origin + ray.direction * t;
}
