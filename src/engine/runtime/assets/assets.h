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
    typedef uint16_t AssetType;
    typedef uint64_t AssetID;

    enum class NativeAssetType : AssetType
    {
        STATIC_MESH = 0,
        MATERIAL,
        TEXTURE,
        _LAST = TEXTURE
    };
    
    struct Asset
    {

    };

    struct AssetRef
    {
        AssetID     ID;
    };

    struct AssetLibrary;
    struct AssetLibraryConfig
    {
        uint32_t maxNumAssets = ASSETS_DEFAULT_LIBRARY_CAPACITY;
    };

    bool CreateAssetLibrary(AssetLibrary** outLib, fnd::memory::MemoryArenaBase* memoryArena, AssetLibraryConfig* config);
    void DestroyAssetLibrary(AssetLibrary* lib);

    
}