#include "stdafx.h"
#include "RayTracerCore.h"
#include "RenderConfig.h"

static inline float fresnel(float cosi, float eta)
{
    float r0 = (1.0f - eta) / (1.0f + eta);
    r0 = r0 * r0;
    return r0 + (1.0f - r0) * powf(1.0f - cosi, 5.0f);
}

static inline Vec3 EvaluateTexture(const Material& mat, const Vec3& pos)
{
    if (mat.texture == TEX_CHECKER) {
        int x = (int)floorf(pos.x * mat.texScale);
        int y = (int)floorf(pos.y * mat.texScale);
        int z = (int)floorf(pos.z * mat.texScale);
        bool odd = ((x + y + z) & 1) != 0;
        return odd ? make_vec3(0.9f, 0.9f, 0.9f) : mat.albedo;
    }
    if (mat.texture == TEX_GRID) {
        float sx = pos.x * mat.texScale;
        float sy = pos.y * mat.texScale;
        float sz = pos.z * mat.texScale;
        float bx = sx - floorf(sx);
        float by = sy - floorf(sy);
        float bz = sz - floorf(sz);
        bool line = (bx < 0.03f) || (by < 0.03f) || (bz < 0.03f);
        return line ? make_vec3(0.1f, 0.1f, 0.1f) : mat.albedo;
    }
    return mat.albedo;
}

static inline bool IntersectLightSphere(const Scene& s, const Ray& r, float& tHit, Vec3& outN)
{
    Vec3 oc = r.origin - s.light.position;
    float h = dot(oc, r.dir);
    float c = dot(oc, oc) - s.lightRadius * s.lightRadius;
    float disc = h * h - c;
    if (disc < 0.0f) return false;
    float sqrtd = sqrtf(disc);
    float root = -h - sqrtd;
    if (root < r.tmin || root > r.tmax) {
        root = -h + sqrtd;
        if (root < r.tmin || root > r.tmax) return false;
    }
    tHit = root;
    outN = (oc + r.dir * tHit) * (1.0f / s.lightRadius);
    return true;
}

static inline Vec3 DirectLightNoShadow(const Vec3& hitPos, const Vec3& N,
    const Vec3& V, const Material& mat, const Vec3& albedo, const Scene& scene)
{
    Vec3 color = make_vec3(0.0f, 0.0f, 0.0f);
    Vec3 toLight = scene.light.position - hitPos;
    float dist2 = dot(toLight, toLight);
    float dist = sqrtf(dist2);
    if (dist < RenderConfig::LightAttenClamp) dist = RenderConfig::LightAttenClamp;

    Vec3 L = toLight / dist;
    float NdotL = fmaxf_(dot(N, L), 0.0f);
    if (NdotL > 0.0f) {
        float att = scene.light.intensity / (dist * dist);
        if (mat.type == MAT_LAMBERTIAN) {
            color = color + albedo * scene.light.color * (NdotL * att);
        }
        else if (mat.type == MAT_METAL) {
            Vec3 H = normalize(L + V);
            float spec = powf(fmaxf_(dot(N, H), 0.0f), 32.0f) * NdotL;
            color = color + albedo * scene.light.color * (spec * att);
        }
    }
    return color;
}

Vec3 SkyColor(const Vec3& dir)
{
    float t = 0.5f * (dir.y + 1.0f);
    return lerp(make_vec3(1.0f, 1.0f, 1.0f), make_vec3(0.5f, 0.7f, 1.0f), t);
}

