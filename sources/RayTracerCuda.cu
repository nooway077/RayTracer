#include "RayTracerCuda.cuh"
#include "RenderConfig.h"

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <math.h>

__constant__ RayCuda::CudaScene g_cudaScene;

namespace RayCuda {

    __device__ __forceinline__ void SetRay(Ray& r, const float3& o, const float3& d) {
        r.origin = o;
        r.dir = d;
        r.invDir = make_float3(1.0f / d.x, 1.0f / d.y, 1.0f / d.z);
        r.tmin = RenderConfig::RayEpsilon;
        r.tmax = RenderConfig::RayTMax;
    }

    __device__ __forceinline__ bool IntersectAABB(const Ray& r, const AABB& box, float t_min, float t_max) {
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

    __device__ __forceinline__ bool IntersectSphere(const Ray& r, const Sphere& s, float& tHit, float3& outN) {
        float3 oc = r.origin - s.center;
        float a = dot(r.dir, r.dir);
        float b = 2.0f * dot(oc, r.dir);
        float c = dot(oc, oc) - s.radius * s.radius;
        float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) return false;
        float sqrtd = sqrtf(disc);
        float root = (-b - sqrtd) / (2.0f * a);
        if (root < r.tmin || root > r.tmax) {
            root = (-b + sqrtd) / (2.0f * a);
            if (root < r.tmin || root > r.tmax) return false;
        }
        tHit = root;
        float3 p = r.origin + r.dir * tHit;
        outN = (p - s.center) / s.radius;
        return true;
    }

    __device__ __forceinline__ bool IntersectQuad(const Ray& r, const Quad& q, float& tHit, float3& outN) {
        float denom = dot(q.normal, r.dir);
        if (fabsf(denom) < 1e-6f) return false;
        tHit = (q.d - dot(q.normal, r.origin)) / denom;
        if (tHit < r.tmin || tHit > r.tmax) return false;

        float3 planar = (r.origin + r.dir * tHit) - q.Q;
        float alpha = dot(cross(planar, q.v), q.w) * q.invDotW;
        float beta = dot(cross(q.u, planar), q.w) * q.invDotW;
        if (alpha < 0.0f || alpha > 1.0f || beta < 0.0f || beta > 1.0f) return false;

        outN = (denom < 0.0f) ? q.normal : (-1.0f * q.normal);
        return true;
    }

    __device__ __forceinline__ bool IntersectLightSphere(const Ray& r, float& tHit, float3& outN) {
        float3 oc = r.origin - g_cudaScene.light.position;
        float h = dot(oc, r.dir);
        float c = dot(oc, oc) - g_cudaScene.lightRadius * g_cudaScene.lightRadius;
        float disc = h * h - c;
        if (disc < 0.0f) return false;
        float sqrtd = sqrtf(disc);
        float root = -h - sqrtd;
        if (root < r.tmin || root > r.tmax) {
            root = -h + sqrtd;
            if (root < r.tmin || root > r.tmax) return false;
        }
        tHit = root;
        outN = (oc + r.dir * tHit) * (1.0f / g_cudaScene.lightRadius);
        return true;
    }

    __device__ __forceinline__ uint32_t PackRGBA8(const float3& c) {
        float r = sqrtf(clamp(c.x, 0.0f, 1.0f));
        float g = sqrtf(clamp(c.y, 0.0f, 1.0f));
        float b = sqrtf(clamp(c.z, 0.0f, 1.0f));
        uint32_t ir = (uint32_t)(r * 255.0f + 0.5f);
        uint32_t ig = (uint32_t)(g * 255.0f + 0.5f);
        uint32_t ib = (uint32_t)(b * 255.0f + 0.5f);
        return (0xFF000000u) | (ib << 16) | (ig << 8) | ir;
    }

    __device__ __forceinline__ float3 EvaluateTexture(const Material& mat, const float3& pos) {
        if (mat.texture == TEX_CHECKER) {
            int x = (int)floorf(pos.x * mat.texScale);
            int y = (int)floorf(pos.y * mat.texScale);
            int z = (int)floorf(pos.z * mat.texScale);
            bool odd = ((x + y + z) & 1) != 0;
            return odd ? make_float3(0.9f, 0.9f, 0.9f) : mat.albedo;
        }
        if (mat.texture == TEX_GRID) {
            float sx = pos.x * mat.texScale;
            float sy = pos.y * mat.texScale;
            float sz = pos.z * mat.texScale;
            float bx = sx - floorf(sx);
            float by = sy - floorf(sy);
            float bz = sz - floorf(sz);
            bool line = (bx < 0.03f) || (by < 0.03f) || (bz < 0.03f);
            return line ? make_float3(0.1f, 0.1f, 0.1f) : mat.albedo;
        }
        return mat.albedo;
    }

