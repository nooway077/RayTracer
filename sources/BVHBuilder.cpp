#include "stdafx.h"
#include "BVHBuilder.h"
#include "Scene.h"

static const int BUCKET_COUNT = 8;

static int PartitionByMidpoint(PrimitiveDesc* p, int start, int count, int axis, float split) {
    int i = start;
    int j = start + count - 1;
    while (i <= j) {
        while (i <= j && p[i].centroid[axis == 0 ? 0 : (axis == 1 ? 1 : 2)] < split) ++i;
        while (i <= j && p[j].centroid[axis == 0 ? 0 : (axis == 1 ? 1 : 2)] >= split) --j;
        if (i >= j) break;
        PrimitiveDesc tmp = p[i]; p[i] = p[j]; p[j] = tmp;
        ++i; --j;
    }
    return i - start;
}

static float GetCentroidAxis(const PrimitiveDesc& p, int axis) {
    if (axis == 0) return p.centroid.x;
    if (axis == 1) return p.centroid.y;
    return p.centroid.z;
}

static int PartitionByBucket(PrimitiveDesc* p, int start, int count, int axis, float bmin, float extent, int bestSplit) {
    int i = start, j = start + count - 1;
    while (i <= j) {
        int b;
        if (extent <= 0.0f) b = 0;
        else {
            b = int(float(BUCKET_COUNT) * ((GetCentroidAxis(p[i], axis) - bmin) / extent));
            if (b < 0) b = 0; if (b >= BUCKET_COUNT) b = BUCKET_COUNT - 1;
        }
        if (b <= bestSplit) { ++i; }
        else {
            while (j > i) {
                int bj;
                if (extent <= 0.0f) bj = 0;
                else {
                    bj = int(float(BUCKET_COUNT) * ((GetCentroidAxis(p[j], axis) - bmin) / extent));
                    if (bj < 0) bj = 0; if (bj >= BUCKET_COUNT) bj = BUCKET_COUNT - 1;
                }
                if (bj <= bestSplit) break;
                --j;
            }
            if (i >= j) break;
            PrimitiveDesc tmp = p[i]; p[i] = p[j]; p[j] = tmp;
            ++i; --j;
        }
    }
    return i - start;
}

static int BuildNode(Scene& s, int start, int count) {
    int nodeIdx = s.nodeCount++;
    BVHNode& node = s.nodes[nodeIdx];
    node.left = node.right = -1;
    node.primOffset = -1;
    node.primCount = 0;

    node.aabb = MakeEmptyAABB();
    for (int i = 0; i < count; i++) Expand(node.aabb, s.primitives[start + i].aabb);

    // Leaf if small.
    if (count <= 2) {
        node.primOffset = start;
        node.primCount = count;
        return nodeIdx;
    }

    // Choose split axis (longest).
    Vec3 extent = node.aabb.max - node.aabb.min;
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > (axis == 0 ? extent.x : extent.y)) axis = 2;

    // SAH bins.
    AABB buckets[BUCKET_COUNT];
    int counts[BUCKET_COUNT];
    for (int b = 0; b < BUCKET_COUNT; b++) { buckets[b] = MakeEmptyAABB(); counts[b] = 0; }

    float bmin = (axis == 0 ? node.aabb.min.x : (axis == 1 ? node.aabb.min.y : node.aabb.min.z));
    float ext = (axis == 0 ? extent.x : (axis == 1 ? extent.y : extent.z));

    for (int i = 0; i < count; i++) {
        int b;
        if (ext <= 0.0f) b = 0;
        else {
            b = int(float(BUCKET_COUNT) * ((GetCentroidAxis(s.primitives[start + i], axis) - bmin) / ext));
            if (b < 0) b = 0; if (b >= BUCKET_COUNT) b = BUCKET_COUNT - 1;
        }
        Expand(buckets[b], s.primitives[start + i].aabb);
        counts[b]++;
    }

    AABB leftBoxes[BUCKET_COUNT], rightBoxes[BUCKET_COUNT];
    int leftCounts[BUCKET_COUNT], rightCounts[BUCKET_COUNT];
    AABB acc = MakeEmptyAABB(); int accC = 0;
    for (int i = 0; i < BUCKET_COUNT; i++) { Expand(acc, buckets[i]); accC += counts[i]; leftBoxes[i] = acc; leftCounts[i] = accC; }
    acc = MakeEmptyAABB(); accC = 0;
    for (int i = BUCKET_COUNT - 1; i >= 0; i--) { Expand(acc, buckets[i]); accC += counts[i]; rightBoxes[i] = acc; rightCounts[i] = accC; }

    float parentArea = Area(node.aabb);
    float bestCost = 1e30f;
    int bestSplit = -1;
    for (int i = 0; i < BUCKET_COUNT - 1; i++) {
        if (leftCounts[i] == 0 || rightCounts[i + 1] == 0) continue;
        float cost = leftCounts[i] * Area(leftBoxes[i]) + rightCounts[i + 1] * Area(rightBoxes[i + 1]);
        if (cost < bestCost) { bestCost = cost; bestSplit = i; }
    }

    if (bestSplit == -1 || bestCost >= parentArea * float(count)) {
        // Fallback: median spatial split.
        float splitPos = (axis == 0 ? node.aabb.min.x : (axis == 1 ? node.aabb.min.y : node.aabb.min.z)) + ext * 0.5f;
        int mid = PartitionByMidpoint(s.primitives, start, count, axis, splitPos);
        if (mid == 0 || mid == count) mid = count / 2;
        node.left = BuildNode(s, start, mid);
        node.right = BuildNode(s, start + mid, count - mid);
        return nodeIdx;
    }

    int mid = PartitionByBucket(s.primitives, start, count, axis, bmin, ext, bestSplit);
    if (mid == 0 || mid == count) {
        node.primOffset = start;
        node.primCount = count;
        return nodeIdx;
    }
    node.left = BuildNode(s, start, mid);
    node.right = BuildNode(s, start + mid, count - mid);
    return nodeIdx;
}

void BuildBVH(Scene& s) {
    s.nodeCount = 0;
    BuildNode(s, 0, s.primCount);
}
