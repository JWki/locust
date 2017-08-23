#pragma once

#ifdef _MSC_VER
#define FBX_IMPORT_API extern "C" __declspec(dllexport)
#else
#define FBX_IMPORT_API extern "C"
#endif

#include <foundation/int_types.h>


struct FBXScene { void* _ptr = nullptr; };
struct FBXMesh { void* _ptr = nullptr; };
struct FBXMeshInfo
{
    size_t  numVertices     = 0;
    bool    hasNormals      = false;
    bool    hasTexcoords    = false;
};

FBX_IMPORT_API
FBXScene FBXLoadSceneFromMemory(void* data, size_t dataSize);

FBX_IMPORT_API
FBXMesh FBXGetMeshWithIndex(FBXScene scene, int index);

FBX_IMPORT_API
size_t FBXGetMeshCount(FBXScene scene);

FBX_IMPORT_API
bool FBXGetMeshInfo(FBXMesh mesh, FBXMeshInfo* outInfo);

FBX_IMPORT_API
bool FBXGetNormals(FBXMesh mesh, float* buffer, size_t bufferCapacity);

FBX_IMPORT_API
bool FBXGetTexcoords(FBXMesh mesh, float* buffer, size_t bufferCapacity);

FBX_IMPORT_API
bool FBXGetVertexPositions(FBXMesh mesh, float* buffer, size_t bufferCapacity);
