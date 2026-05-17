#pragma once

#ifdef __CUDACC__
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <math.h>
#define RAYCUDA_HD __host__ __device__
#else
#include <math.h>
#define RAYCUDA_HD
#endif

#include <cstdint>

namespace RayCuda {

    struct float2 { float x, y; };
    struct float3 { float x, y, z; };
    struct float4 { float x, y, z, w; };

    RAYCUDA_HD inline float3 make_float3(float x, float y, float z) { float3 r = { x, y, z }; return r; }
    RAYCUDA_HD inline float3 make_float3(float s) { return make_float3(s, s, s); }

    RAYCUDA_HD inline float3 operator+(const float3& a, const float3& b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
    RAYCUDA_HD inline float3 operator-(const float3& a, const float3& b) { return make_float3(a.x - b.x, a.y - b.y, a.z - b.z); }
    RAYCUDA_HD inline float3 operator-(const float3& a) { return make_float3(-a.x, -a.y, -a.z); }
    RAYCUDA_HD inline float3 operator*(const float3& a, float s) { return make_float3(a.x * s, a.y * s, a.z * s); }
    RAYCUDA_HD inline float3 operator*(float s, const float3& a) { return a * s; }
    RAYCUDA_HD inline float3 operator*(const float3& a, const float3& b) { return make_float3(a.x * b.x, a.y * b.y, a.z * b.z); }
    RAYCUDA_HD inline float3 operator/(const float3& a, float s) { return a * (1.0f / s); }

    RAYCUDA_HD inline float3& operator+=(float3& a, const float3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
    RAYCUDA_HD inline float3& operator-=(float3& a, const float3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
    RAYCUDA_HD inline float3& operator*=(float3& a, float s) { a.x *= s;   a.y *= s;   a.z *= s;   return a; }
    RAYCUDA_HD inline float3& operator/=(float3& a, float s) { float inv = 1.0f / s; a.x *= inv; a.y *= inv; a.z *= inv; return a; }

    RAYCUDA_HD inline float  dot(const float3& a, const float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    RAYCUDA_HD inline float3 cross(const float3& a, const float3& b) {
        return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }
    RAYCUDA_HD inline float  length(const float3& v) { return sqrtf(dot(v, v)); }
    RAYCUDA_HD inline float3 normalize(const float3& v) { return v / length(v); }
    RAYCUDA_HD inline float3 lerp(const float3& a, const float3& b, float t) { return a + (b - a) * t; }

    RAYCUDA_HD inline float fminf_(float a, float b) { return a < b ? a : b; }
    RAYCUDA_HD inline float fmaxf_(float a, float b) { return a > b ? a : b; }
    RAYCUDA_HD inline float clamp(float v, float lo, float hi) { return fmaxf_(lo, fminf_(hi, v)); }

    RAYCUDA_HD inline float3 reflect(const float3& I, const float3& N) { return I - 2.0f * dot(I, N) * N; }
    RAYCUDA_HD inline float3 refract(const float3& I, const float3& N, float eta) {
        float cosI = -dot(I, N);
        float sinT2 = eta * eta * (1.0f - cosI * cosI);
        if (sinT2 > 1.0f) return make_float3(0, 0, 0);
        float cosT = sqrtf(1.0f - sinT2);
        return eta * I + (eta * cosI - cosT) * N;
    }
    RAYCUDA_HD inline float schlick(float cosine, float eta) {
        float r0 = (1.0f - eta) / (1.0f + eta);
        r0 = r0 * r0;
        return r0 + (1.0f - r0) * powf(1.0f - cosine, 5.0f);
    }
}
