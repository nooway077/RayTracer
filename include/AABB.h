#pragma once

#include "CPUTypes.h"
#include "Ray.h"

struct AABB {
    Vec3 min;
    Vec3 max;
};

inline AABB MakeEmptyAABB() {
    AABB a = { { 1e30f, 1e30f, 1e30f }, { -1e30f,-1e30f,-1e30f } };
    return a;
}
inline void Expand(AABB& a, const Vec3& p) {
    a.min.x = fminf_(a.min.x, p.x); a.min.y = fminf_(a.min.y, p.y); a.min.z = fminf_(a.min.z, p.z);
    a.max.x = fmaxf_(a.max.x, p.x); a.max.y = fmaxf_(a.max.y, p.y); a.max.z = fmaxf_(a.max.z, p.z);
}
inline void Expand(AABB& a, const AABB& b) { Expand(a, b.min); Expand(a, b.max); }
inline float Area(const AABB& a) {
    Vec3 e = { a.max.x - a.min.x, a.max.y - a.min.y, a.max.z - a.min.z };
    return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
}

// Scalar slab.
inline bool IntersectAABB(const Ray& r, const AABB& box, float t_min, float t_max) {
    float t1 = (box.min.x - r.origin.x) * r.invDir.x;
    float t2 = (box.max.x - r.origin.x) * r.invDir.x;
    float tmin = fminf_(t1, t2);
    float tmax = fmaxf_(t1, t2);

    t1 = (box.min.y - r.origin.y) * r.invDir.y;
    t2 = (box.max.y - r.origin.y) * r.invDir.y;
    tmin = fmaxf_(tmin, fminf_(t1, t2));
    tmax = fminf_(tmax, fmaxf_(t1, t2));

    t1 = (box.min.z - r.origin.z) * r.invDir.z;
    t2 = (box.max.z - r.origin.z) * r.invDir.z;
    tmin = fmaxf_(tmin, fminf_(t1, t2));
    tmax = fminf_(tmax, fmaxf_(t1, t2));

    return tmax >= fmaxf_(tmin, t_min) && tmin <= t_max;
}

// SSE packet slab (4 rays vs 1 AABB).
inline __m128 IntersectAABBPacket4(const RayPacket4& p, const AABB& box) {
    __m128 t1 = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(box.min.x), p.ox), p.invDx);
    __m128 t2 = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(box.max.x), p.ox), p.invDx);
    __m128 tmin = _mm_min_ps(t1, t2);
    __m128 tmax = _mm_max_ps(t1, t2);

    t1 = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(box.min.y), p.oy), p.invDy);
    t2 = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(box.max.y), p.oy), p.invDy);
    tmin = _mm_max_ps(tmin, _mm_min_ps(t1, t2));
    tmax = _mm_min_ps(tmax, _mm_max_ps(t1, t2));

    t1 = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(box.min.z), p.oz), p.invDz);
    t2 = _mm_mul_ps(_mm_sub_ps(_mm_set1_ps(box.max.z), p.oz), p.invDz);
    tmin = _mm_max_ps(tmin, _mm_min_ps(t1, t2));
    tmax = _mm_min_ps(tmax, _mm_max_ps(t1, t2));

    return _mm_cmpge_ps(tmax, _mm_max_ps(tmin, p.tmin));
}
