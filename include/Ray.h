#pragma once

#include "CPUTypes.h"

struct Ray {
    Vec3 origin;
    Vec3 dir;
    Vec3 invDir;
    float tmin;
    float tmax;
};

inline void SetRay(Ray& r, const Vec3& o, const Vec3& d) {
    r.origin = o;
    r.dir = d;
    r.invDir = make_vec3(1.0f / d.x, 1.0f / d.y, 1.0f / d.z);
    r.tmin = 1e-4f;
    r.tmax = 1e30f;
}

struct RayPacket4 {
    __m128 ox, oy, oz;
    __m128 dx, dy, dz;
    __m128 invDx, invDy, invDz;
    __m128 tmin, tmax;
};

inline RayPacket4 MakeRayPacket4(const Ray r[4]) {
    RayPacket4 p;
    p.ox = _mm_set_ps(r[3].origin.x, r[2].origin.x, r[1].origin.x, r[0].origin.x);
    p.oy = _mm_set_ps(r[3].origin.y, r[2].origin.y, r[1].origin.y, r[0].origin.y);
    p.oz = _mm_set_ps(r[3].origin.z, r[2].origin.z, r[1].origin.z, r[0].origin.z);

    p.dx = _mm_set_ps(r[3].dir.x, r[2].dir.x, r[1].dir.x, r[0].dir.x);
    p.dy = _mm_set_ps(r[3].dir.y, r[2].dir.y, r[1].dir.y, r[0].dir.y);
    p.dz = _mm_set_ps(r[3].dir.z, r[2].dir.z, r[1].dir.z, r[0].dir.z);
    p.invDx = _mm_set_ps(r[3].invDir.x, r[2].invDir.x, r[1].invDir.x, r[0].invDir.x);
    p.invDy = _mm_set_ps(r[3].invDir.y, r[2].invDir.y, r[1].invDir.y, r[0].invDir.y);
    p.invDz = _mm_set_ps(r[3].invDir.z, r[2].invDir.z, r[1].invDir.z, r[0].invDir.z);
    p.tmin = _mm_set_ps(r[3].tmin, r[2].tmin, r[1].tmin, r[0].tmin);
    return p;
}
