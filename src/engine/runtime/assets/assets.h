#pragma once

#include <foundation/int_types.h>
#include <engine/runtime/gfx/gfx.h>

namespace fnd {
    namespace memory {
        class MemoryArenaBase;
    }
}

#define ASSETS_DEFAULT_LIBRARY_CAPACITY 1024

namespace assets
{
    
    enum class AssetType : uint16_t
    {
        STATIC_MESH,
        MATERIAL,
        TEXTURE
    };


    struct StaticMeshAsset
    {
        gfx::VertexLayoutDesc   vertexLayout;
        gfx::IndexFormat        indexFormat;
        void*                   vertexBuffer;
        void*                   indexBuffer;
    };

    struct MaterialAsset
    {

    };

    struct TextureAsset
    {

    };

    struct Asset
    {
        AssetType type;

        union {
            StaticMeshAsset staticMesh;
            MaterialAsset material;
            TextureAsset texture;
        };
    };


    struct AssetLibrary;
    struct AssetLibraryConfig
    {
        uint32_t maxNumAssets = ASSETS_DEFAULT_LIBRARY_CAPACITY;
    };

    bool CreateAssetLibrary(AssetLibrary** outLib, fnd::memory::MemoryArenaBase* memoryArena, AssetLibraryConfig* config);
    void DestroyAssetLibrary(AssetLibrary* lib);

    
}