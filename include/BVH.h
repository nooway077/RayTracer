#pragma once

#include "AABB.h"

// Generic reference used by the builder & traversal.
struct PrimitiveDesc {
    int type;            // 0 = sphere, 1 = quad
    int index;           // Into scene.spheres / scene.quads.
    int materialIndex;
    AABB aabb;
    Vec3 centroid;
};

struct BVHNode {
    AABB aabb;
    int left;        // Child node index (internal).
    int right;       // Child node index (internal).
    int primOffset;  // Valid if leaf.
    int primCount;   // >0 => leaf
};

inline bool IsLeaf(const BVHNode& n) { return n.primCount > 0; }