    __device__ __forceinline__ float3 DirectLightNoShadow(const float3& hitPos, const float3& N,
        const float3& V, const Material& mat, const float3& albedo) {
        float3 color = make_float3(0.0f, 0.0f, 0.0f);
        float3 toLight = g_cudaScene.light.position - hitPos;
        float dist2 = dot(toLight, toLight);
        float dist = sqrtf(dist2);
        if (dist < RenderConfig::LightAttenClamp) dist = RenderConfig::LightAttenClamp;

        float3 L = toLight / dist;
        float NdotL = fmaxf_(dot(N, L), 0.0f);
        if (NdotL > 0.0f) {
            float att = g_cudaScene.light.intensity / (dist * dist);
            if (mat.type == MAT_LAMBERTIAN) {
                color += albedo * g_cudaScene.light.color * (NdotL * att);
            }
            else if (mat.type == MAT_METAL) {
                float3 H = normalize(L + V);
                float spec = powf(fmaxf_(dot(N, H), 0.0f), 32.0f) * NdotL;
                color += albedo * g_cudaScene.light.color * (spec * att);
            }
        }
        return color;
    }

    __device__ __forceinline__ float3 SkyColor(const float3& dir) {
        float t = 0.5f * (dir.y + 1.0f);
        return lerp(make_float3(1.0f, 1.0f, 1.0f),
            make_float3(0.5f, 0.7f, 1.0f), t);
    }

    __device__ __forceinline__ float fresnel(float cosi, float eta) {
        float r0 = (1.0f - eta) / (1.0f + eta);
        r0 = r0 * r0;
        return r0 + (1.0f - r0) * powf(1.0f - cosi, 5.0f);
    }

    // BVH traversal for secondary / shadow rays.
    __device__ bool SceneIntersectBVH(const Ray& r, Hit& outHit) {
        bool found = false;
        float closest = r.tmax;
        float3 bestN;
        int bestMat = -1;

        int stack[24];
        int sp = 0;
        stack[sp++] = 0;

        while (sp > 0) {
            int nidx = stack[--sp];
            const BVHNode& node = g_cudaScene.nodes[nidx];
            if (!IntersectAABB(r, node.aabb, r.tmin, closest)) continue;

            if (node.primCount > 0) {
                for (int i = 0; i < node.primCount; ++i) {
                    const PrimitiveDesc& p = g_cudaScene.primitives[node.primOffset + i];
                    float t; float3 n;
                    if (p.type == 0) {
                        if (IntersectSphere(r, g_cudaScene.spheres[p.index], t, n) && t < closest && t > r.tmin) {
                            closest = t; bestN = n; bestMat = p.materialIndex; found = true;
                        }
                    }
                    else {
                        if (IntersectQuad(r, g_cudaScene.quads[p.index], t, n) && t < closest && t > r.tmin) {
                            closest = t; bestN = n; bestMat = p.materialIndex; found = true;
                        }
                    }
                }
            }
            else {
                stack[sp++] = node.left;
                stack[sp++] = node.right;
            }
        }

        float tL; float3 nL;
        if (IntersectLightSphere(r, tL, nL) && tL < closest) {
            closest = tL; bestN = nL; bestMat = g_cudaScene.lightMaterialIndex; found = true;
        }

        if (found) {
            outHit.t = closest;
            outHit.pos = r.origin + r.dir * closest;
            outHit.normal = bestN;
            outHit.materialIndex = bestMat;
        }
        return found;
    }

    __device__ bool SceneAnyHitBVH(const Ray& r) {
        int stack[24];
        int sp = 0;
        stack[sp++] = 0;

        while (sp > 0) {
            int nidx = stack[--sp];
            const BVHNode& node = g_cudaScene.nodes[nidx];
            if (!IntersectAABB(r, node.aabb, r.tmin, r.tmax)) continue;

            if (node.primCount > 0) {
                for (int i = 0; i < node.primCount; ++i) {
                    const PrimitiveDesc& p = g_cudaScene.primitives[node.primOffset + i];
                    float t; float3 n;
                    if (p.type == 0) {
                        if (IntersectSphere(r, g_cudaScene.spheres[p.index], t, n) && t < r.tmax && t > r.tmin) return true;
                    }
                    else {
                        if (IntersectQuad(r, g_cudaScene.quads[p.index], t, n) && t < r.tmax && t > r.tmin) return true;
                    }
                }
            }
            else {
                stack[sp++] = node.left;
                stack[sp++] = node.right;
            }
        }
        return false;
    }

