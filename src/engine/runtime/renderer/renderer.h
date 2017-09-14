#pragma once

#include <foundation/int_types.h>
#include <engine/runtime/gfx/gfx.h>

namespace fnd { namespace memory { class MemoryArenaBase; } }
namespace core {
    struct Asset { uint32_t id = 0; bool operator == (const Asset& other) { return other.id == id; } };
}

#define RENDERER_API_NAME "renderer"

namespace renderer
{
    struct MeshLibrary;
    struct TextureLibrary;
    struct MaterialLibrary;

    static const size_t DEFAULT_RENDERABLE_POOL_SIZE = 1024;
    static const size_t DEFAULT_LIBRARY_SIZE = 1024;

    struct Renderer;
    struct RenderWorld;

    struct RenderWorldConfig
    {
        size_t renderablePoolSize   = DEFAULT_RENDERABLE_POOL_SIZE;
        size_t meshLibrarySize      = DEFAULT_LIBRARY_SIZE;
        size_t textureLibrarySize   = DEFAULT_LIBRARY_SIZE;
        size_t materialLibrarySize  = DEFAULT_LIBRARY_SIZE;
    
        Renderer* renderer          = nullptr;
    };

    bool CreateRenderWorld(RenderWorld** outWorld, fnd::memory::MemoryArenaBase* memoryArena, RenderWorldConfig* config);
    void DestroyRenderWorld(RenderWorld* world);

    struct RendererConfig
    {
        gfx::Device* gfxDevice = nullptr;
    };

    bool CreateRenderer(Renderer** outRenderer, fnd::memory::MemoryArenaBase* memoryArena, RendererConfig* config);
    void DestroyRenderer(Renderer* renderer);

    // @TODO support non-interleaved vertex streams
    struct MeshDesc
    {
        gfx::VertexLayoutDesc vertexLayout;
        gfx::IndexFormat indexFormat;
        void*   vertexData      = nullptr;
        size_t  vertexDataSize  = 0;
        void* indexData         = nullptr;
        size_t indexDataSize    = 0;
    };

    struct TextureDesc
    {
        gfx::ImageDesc desc;
    };

    struct MaterialDesc
    {
        core::Asset baseColorMap;
        core::Asset roughnessMap;
        core::Asset metalnessMap;
        core::Asset normalVecMap;
        core::Asset occlusionMap;
    };

    static const uint32_t INVALID_ID = 0;

    typedef struct { uint32_t id; } StaticMesh;

    bool UpdateMeshLibrary(RenderWorld* world, core::Asset assetID, MeshDesc* meshDesc, size_t numSubmeshes);
    bool UpdateTextureLibrary(RenderWorld* world, core::Asset assetID, TextureDesc* textureDesc);
    bool UpdateMaterialLibrary(RenderWorld* world, core::Asset assetID, MaterialDesc* materialDesc);

    StaticMesh CreateStaticMesh(RenderWorld* world, uint32_t entityID, core::Asset mesh, core::Asset* materials, size_t numMaterials);
    void DestroyStaticMesh(RenderWorld* world, StaticMesh mesh);

    StaticMesh GetStaticMesh(RenderWorld* world, uint32_t entityID);

    void GetMaterials(RenderWorld* world, StaticMesh mesh, core::Asset* outMaterials, size_t* outNumMaterials);

    void Render(RenderWorld* world);

    void SetCameraTransform(RenderWorld* world, float* transform);
    void SetCameraProjection(RenderWorld* world, float* transform);

    struct RendererInterface
    {
        decltype(CreateRenderWorld)*        CreateRenderWorld = nullptr;
        decltype(DestroyRenderWorld)*       DestroyRenderWorld = nullptr;
        decltype(UpdateMeshLibrary)*        UpdateMeshLibrary = nullptr;
        decltype(UpdateTextureLibrary)*     UpdateTextureLibrary = nullptr;
        decltype(UpdateMaterialLibrary)*    UpdateMaterialLibrary = nullptr;
        decltype(CreateStaticMesh)*         CreateStaticMesh = nullptr;
        decltype(Render)*                   Render = nullptr;

        decltype(CreateRenderer)*           CreateRenderer = nullptr;
        decltype(DestroyRenderer)*          DestroyRenderer = nullptr;
    };
}

extern "C"
{
    __declspec(dllexport)
    bool renderer_get_interface(renderer::RendererInterface* outInterface);
}
