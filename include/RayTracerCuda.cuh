#pragma once

#include "CudaTypes.cuh"

#ifdef __CUDACC__
#include <cuda_runtime.h>
#define RAYCUDA_HOST __host__
#else
struct CUstream_st;
typedef CUstream_st* cudaStream_t;
struct CUevent_st;
typedef CUevent_st* cudaEvent_t;
#define RAYCUDA_HOST
#endif

namespace RayCuda {

    enum MaterialType { MAT_LAMBERTIAN = 0, MAT_METAL, MAT_DIELECTRIC, MAT_EMISSIVE };
    enum TextureType { TEX_NONE = 0, TEX_CHECKER, TEX_GRID };

    struct Material { MaterialType type; float3 albedo; float ior; float roughness; TextureType texture; float texScale; };
    struct Sphere { float3 center; float radius; int materialIndex; };
    struct Quad { float3 Q, u, v, normal; float d; int materialIndex; float3 w; float invDotW; };
    struct AABB { float3 min, max; };
    struct Ray { float3 origin, dir, invDir; float tmin, tmax; };
    struct PrimitiveDesc { int type, index, materialIndex; AABB aabb; float3 centroid; };
    struct BVHNode { AABB aabb; int left, right, primOffset, primCount; };
    struct Light { float3 position, color; float intensity; };

    struct CudaScene {
        static const int MAX_SPHERES = 8, MAX_QUADS = 32, MAX_MATS = 16;
        static const int MAX_PRIMS = MAX_SPHERES + MAX_QUADS, MAX_NODES = MAX_PRIMS * 2;
        Sphere spheres[MAX_SPHERES];   int sphereCount;
        Quad   quads[MAX_QUADS];       int quadCount;
        Material materials[MAX_MATS];  int materialCount;
        Light light; float lightRadius; int lightMaterialIndex;
        PrimitiveDesc primitives[MAX_PRIMS]; int primCount;
        BVHNode nodes[MAX_NODES]; int nodeCount;
    };

    struct Hit { float t; float3 pos, normal; int materialIndex; };
    struct CameraData { float3 origin, lowerLeftCorner, horizontal, vertical; };

    RAYCUDA_HOST void UploadScene(const CudaScene* scene);
    RAYCUDA_HOST void UploadSceneAsync(const CudaScene* scene, cudaStream_t stream);
    RAYCUDA_HOST void LaunchRenderKernel1x(uint32_t* d_pixels, int width, int height,
        const CameraData& cam, cudaStream_t stream);
    RAYCUDA_HOST void LaunchRenderKernel4x(uint32_t* d_pixels, int width, int height,
        const CameraData& cam, cudaStream_t stream);
    RAYCUDA_HOST void LaunchRenderKernelNx(uint32_t* d_pixels, int width, int height,
        const CameraData& cam, int spp, cudaStream_t stream);
}
