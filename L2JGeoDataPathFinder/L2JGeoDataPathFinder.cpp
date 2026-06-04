// Stub implementation of L2JGeoDataPathFinder.dll
// Provides straight-line pathfinding without geodata.
// Returns 2 nodes (start + end) → 1 line segment + 1 dot at destination.

#include <stdlib.h>

#pragma pack(push, 4)
struct PathNode {
    unsigned long minX;
    unsigned long minY;
    unsigned long maxX;
    unsigned long maxY;
    short height;
};
#pragma pack(pop)

// float -> int -> unsigned long preserves negative coordinates.
static unsigned long f2ul(float v) {
    int i = (int)v;
    return (unsigned long)i;
}

extern "C" __declspec(dllexport) unsigned long __cdecl FindPath(
    PathNode** arrayPtr,
    const char* geoDataDirectory,
    float startX, float startY, float startZ,
    float endX, float endY,
    unsigned short maxPassableHeight)
{
    PathNode* nodes = (PathNode*)malloc(2 * sizeof(PathNode));
    if (!nodes) {
        *arrayPtr = 0;
        return 0;
    }

    nodes[0].minX = f2ul(startX);
    nodes[0].minY = f2ul(startY);
    nodes[0].maxX = f2ul(startX);
    nodes[0].maxY = f2ul(startY);
    nodes[0].height = (short)startZ;

    nodes[1].minX = f2ul(endX);
    nodes[1].minY = f2ul(endY);
    nodes[1].maxX = f2ul(endX);
    nodes[1].maxY = f2ul(endY);
    nodes[1].height = (short)startZ;

    *arrayPtr = nodes;
    return 2;
}

extern "C" __declspec(dllexport) unsigned long __cdecl ReleasePath(PathNode* arrayPtr)
{
    if (arrayPtr) {
        free(arrayPtr);
    }
    return 0;
}

extern "C" __declspec(dllexport) int __cdecl HasLineOfSight(
    const char* geoDataDirectory,
    float startX, float startY, float startZ,
    float endX, float endY,
    unsigned short maxPassableHeight)
{
    return 1;
}
