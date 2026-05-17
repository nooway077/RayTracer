#pragma once

#include "CPUTypes.h"

enum MaterialType {
    MAT_LAMBERTIAN = 0,
    MAT_METAL,
    MAT_DIELECTRIC,
    MAT_EMISSIVE
};

enum TextureType {
    TEX_NONE = 0,
    TEX_CHECKER,
    TEX_GRID
};

struct Material {
    MaterialType type;
    Vec3       albedo;
    float        ior;       // For dielectric.
    float        roughness; // 0 = mirror-smooth, 1 = diffuse (reserved)
    TextureType  texture;
    float        texScale;
};
