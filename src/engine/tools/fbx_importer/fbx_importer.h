#pragma once

#ifdef _MSC_VER
#define FBX_IMPORT_API extern "C" __declspec(dllexport)
#else
#define FBX_IMPORT_API extern "C"
#endif

#include <foundation/memory/memory.h>
#include <foundation/math/math.h>
#include <foundation/int_types.h>
#include <engine/runtime/renderer/renderer.h>

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
bool FBXImportAsset(fnd::memory::MemoryArenaBase* arena, char* fbxData, size_t fbxDataSize, renderer::MeshDesc* outMeshDescs, size_t* outNumSubmeshes);