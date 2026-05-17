#include "stdafx.h"
#include "Camera.h"

void Camera::Init(const Vec3& eye, const Vec3& lookAt, const Vec3& up, float fovDeg, float aspect) {
    origin = eye;
    Vec3 w = normalize(eye - lookAt);
    Vec3 u = normalize(cross(up, w));
    Vec3 v = cross(w, u);
    float theta = fovDeg * 3.14159265f / 180.0f;
    float halfH = tanf(theta * 0.5f);
    float halfW = aspect * halfH;
    lowerLeftCorner = origin - halfW * u - halfH * v - w;
    horizontal = 2.0f * halfW * u;
    vertical = 2.0f * halfH * v;
}

void Camera::GetRay(float s, float t, Ray& r) const {
    Vec3 dir = lowerLeftCorner + s * horizontal + t * vertical - origin;
    SetRay(r, origin, normalize(dir));
}