static bool SceneIntersectBVH(const Scene& s, const Ray& r, Hit& outHit)
{
    bool found = false;
    float closest = r.tmax;
    Vec3 bestN;
    int bestMat = -1;

    int stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int nidx = stack[--sp];
        const BVHNode& node = s.nodes[nidx];
        if (!IntersectAABB(r, node.aabb, r.tmin, closest)) continue;

        if (IsLeaf(node)) {
            for (int i = 0; i < node.primCount; ++i) {
                const PrimitiveDesc& p = s.primitives[node.primOffset + i];
                float t; Vec3 n;
                if (p.type == 0) {
                    if (IntersectSphere(r, s.spheres[p.index], t, n) && t < closest && t > r.tmin) {
                        closest = t; bestN = n; bestMat = p.materialIndex; found = true;
                    }
                }
                else {
                    if (IntersectQuad(r, s.quads[p.index], t, n) && t < closest && t > r.tmin) {
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

    float tL; Vec3 nL;
    if (IntersectLightSphere(s, r, tL, nL) && tL < closest) {
        closest = tL; bestN = nL; bestMat = s.lightMaterialIndex; found = true;
    }

    if (found) {
        outHit.t = closest;
        outHit.pos = r.origin + r.dir * closest;
        outHit.normal = bestN;
        outHit.materialIndex = bestMat;
    }
    return found;
}

static bool SceneAnyHitBVH(const Scene& s, const Ray& r)
{
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int nidx = stack[--sp];
        const BVHNode& node = s.nodes[nidx];
        if (!IntersectAABB(r, node.aabb, r.tmin, r.tmax)) continue;

        if (IsLeaf(node)) {
            for (int i = 0; i < node.primCount; ++i) {
                const PrimitiveDesc& p = s.primitives[node.primOffset + i];
                float t; Vec3 n;
                if (p.type == 0) {
                    if (IntersectSphere(r, s.spheres[p.index], t, n) && t < r.tmax && t > r.tmin) return true;
                }
                else {
                    if (IntersectQuad(r, s.quads[p.index], t, n) && t < r.tmax && t > r.tmin) return true;
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

static Vec3 SampleDielectric(const Ray& ray, const Vec3& hitPos, const Vec3& N,
    const Material& mat, const Scene& scene, int depth)
{
    bool frontFace = dot(ray.dir, N) < 0.0f;
    Vec3 outwardN = frontFace ? N : (-1.0f * N);
    float cosTheta = fminf_(dot(-ray.dir, outwardN), 1.0f);
    float eta = frontFace ? (1.0f / mat.ior) : mat.ior;

    Vec3 reflectDir = reflect(ray.dir, outwardN);
    Ray reflectRay; SetRay(reflectRay, hitPos + outwardN * RenderConfig::RayEpsilon, reflectDir);
    Vec3 reflectColor = TraceSecondary(reflectRay, scene, depth - 1);

    Vec3 refractDir = refract(ray.dir, outwardN, eta);
    if (refractDir.x == 0.0f && refractDir.y == 0.0f && refractDir.z == 0.0f) {
        return reflectColor;
    }
    Ray refractRay; SetRay(refractRay, hitPos - outwardN * RenderConfig::RayEpsilon, refractDir);
    Vec3 refractColor = TraceSecondary(refractRay, scene, depth - 1);

    float R = fresnel(cosTheta, eta);
    return reflectColor * R + refractColor * (1.0f - R);
}

Vec3 TraceSecondary(const Ray& ray, const Scene& scene, int depth)
{
    Hit hit;
    if (!SceneIntersectBVH(scene, ray, hit)) return SkyColor(ray.dir);

    if (hit.materialIndex == scene.lightMaterialIndex) {
        return scene.light.color * scene.light.intensity * 0.25f;
    }

    const Material& mat = scene.materials[hit.materialIndex];
    Vec3 N = hit.normal;
    Vec3 hitPos = hit.pos;
    Vec3 albedo = EvaluateTexture(mat, hitPos);

    if (mat.type == MAT_DIELECTRIC) {
        if (depth <= 0) return make_vec3(0.0f, 0.0f, 0.0f);
        return SampleDielectric(ray, hitPos, N, mat, scene, depth);
    }

    Vec3 ambient = SkyColor(N) * RenderConfig::AmbientSkyScale
        + make_vec3(RenderConfig::AmbientFillR, RenderConfig::AmbientFillG, RenderConfig::AmbientFillB);
    Vec3 color = ambient * albedo;

    float bounce = fmaxf_(dot(N, make_vec3(0.0f, -1.0f, 0.0f)), 0.0f);
    color = color + make_vec3(RenderConfig::FloorBounceR, RenderConfig::FloorBounceG, RenderConfig::FloorBounceB)
        * albedo * bounce * RenderConfig::FloorBounceStr;

    color = color + DirectLightNoShadow(hitPos, N, -ray.dir, mat, albedo, scene);

    if (depth > 0 && mat.type == MAT_METAL) {
        Vec3 refl = reflect(ray.dir, N);
        Ray rr; SetRay(rr, hitPos + N * RenderConfig::RayEpsilon, refl);
        Vec3 reflectColor = TraceSecondary(rr, scene, depth - 1);

        float cosTheta = fmaxf_(dot(-ray.dir, N), 0.0f);
        Vec3 F0 = albedo;
        Vec3 F = F0 + (make_vec3(1.0f, 1.0f, 1.0f) - F0) * powf(1.0f - cosTheta, 5.0f);

        color = color + F * reflectColor;
    }
    return color;
}

static Vec3 ShadePrimary(const Ray& ray, const Hit& hit, const Scene& scene, int depth)
{
    if (hit.materialIndex == scene.lightMaterialIndex) {
        return scene.light.color * scene.light.intensity * 0.25f;
    }

    const Material& mat = scene.materials[hit.materialIndex];
    Vec3 hitPos = hit.pos;
    Vec3 N = hit.normal;
    Vec3 V = -ray.dir;

    Vec3 albedo = EvaluateTexture(mat, hitPos);

    if (mat.type == MAT_DIELECTRIC) {
        if (depth <= 0) return make_vec3(0.0f, 0.0f, 0.0f);
        return SampleDielectric(ray, hitPos, N, mat, scene, depth);
    }

    Vec3 ambient = SkyColor(N) * RenderConfig::AmbientSkyScale
        + make_vec3(RenderConfig::AmbientFillR, RenderConfig::AmbientFillG, RenderConfig::AmbientFillB);
    Vec3 color = ambient * albedo;

    float bounce = fmaxf_(dot(N, make_vec3(0.0f, -1.0f, 0.0f)), 0.0f);
    color = color + make_vec3(RenderConfig::FloorBounceR, RenderConfig::FloorBounceG, RenderConfig::FloorBounceB)
        * albedo * bounce * RenderConfig::FloorBounceStr;

    Vec3 toLight = scene.light.position - hitPos;
    float dist = sqrtf(dot(toLight, toLight));

    Ray sray; SetRay(sray, hitPos + N * RenderConfig::RayEpsilon, toLight / dist);
    sray.tmax = dist - RenderConfig::RayEpsilon;
    bool inShadow = SceneAnyHitBVH(scene, sray);

    float NdotL = fmaxf_(dot(N, sray.dir), 0.0f);
    if (!inShadow && NdotL > 0.0f) {
        color = color + DirectLightNoShadow(hitPos, N, V, mat, albedo, scene);
    }

    if (depth > 0 && mat.type == MAT_METAL) {
        Vec3 refl = reflect(ray.dir, N);
        Ray rr; SetRay(rr, hitPos + N * RenderConfig::RayEpsilon, refl);
        Vec3 reflectColor = TraceSecondary(rr, scene, depth - 1);

        float cosTheta = fmaxf_(dot(V, N), 0.0f);
        Vec3 F0 = albedo;
        Vec3 F = F0 + (make_vec3(1.0f, 1.0f, 1.0f) - F0) * powf(1.0f - cosTheta, 5.0f);

        color = color + F * reflectColor;
    }

    return color;
}

void TracePacket4(Ray rays[4], const Scene& scene, Vec3 outColor[4])
{
    RayPacket4 packet = MakeRayPacket4(rays);

    int laneHitMask = 0;
    float laneT[4] = { RenderConfig::RayTMax, RenderConfig::RayTMax,
                       RenderConfig::RayTMax, RenderConfig::RayTMax };
    Vec3 laneN[4];
    int laneMat[4];

    int stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int nidx = stack[--sp];
        const BVHNode& node = scene.nodes[nidx];
        __m128 nodeHit = IntersectAABBPacket4(packet, node.aabb);
        int nodeMask = _mm_movemask_ps(nodeHit);
        if (nodeMask == 0) continue;

        if (IsLeaf(node)) {
            for (int pi = 0; pi < node.primCount; ++pi) {
                const PrimitiveDesc& p = scene.primitives[node.primOffset + pi];
                if (p.type == 0) {
                    const Sphere& sph = scene.spheres[p.index];
                    for (int lane = 0; lane < 4; ++lane) {
                        if (!(nodeMask & (1 << lane))) continue;
                        float t; Vec3 n;
                        if (IntersectSphere(rays[lane], sph, t, n) && t < laneT[lane]) {
                            laneT[lane] = t;
                            rays[lane].tmax = t;
                            laneN[lane] = n;
                            laneMat[lane] = p.materialIndex;
                            laneHitMask |= (1 << lane);
                        }
                    }
                }
                else {
                    const Quad& q = scene.quads[p.index];
                    for (int lane = 0; lane < 4; ++lane) {
                        if (!(nodeMask & (1 << lane))) continue;
                        float t; Vec3 n;
                        if (IntersectQuad(rays[lane], q, t, n) && t < laneT[lane]) {
                            laneT[lane] = t;
                            rays[lane].tmax = t;
                            laneN[lane] = n;
                            laneMat[lane] = p.materialIndex;
                            laneHitMask |= (1 << lane);
                        }
                    }
                }
            }
        }
        else {
            stack[sp++] = node.left;
            stack[sp++] = node.right;
        }
    }

    for (int lane = 0; lane < 4; ++lane) {
        float tL; Vec3 nL;
        if (IntersectLightSphere(scene, rays[lane], tL, nL)) {
            float bestT = (laneHitMask & (1 << lane)) ? laneT[lane] : RenderConfig::RayTMax;
            if (tL < bestT) {
                laneT[lane] = tL;
                laneN[lane] = nL;
                laneMat[lane] = scene.lightMaterialIndex;
                laneHitMask |= (1 << lane);
            }
        }
    }

    for (int lane = 0; lane < 4; ++lane) {
        if (laneHitMask & (1 << lane)) {
            Hit hit;
            hit.t = laneT[lane];
            hit.normal = laneN[lane];
            hit.materialIndex = laneMat[lane];
            hit.pos = rays[lane].origin + rays[lane].dir * hit.t;
            outColor[lane] = ShadePrimary(rays[lane], hit, scene, RenderConfig::TraceDepthPrimary);
        }
        else {
            outColor[lane] = SkyColor(rays[lane].dir);
        }
    }
}

Vec3 TracePrimaryRay(const Ray& ray, const Scene& scene)
{
    Hit hit;
    if (SceneIntersectBVH(scene, ray, hit))
        return ShadePrimary(ray, hit, scene, RenderConfig::TraceDepthPrimary);
    return SkyColor(ray.dir);
}
