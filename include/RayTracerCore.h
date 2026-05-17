#pragma once

#include "CPUTypes.h"
#include "Scene.h"
#include "Ray.h"

struct Hit {
    float t;
    Vec3 pos;
    Vec3 normal;
    int materialIndex;
};

Vec3 SkyColor(const Vec3& dir);

// Recursive trace for reflected / refracted rays.
Vec3 TraceSecondary(const Ray& ray, const Scene& scene, int depth);

// Trace a 2x2 packet of primary rays through the BVH + dynamic light sphere.
void TracePacket4(Ray rays[4], const Scene& scene, Vec3 outColor[4]);

Vec3 TracePrimaryRay(const Ray& ray, const Scene& scene);
