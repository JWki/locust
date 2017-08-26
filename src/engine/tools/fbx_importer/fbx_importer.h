#pragma once

#ifdef _MSC_VER
#define FBX_IMPORT_API extern "C" __declspec(dllexport)
#else
#define FBX_IMPORT_API extern "C"
#endif

#include <foundation/memory/memory.h>
#include <foundation/math/math.h>
#include <foundation/int_types.h>

struct MeshAsset
{
    fnd::math::float3*   vertexPositions = nullptr;
    fnd::math::float3*   vertexNormals = nullptr;
    fnd::math::float3*   vertexTangents = nullptr;
    fnd::math::float2*   vertexUVs = nullptr;

    enum class IndexFormat : uint8_t {
        UINT16,
        UINT32
    } indexFormat = IndexFormat::UINT16;
    union {
        uint16_t* as_uint16;
        uint32_t* as_uint32;
    } indices;

    uint32_t        numVertices = 0;
    uint32_t        numIndices = 0;

    MeshAsset() { indices.as_uint16 = nullptr; }
};


struct FBXScene { void* _ptr = nullptr; };
struct FBXMesh { void* _ptr = nullptr; };
struct FBXMeshInfo
{
    size_t  numVertices     = 0;
    bool    hasNormals      = false;
    bool    hasTexcoords    = false;
    bool    hasTangents     = false;
};

FBX_IMPORT_API
bool FBXImportAsset(fnd::memory::MemoryArenaBase* arena, char* fbxData, size_t fbxDataSize, MeshAsset* outAsset);