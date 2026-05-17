#include "stdafx.h"
#include "Scene.h"
#include "BVHBuilder.h"
#include "RenderConfig.h"

static void AddQuad(Scene& s, const Vec3& Q, const Vec3& u, const Vec3& v, int materialIndex)
{
    Quad& q = s.quads[s.quadCount];
    q.Q = Q;
    q.u = u;
    q.v = v;
    q.materialIndex = materialIndex;
    InitQuad(q);
    ++s.quadCount;
}

static void AddBox(Scene& s, const Vec3& minP, const Vec3& maxP, int materialIndex)
{
    Vec3 sz = maxP - minP;
    AddQuad(s, make_vec3(minP.x, minP.y, maxP.z), make_vec3(0, sz.y, 0), make_vec3(0, 0, -sz.z), materialIndex);
    AddQuad(s, make_vec3(maxP.x, minP.y, minP.z), make_vec3(0, sz.y, 0), make_vec3(0, 0, sz.z), materialIndex);
    AddQuad(s, minP, make_vec3(sz.x, 0, 0), make_vec3(0, 0, sz.z), materialIndex);
    AddQuad(s, make_vec3(minP.x, maxP.y, maxP.z), make_vec3(sz.x, 0, 0), make_vec3(0, 0, -sz.z), materialIndex);
    AddQuad(s, make_vec3(maxP.x, minP.y, minP.z), make_vec3(-sz.x, 0, 0), make_vec3(0, sz.y, 0), materialIndex);
    AddQuad(s, make_vec3(minP.x, minP.y, maxP.z), make_vec3(sz.x, 0, 0), make_vec3(0, sz.y, 0), materialIndex);
}

