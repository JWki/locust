#pragma once

#ifdef _MSC_VER
#define FBX_IMPORT_API extern "C" __declspec(dllexport)
#else
#define FBX_IMPORT_API extern "C"
#endif

#define FBX_IMPORTER_API_NAME "fbx_importer"

#include <foundation/memory/memory.h>
#include <foundation/math/math.h>
#include <foundation/int_types.h>
#include <engine/runtime/renderer/renderer.h>

namespace fbx_importer
{
    bool FBXImportAsset(fnd::memory::MemoryArenaBase* arena, char* fbxData, size_t fbxDataSize, renderer::MeshDesc* outMeshDescs, size_t* outNumSubmeshes);

    struct FBXImportInterface
    {
        decltype(FBXImportAsset)*   FBXImportAsset = nullptr;
    };
}

FBX_IMPORT_API bool fbx_importer_get_interface(fbx_importer::FBXImportInterface* outInterface);

