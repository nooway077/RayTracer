#pragma once

#include "CPUTypes.h"
#include "Ray.h"
#include "AABB.h"

struct Sphere {
    Vec3 center;
    float radius;
    int materialIndex;
};

inline bool IntersectSphere(const Ray& r, const Sphere& s, float& tHit, Vec3& outN) {
    Vec3 oc = r.origin - s.center;
    float a = dot(r.dir, r.dir);
    float b = 2.0f * dot(oc, r.dir);
    float c = dot(oc, oc) - s.radius * s.radius;
    float disc = b * b - 4 * a * c;
    if (disc < 0.0f) return false;
    float sqrtd = sqrtf(disc);
    float root = (-b - sqrtd) / (2.0f * a);
    if (root < r.tmin || root > r.tmax) {
        root = (-b + sqrtd) / (2.0f * a);
        if (root < r.tmin || root > r.tmax) return false;
    }
    tHit = root;
    Vec3 p = r.origin + r.dir * tHit;
    outN = (p - s.center) / s.radius;
    return true;
}

inline AABB GetAABB(const Sphere& s) {
    AABB a;
    a.min = s.center - make_vec3(s.radius, s.radius, s.radius);
    a.max = s.center + make_vec3(s.radius, s.radius, s.radius);
    return a;
}