    // Recursive secondary trace.
    __device__ float3 TraceSecondary(const Ray& ray, int depth);

    __device__ float3 SampleDielectric(const Ray& ray, const float3& hitPos,
        const float3& N, const Material& mat, int depth) {
        bool frontFace = dot(ray.dir, N) < 0.0f;
        float3 outwardN = frontFace ? N : (-1.0f * N);
        float cosTheta = fminf_(dot(-ray.dir, outwardN), 1.0f);
        float eta = frontFace ? (1.0f / mat.ior) : mat.ior;

        float3 reflectDir = reflect(ray.dir, outwardN);
        Ray reflectRay; SetRay(reflectRay, hitPos + outwardN * RenderConfig::RayEpsilon, reflectDir);
        float3 reflectColor = TraceSecondary(reflectRay, depth - 1);

        float3 refractDir = refract(ray.dir, outwardN, eta);
        if (refractDir.x == 0.0f && refractDir.y == 0.0f && refractDir.z == 0.0f) {
            return reflectColor;
        }
        Ray refractRay; SetRay(refractRay, hitPos - outwardN * RenderConfig::RayEpsilon, refractDir);
        float3 refractColor = TraceSecondary(refractRay, depth - 1);

        float R = fresnel(cosTheta, eta);
        return reflectColor * R + refractColor * (1.0f - R);
    }

    __device__ float3 TraceSecondary(const Ray& ray, int depth) {
        Hit hit;
        if (!SceneIntersectBVH(ray, hit)) return SkyColor(ray.dir);

        if (hit.materialIndex == g_cudaScene.lightMaterialIndex) {
            return g_cudaScene.light.color * g_cudaScene.light.intensity * 0.25f;
        }

        const Material& mat = g_cudaScene.materials[hit.materialIndex];
        float3 N = hit.normal;
        float3 hitPos = hit.pos;
        float3 albedo = EvaluateTexture(mat, hitPos);

        if (mat.type == MAT_DIELECTRIC) {
            if (depth <= 0) return make_float3(0.0f, 0.0f, 0.0f);
            return SampleDielectric(ray, hitPos, N, mat, depth);
        }

        float3 ambient = SkyColor(N) * RenderConfig::AmbientSkyScale
            + make_float3(RenderConfig::AmbientFillR, RenderConfig::AmbientFillG, RenderConfig::AmbientFillB);
        float3 color = ambient * albedo;

        float bounce = fmaxf_(dot(N, make_float3(0.0f, -1.0f, 0.0f)), 0.0f);
        color += make_float3(RenderConfig::FloorBounceR, RenderConfig::FloorBounceG, RenderConfig::FloorBounceB)
            * albedo * bounce * RenderConfig::FloorBounceStr;

        color += DirectLightNoShadow(hitPos, N, -ray.dir, mat, albedo);

        if (depth > 0 && mat.type == MAT_METAL) {
            float3 refl = reflect(ray.dir, N);
            Ray rr; SetRay(rr, hitPos + N * RenderConfig::RayEpsilon, refl);
            float3 reflectColor = TraceSecondary(rr, depth - 1);

            float cosTheta = fmaxf_(dot(-ray.dir, N), 0.0f);
            float3 F0 = albedo;
            float3 F = F0 + (make_float3(1.0f, 1.0f, 1.0f) - F0) * powf(1.0f - cosTheta, 5.0f);

            color += F * reflectColor;
        }
        return color;
    }

