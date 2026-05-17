#pragma once

#include <cmath>
#include <immintrin.h>

typedef unsigned int       uint32;
typedef unsigned long long uint64;
typedef int                int32;

struct Vec2 { float x, y; };
struct Vec3 {
    float x, y, z;

    inline float& operator[](int i) {
        return (i == 0) ? x : (i == 1) ? y : z;
    }
    inline float operator[](int i) const {
        return (i == 0) ? x : (i == 1) ? y : z;
    }
};
struct Vec4 { float x, y, z, w; };

inline Vec3 make_vec3(float x, float y, float z) { Vec3 r = { x, y, z }; return r; }
inline Vec3 make_vec3(float s) { return make_vec3(s, s, s); }

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return make_vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return make_vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator-(const Vec3& a) { return make_vec3(-a.x, -a.y, -a.z); }
inline Vec3 operator*(const Vec3& a, float s) { return make_vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(float s, const Vec3& a) { return a * s; }
inline Vec3 operator*(const Vec3& a, const Vec3& b) { return make_vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline Vec3 operator/(const Vec3& a, float s) { return a * (1.0f / s); }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return make_vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float length(const Vec3& v) { return sqrtf(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) { return v / length(v); }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

inline float fminf_(float a, float b) { return a < b ? a : b; }
inline float fmaxf_(float a, float b) { return a > b ? a : b; }
inline float clamp(float v, float lo, float hi) { return fmaxf_(lo, fminf_(hi, v)); }

inline Vec3 reflect(const Vec3& I, const Vec3& N) { return I - 2.0f * dot(I, N) * N; }
inline Vec3 refract(const Vec3& I, const Vec3& N, float eta) {
    float cosI = -dot(I, N);
    float sinT2 = eta * eta * (1.0f - cosI * cosI);
    if (sinT2 > 1.0f) return make_vec3(0, 0, 0);
    float cosT = sqrtf(1.0f - sinT2);
    return eta * I + (eta * cosI - cosT) * N;
}
inline float schlick(float cosine, float eta) {
    float r0 = (1.0f - eta) / (1.0f + eta);
    r0 = r0 * r0;
    return r0 + (1.0f - r0) * powf(1.0f - cosine, 5.0f);
}
