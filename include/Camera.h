#pragma once

#include "CPUTypes.h"
#include "Ray.h"

struct Camera {
    Vec3 origin;
    Vec3 lowerLeftCorner;
    Vec3 horizontal;
    Vec3 vertical;
    void Init(const Vec3& eye, const Vec3& lookAt, const Vec3& up, float fovDeg, float aspect);
    void GetRay(float s, float t, Ray& r) const;
};