    __device__ float3 ShadePrimary(const Ray& ray, const Hit& hit, int depth) {
        if (hit.materialIndex == g_cudaScene.lightMaterialIndex) {
            return g_cudaScene.light.color * g_cudaScene.light.intensity * 0.25f;
        }

        const Material& mat = g_cudaScene.materials[hit.materialIndex];
        float3 hitPos = hit.pos;
        float3 N = hit.normal;
        float3 V = -ray.dir;

        float3 albedo = EvaluateTexture(mat, hitPos);

        if (mat.type == MAT_DIELECTRIC) {
            if (depth <= 0) return make_float3(0.0f, 0.0f, 0.0f);
            return SampleDielectric(ray, hitPos, N, mat, depth);
        }

        float3 ambient = SkyColor(N) * RenderConfig::AmbientSkyScale
            + make_float3(RenderConfig::AmbientFillR, RenderConfig::AmbientFillG, RenderConfig::AmbientFillB);
        float3 color = ambient * albedo;

        float bounce = fmaxf_(dot(N, make_float3(0.0f, -1.0f, 0.0f)), 0.0f);
        color += make_float3(RenderConfig::FloorBounceR, RenderConfig::FloorBounceG, RenderConfig::FloorBounceB)
            * albedo * bounce * RenderConfig::FloorBounceStr;

        float3 toLight = g_cudaScene.light.position - hitPos;
        float dist = sqrtf(dot(toLight, toLight));

        Ray sray; SetRay(sray, hitPos + N * RenderConfig::RayEpsilon, toLight / dist);
        sray.tmax = dist - RenderConfig::RayEpsilon;
        bool inShadow = SceneAnyHitBVH(sray);

        float NdotL = fmaxf_(dot(N, sray.dir), 0.0f);
        if (!inShadow && NdotL > 0.0f) {
            color += DirectLightNoShadow(hitPos, N, V, mat, albedo);
        }

        if (depth > 0 && mat.type == MAT_METAL) {
            float3 refl = reflect(ray.dir, N);
            Ray rr; SetRay(rr, hitPos + N * RenderConfig::RayEpsilon, refl);
            float3 reflectColor = TraceSecondary(rr, depth - 1);

            float cosTheta = fmaxf_(dot(V, N), 0.0f);
            float3 F0 = albedo;
            float3 F = F0 + (make_float3(1.0f, 1.0f, 1.0f) - F0) * powf(1.0f - cosTheta, 5.0f);

            color += F * reflectColor;
        }

        return color;
    }

    __device__ bool TracePrimary(const Ray& ray, Hit& outHit) {
        bool found = false;
        float closest = ray.tmax;
        float3 bestN;
        int bestMat = -1;

        int stack[24];
        int sp = 0;
        stack[sp++] = 0;

        while (sp > 0) {
            int nidx = stack[--sp];
            const BVHNode& node = g_cudaScene.nodes[nidx];
            if (!IntersectAABB(ray, node.aabb, ray.tmin, closest)) continue;

            if (node.primCount > 0) {
                for (int i = 0; i < node.primCount; ++i) {
                    const PrimitiveDesc& p = g_cudaScene.primitives[node.primOffset + i];
                    float t; float3 n;
                    if (p.type == 0) {
                        if (IntersectSphere(ray, g_cudaScene.spheres[p.index], t, n)
                            && t < closest && t > ray.tmin) {
                            closest = t; bestN = n; bestMat = p.materialIndex; found = true;
                        }
                    }
                    else {
                        if (IntersectQuad(ray, g_cudaScene.quads[p.index], t, n)
                            && t < closest && t > ray.tmin) {
                            closest = t; bestN = n; bestMat = p.materialIndex; found = true;
                        }
                    }
                }
            }
            else {
                stack[sp++] = node.left;
                stack[sp++] = node.right;
            }
        }

        float tL; float3 nL;
        if (IntersectLightSphere(ray, tL, nL) && tL < closest) {
            closest = tL; bestN = nL; bestMat = g_cudaScene.lightMaterialIndex; found = true;
        }

        if (found) {
            outHit.t = closest;
            outHit.pos = ray.origin + ray.dir * closest;
            outHit.normal = bestN;
            outHit.materialIndex = bestMat;
        }
        return found;
    }

    // Kernels.
    __global__ void RenderKernel1x(uint32_t* pixels, int width, int height, CameraData cam)
    {
        int x = blockIdx.x * blockDim.x + threadIdx.x;
        int y = blockIdx.y * blockDim.y + threadIdx.y;
        if (x >= width || y >= height) return;

        float su = 1.0f / float(width);
        float sv = 1.0f / float(height);
        float s = (x + 0.5f) * su;
        float t = (y + 0.5f) * sv;
        float3 dir = cam.lowerLeftCorner + cam.horizontal * s + cam.vertical * t - cam.origin;

        Ray ray; SetRay(ray, cam.origin, normalize(dir));
        ray.tmax = RenderConfig::RayTMax;

        float3 col;
        Hit hit;
        if (TracePrimary(ray, hit))
            col = ShadePrimary(ray, hit, RenderConfig::TraceDepthPrimary);
        else
            col = SkyColor(ray.dir);

        pixels[y * width + x] = PackRGBA8(col);
    }

