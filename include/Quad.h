#pragma once

#include "CPUTypes.h"
#include "Ray.h"
#include "AABB.h"

struct Quad {
    Vec3 Q;
    Vec3 u;
    Vec3 v;
    Vec3 normal;
    float d;
    int materialIndex;
    // Cached by InitQuad for fast scalar intersection.
    Vec3 w;
    float invDotW;
};

inline void InitQuad(Quad& q) {
    q.normal = normalize(cross(q.u, q.v));
    q.d = dot(q.normal, q.Q);
    q.w = cross(q.u, q.v);
    q.invDotW = 1.0f / dot(q.w, q.w);
}

inline AABB GetAABB(const Quad& q) {
    AABB a = MakeEmptyAABB();
    Expand(a, q.Q);
    Expand(a, q.Q + q.u);
    Expand(a, q.Q + q.v);
    Expand(a, q.Q + q.u + q.v);
    return a;
}

// Fast quad intersection (uses cached w / invDotW).
inline bool IntersectQuad(const Ray& r, const Quad& q, float& tHit, Vec3& outN) {
    float denom = dot(q.normal, r.dir);
    if (fabsf(denom) < 1e-6f) return false;
    tHit = (q.d - dot(q.normal, r.origin)) / denom;
    if (tHit < r.tmin || tHit > r.tmax) return false;

    Vec3 planar = (r.origin + r.dir * tHit) - q.Q;
    float alpha = dot(cross(planar, q.v), q.w) * q.invDotW;
    float beta = dot(cross(q.u, planar), q.w) * q.invDotW;
    if (alpha < 0.0f || alpha > 1.0f || beta < 0.0f || beta > 1.0f) return false;

    outN = (denom < 0.0f) ? q.normal : (-1.0f * q.normal);
    return true;
}
