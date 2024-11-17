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

bool rayBoxIntersect(Ray ray, vec3 minBound, vec3 maxBound, float minT, float maxT,
    out float tMin, out float tMax)
{
    vec3 tbot = ray.invDir * (minBound - ray.origin);
    vec3 ttop = ray.invDir * (maxBound - ray.origin);

    vec3 tmin = min(ttop, tbot);
    vec3 tmax = max(ttop, tbot);

    // vec2 t = max(tmin.xx, tmin.yz);
    // float t0 = max(t.x, t.y);
    float t0 = max(tmin.x, max(tmin.y, tmin.z));
    // t = min(tmax.xx, tmax.yz);
    // float t1 = min(t.x, t.y);
    float t1 = min(tmax.x, min(tmax.y, tmax.z));
    tMin = t0;
    tMax = t1;
    return t1 > max(t0, 0.0) && tMax > minT && tMin < maxT;
}

vec3 rayPosition(Ray ray, float t)
{
    return ray.origin + ray.direction * t;
}