    __global__ void RenderKernel4x(uint32_t* pixels, int width, int height, CameraData cam)
    {
        int x = blockIdx.x * blockDim.x + threadIdx.x;
        int y = blockIdx.y * blockDim.y + threadIdx.y;
        if (x >= width || y >= height) return;

        float su = 1.0f / float(width);
        float sv = 1.0f / float(height);
        float3 col = make_float3(0.0f, 0.0f, 0.0f);
        float offs[2] = { 0.25f, 0.75f };

        for (int iy = 0; iy < 2; ++iy) {
            for (int ix = 0; ix < 2; ++ix) {
                float s = (x + offs[ix]) * su;
                float t = (y + offs[iy]) * sv;
                float3 dir = cam.lowerLeftCorner + cam.horizontal * s + cam.vertical * t - cam.origin;

                Ray ray; SetRay(ray, cam.origin, normalize(dir));
                ray.tmax = RenderConfig::RayTMax;

                Hit hit;
                if (TracePrimary(ray, hit))
                    col += ShadePrimary(ray, hit, RenderConfig::TraceDepthPrimary);
                else
                    col += SkyColor(ray.dir);
            }
        }
        col *= 0.25f;
        pixels[y * width + x] = PackRGBA8(col);
    }

    // Generic N-sample kernel (1-16).
    __global__ void RenderKernelNx(uint32_t* pixels, int width, int height, CameraData cam, int spp)
    {
        int x = blockIdx.x * blockDim.x + threadIdx.x;
        int y = blockIdx.y * blockDim.y + threadIdx.y;
        if (x >= width || y >= height) return;

        float su = 1.0f / float(width);
        float sv = 1.0f / float(height);

        int n = int(ceilf(sqrtf(float(spp))));
        float invN = 1.0f / n;

        float3 col = make_float3(0.0f, 0.0f, 0.0f);

        for (int si = 0; si < spp; ++si) {
            int ix = si % n;
            int iy = si / n;
            float ox = (ix + 0.5f) * invN;
            float oy = (iy + 0.5f) * invN;
            float s = (x + ox) * su;
            float t = (y + oy) * sv;
            float3 dir = cam.lowerLeftCorner + cam.horizontal * s + cam.vertical * t - cam.origin;

            Ray ray; SetRay(ray, cam.origin, normalize(dir));
            ray.tmax = RenderConfig::RayTMax;

            Hit hit;
            if (TracePrimary(ray, hit))
                col += ShadePrimary(ray, hit, RenderConfig::TraceDepthPrimary);
            else
                col += SkyColor(ray.dir);
        }

        col *= (1.0f / spp);
        pixels[y * width + x] = PackRGBA8(col);
    }

}

void RayCuda::UploadScene(const CudaScene* scene) {
    cudaMemcpyToSymbol(g_cudaScene, scene, sizeof(CudaScene));
}

void RayCuda::UploadSceneAsync(const CudaScene* scene, cudaStream_t stream) {
    cudaMemcpyToSymbolAsync(g_cudaScene, scene, sizeof(CudaScene), 0, cudaMemcpyHostToDevice, stream);
}

void RayCuda::LaunchRenderKernel1x(uint32_t* d_pixels, int width, int height,
    const CameraData& cam, cudaStream_t stream) {
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    RayCuda::RenderKernel1x << <grid, block, 0, stream >> > (d_pixels, width, height, cam);
}

void RayCuda::LaunchRenderKernel4x(uint32_t* d_pixels, int width, int height,
    const CameraData& cam, cudaStream_t stream) {
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
    RayCuda::RenderKernel4x << <grid, block, 0, stream >> > (d_pixels, width, height, cam);
}

void RayCuda::LaunchRenderKernelNx(uint32_t* d_pixels, int width, int height,
    const CameraData& cam, int spp, cudaStream_t stream) {
    dim3 block(16, 16);
    dim3 grid((width + block.x - 1) / block.x,
        (height + block.y - 1) / block.y);
    RayCuda::RenderKernelNx << <grid, block, 0, stream >> > (d_pixels, width, height, cam, spp);
}