void Scene::SetupStaticScene()
{
    sphereCount = 0;
    quadCount = 0;
    materialCount = 0;
    primCount = 0;

    //                             Type            Albedo                          IOR   Rough Tex          Scale
    materials[materialCount++] = { MAT_LAMBERTIAN, make_vec3(0.85f, 0.20f, 0.20f), 0.0f, 0.0f, TEX_NONE,    0.0f };
    materials[materialCount++] = { MAT_METAL,      make_vec3(0.95f, 0.95f, 0.95f), 0.0f, 0.0f, TEX_NONE,    0.0f };
    materials[materialCount++] = { MAT_DIELECTRIC, make_vec3(1.00f, 1.00f, 1.00f), 1.5f, 0.0f, TEX_NONE,    0.0f };
    materials[materialCount++] = { MAT_LAMBERTIAN, make_vec3(0.90f, 0.90f, 0.90f), 0.0f, 0.0f, TEX_NONE,    0.0f };
    materials[materialCount++] = { MAT_LAMBERTIAN, make_vec3(0.25f, 0.45f, 0.85f), 0.0f, 0.0f, TEX_NONE,    0.0f };
    materials[materialCount++] = { MAT_LAMBERTIAN, make_vec3(0.95f, 0.80f, 0.20f), 0.0f, 0.0f, TEX_NONE,    0.0f };
    lightMaterialIndex = materialCount;
    materials[materialCount++] = { MAT_EMISSIVE,   make_vec3(1.00f, 0.95f, 0.80f), 0.0f, 0.0f, TEX_NONE,    0.0f };
    materials[materialCount++] = { MAT_LAMBERTIAN, make_vec3(0.65f, 0.50f, 0.25f), 0.0f, 0.0f, TEX_CHECKER, 2.0f };
    materials[materialCount++] = { MAT_LAMBERTIAN, make_vec3(0.60f, 0.20f, 0.60f), 0.0f, 0.0f, TEX_NONE,    0.0f };
    materials[materialCount++] = { MAT_LAMBERTIAN, make_vec3(0.95f, 0.80f, 0.20f), 0.0f, 0.0f, TEX_GRID,    2.0f };

    lightRadius = RenderConfig::LightRadius;

    // Derived room dimensions.
    float roomW = RenderConfig::RoomMaxX - RenderConfig::RoomMinX;
    float roomH = RenderConfig::RoomMaxY - RenderConfig::RoomMinY;
    float roomD = RenderConfig::RoomMaxZ - RenderConfig::RoomMinZ;

    // Floor (+Y).
    AddQuad(*this,
        make_vec3(RenderConfig::RoomMinX, RenderConfig::RoomMinY, RenderConfig::RoomMaxZ),
        make_vec3(roomW, 0.0f, 0.0f),
        make_vec3(0.0f, 0.0f, -roomD),
        3);
    // Ceiling (-Y).
    AddQuad(*this,
        make_vec3(RenderConfig::RoomMinX, RenderConfig::RoomMaxY, RenderConfig::RoomMinZ),
        make_vec3(roomW, 0.0f, 0.0f),
        make_vec3(0.0f, 0.0f, roomD),
        3);
    // Back wall (+Z).
    AddQuad(*this,
        make_vec3(RenderConfig::RoomMinX, RenderConfig::RoomMinY, RenderConfig::RoomMinZ),
        make_vec3(roomW, 0.0f, 0.0f),
        make_vec3(0.0f, roomH, 0.0f),
        3);
    // Left wall (+X).
    AddQuad(*this,
        make_vec3(RenderConfig::RoomMinX, RenderConfig::RoomMinY, RenderConfig::RoomMinZ),
        make_vec3(0.0f, roomH, 0.0f),
        make_vec3(0.0f, 0.0f, roomD),
        3);
    // Right wall (-X).
    AddQuad(*this,
        make_vec3(RenderConfig::RoomMaxX, RenderConfig::RoomMinY, RenderConfig::RoomMinZ),
        make_vec3(0.0f, 0.0f, roomD),
        make_vec3(0.0f, roomH, 0.0f),
        3);
    // Front wall (-Z).
    AddQuad(*this,
        make_vec3(RenderConfig::RoomMaxX, RenderConfig::RoomMinY, RenderConfig::RoomMaxZ),
        make_vec3(-roomW, 0.0f, 0.0f),
        make_vec3(0.0f, roomH, 0.0f),
        3);

    // Spheres.
    spheres[sphereCount++] = { make_vec3(-2.5f, 0.6f, -1.5f), 0.6f, 7 };
    spheres[sphereCount++] = { make_vec3(-0.8f, 0.5f, -0.5f), 0.5f, 0 };
    spheres[sphereCount++] = { make_vec3(0.0f, 0.5f, -2.5f), 0.5f, 1 };
    spheres[sphereCount++] = { make_vec3(1.5f, 0.5f, -0.5f), 0.5f, 2 };
    spheres[sphereCount++] = { make_vec3(2.8f, 0.4f, -1.5f), 0.4f, 8 };

    // Cubes.
    AddBox(*this, make_vec3(-2.2f, 0.0f, -4.0f), make_vec3(-1.2f, 0.8f, -3.0f), 4);
    AddBox(*this, make_vec3(1.8f, 0.0f, -4.0f), make_vec3(2.8f, 0.6f, -3.0f), 9);

    // Build the BVH.
    for (int i = 0; i < sphereCount; ++i) {
        PrimitiveDesc& pd = primitives[primCount++];
        pd.type = 0; pd.index = i;
        pd.materialIndex = spheres[i].materialIndex;
        pd.aabb = GetAABB(spheres[i]);
        pd.centroid = spheres[i].center;
    }
    for (int i = 0; i < quadCount; ++i) {
        PrimitiveDesc& pd = primitives[primCount++];
        pd.type = 1; pd.index = i;
        pd.materialIndex = quads[i].materialIndex;
        pd.aabb = GetAABB(quads[i]);
        pd.centroid.x = (pd.aabb.min.x + pd.aabb.max.x) * 0.5f;
        pd.centroid.y = (pd.aabb.min.y + pd.aabb.max.y) * 0.5f;
        pd.centroid.z = (pd.aabb.min.z + pd.aabb.max.z) * 0.5f;
    }

    BuildBVH(*this);
}
