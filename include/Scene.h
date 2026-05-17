#pragma once

#include "BVH.h"
#include "Sphere.h"
#include "Quad.h"
#include "Material.h"
#include "Light.h"

struct Scene {
    static const int MAX_SPHERES = 8;
    static const int MAX_QUADS = 32;
    static const int MAX_MATS = 16;
    static const int MAX_PRIMS = MAX_SPHERES + MAX_QUADS;
    static const int MAX_NODES = MAX_PRIMS * 2;

    Sphere spheres[MAX_SPHERES];
    int sphereCount;

    Quad quads[MAX_QUADS];
    int quadCount;

    Material materials[MAX_MATS];
    int materialCount;

    Light light;
    float lightRadius;
    int   lightMaterialIndex; // Dynamic emissive sphere.

    PrimitiveDesc primitives[MAX_PRIMS];
    int primCount;

    BVHNode nodes[MAX_NODES];
    int nodeCount;

    void SetupStaticScene();
};
