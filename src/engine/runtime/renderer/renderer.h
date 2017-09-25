#pragma once

#include <foundation/int_types.h>
#include <engine/runtime/gfx/gfx.h>
#include <foundation/math/math.h>

namespace fnd { namespace memory { class MemoryArenaBase; } }
namespace core {
    struct Asset { uint32_t id = 0; bool operator == (const Asset& other) { return other.id == id; } };
}

#define RENDERER_API_NAME "renderer"

struct ImDrawData;

namespace renderer
{
    struct Transform
    {
        uint32_t entityID = 0;
        float transform[16];
    };

    struct WorldSnapshot
    {
        uint32_t    numTransforms = 0;
        Transform*  transforms = nullptr;
    };

    struct DefaultVertex
    {
        fnd::math::float3   position;
        fnd::math::float3   normal;
        fnd::math::float2   uv;
        fnd::math::float3   tangent;
    };

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

    /** @NOTE: these only serialize/deserialize renderables, NOT RESOURCES */
    bool SerializeRenderWorld(RenderWorld* world, void* buffer, size_t bufferSize, size_t* requiredBufferSize);
    bool DeserializeRenderWorld(RenderWorld* world, void* buffer, size_t bufferSize, size_t* bytesRead);

    struct RendererConfig
    {
        gfx::Device* gfxDevice = nullptr;

        uint32_t    windowWidth = 0;
        uint32_t    windowHeight = 0;
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

        size_t numElements = 0;
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

    gfx::Image GetTextureHandle(RenderWorld* world, core::Asset assetID);

    StaticMesh CreateStaticMesh(RenderWorld* world, uint32_t entityID, core::Asset mesh, core::Asset* materials, size_t numMaterials);
    void DestroyStaticMesh(RenderWorld* world, StaticMesh mesh);

    StaticMesh GetStaticMesh(RenderWorld* world, uint32_t entityID);
    StaticMesh CopyStaticMesh(RenderWorld* world, uint32_t entityID, StaticMesh mesh);

    core::Asset GetMeshAsset(RenderWorld* world, StaticMesh mesh);
    void GetMaterials(RenderWorld* world, StaticMesh mesh, core::Asset* outMaterials, size_t* outNumMaterials);

    void Render(RenderWorld* world, gfx::SwapChain swapChain);
    void RenderUI(Renderer* renderer, ImDrawData* drawData);

    void SetCameraTransform(RenderWorld* world, float* transform);
    void SetCameraProjection(RenderWorld* world, float* transform);

    float* GetCameraTransform(RenderWorld* world);
    float* GetCameraProjection(RenderWorld* world);

    void UpdateWorldState(RenderWorld* world, WorldSnapshot* snapshot);

    size_t*     GetActiveCubemap(RenderWorld* world);

    struct RendererInterface
    {
        decltype(CreateRenderWorld)*        CreateRenderWorld = nullptr;
        decltype(DestroyRenderWorld)*       DestroyRenderWorld = nullptr;

        decltype(SerializeRenderWorld)*     SerializeRenderWorld = nullptr;
        decltype(DeserializeRenderWorld)*   DeserializeRenderWorld = nullptr;

        decltype(UpdateMeshLibrary)*        UpdateMeshLibrary = nullptr;
        decltype(UpdateTextureLibrary)*     UpdateTextureLibrary = nullptr;
        decltype(UpdateMaterialLibrary)*    UpdateMaterialLibrary = nullptr;
        decltype(CreateStaticMesh)*         CreateStaticMesh = nullptr;
        decltype(DestroyStaticMesh)*        DestroyStaticMesh = nullptr;
        decltype(GetStaticMesh)*            GetStaticMesh = nullptr;

        decltype(GetMeshAsset)*             GetMeshAsset = nullptr;
        decltype(GetMaterials)*             GetMaterials = nullptr;

        decltype(CopyStaticMesh)*           CopyStaticMesh = nullptr;

        decltype(Render)*                   Render = nullptr;
        decltype(RenderUI)*                 RenderUI = nullptr;

        decltype(GetTextureHandle)*         GetTextureHandle = nullptr;

        decltype(CreateRenderer)*           CreateRenderer = nullptr;
        decltype(DestroyRenderer)*          DestroyRenderer = nullptr;

        decltype(SetCameraTransform)*       SetCameraTransform = nullptr;
        decltype(SetCameraProjection)*      SetCameraProjection = nullptr;
        
        decltype(GetCameraTransform)*       GetCameraTransform = nullptr;
        decltype(GetCameraProjection)*      GetCameraProjection = nullptr;

        decltype(UpdateWorldState)*         UpdateWorldState = nullptr;
    
        decltype(GetActiveCubemap)*         GetActiveCubemap = nullptr;
    };
}

extern "C"
{
    __declspec(dllexport)
    bool renderer_get_interface(renderer::RendererInterface* outInterface);
}
