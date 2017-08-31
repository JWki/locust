#include "assets.h"
#include <foundation/memory/memory.h>

namespace assets
{
    struct AssetLibrary
    {
        fnd::memory::MemoryArenaBase* memoryArena = nullptr;
    
        Asset* assets = nullptr;
    };



    bool CreateAssetLibrary(AssetLibrary** outLib, fnd::memory::MemoryArenaBase* memoryArena, AssetLibraryConfig* config)
    {
        AssetLibrary* lib = GT_NEW(AssetLibrary, memoryArena);
        lib->memoryArena = memoryArena;
        lib->assets = GT_NEW_ARRAY_WITH_INITIALIZER(Asset, config->maxNumAssets, memoryArena, [](Asset* asset) {
            return;
        });

        *outLib = lib;
        return true;
    }


    void DestroyAssetLibrary(AssetLibrary* lib)
    {
        GT_DELETE_ARRAY(lib->assets, lib->memoryArena);
        GT_DELETE(lib, lib->memoryArena);
    }
}