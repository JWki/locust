#include "renderer.h"
#include <foundation/memory/memory.h>
#include <foundation/logging/logging.h>
#include <cassert>
#include <string.h>

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef near
#undef far
#endif

#define HANDLE_INDEX(handle)        (uint16_t)(handle)
#define HANDLE_GENERATION(handle)   (uint16_t)(handle >> 16)

#define HANDLE_GENERATION_START 1

#define MAKE_HANDLE(index, generation) (uint32_t)(((uint32_t)generation) << 16 | index); 


void* LoadFileContents(const char* path, fnd::memory::MemoryArenaBase* memoryArena, size_t* fileSize = nullptr)
{
    HANDLE handle = CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (!handle) {
        GT_LOG_ERROR("FileSystem", "Failed to load %s\n", path);
        return nullptr;
    }
    DWORD size = GetFileSize(handle, NULL);
    void* buffer = memoryArena->Allocate(size, 16, GT_SOURCE_INFO);
    DWORD bytesRead = 0;
    auto res = ReadFile(handle, buffer, size, &bytesRead, NULL);
    if (res == FALSE || bytesRead != size) {
        GT_LOG_ERROR("FileSystem", "Failed to read %s\n", path);
        memoryArena->Free(buffer);
        return nullptr;
    }
    if (fileSize) { *fileSize = bytesRead; }
    CloseHandle(handle);
    return buffer;
}


namespace renderer
{
    template <class TResource>
    struct ResourcePool
    {
        uint32_t    size = 0;
        uint32_t    numElements = 0;
        TResource*  buffer = nullptr;
        uint16_t*   indexList = nullptr;
        uint32_t    indexListHead = 0;
        uint32_t    indexListTail = 0;

        void Initialize(uint32_t bufferSize, fnd::memory::MemoryArenaBase* memoryArena)
        {
            numElements = 0;
            size = bufferSize;
            buffer = GT_NEW_ARRAY(TResource, size, memoryArena);
            indexList = GT_NEW_ARRAY(uint16_t, size, memoryArena);
            indexListHead = indexListTail = 0;
            for (uint32_t i = 0; i < size; ++i) {
                indexList[i] = i;
            }
            indexListTail = size - 1;
        }

        bool GetNextIndex(uint16_t* outIndex)
        {
            if (indexListHead == indexListTail) { return false; }
            *outIndex = indexList[indexListHead];
            indexListHead = (indexListHead + 1) % size;
            return true;
        }

        void ReleaseIndex(uint16_t index)
        {
            indexListTail = (indexListTail + 1) % size;
            indexList[indexListTail] = index;
        }

        bool Allocate(TResource** resource, uint32_t* id)
        {
            uint16_t index = 0;
            if (!GetNextIndex(&index)) {
                return false;
            }
            TResource* res = &buffer[index];
            //res->resState = _ResourceState::STATE_ALLOC;
            *resource = res;
            *id = MAKE_HANDLE(index, res->generation);
            numElements++;
            return true;
        }

        void Free(uint32_t id)
        {
            uint16_t index = HANDLE_INDEX(id);
            TResource* res = &buffer[index];
            assert(res->generation == HANDLE_GENERATION(id));
            //D3D11ReleaseResource(res);

            res->generation++;
            //res->resState = _ResourceState::STATE_EMPTY;
            numElements--;
            ReleaseIndex(index);
        }

        TResource* Get(uint32_t id)
        {
            uint16_t index = HANDLE_INDEX(id);
            TResource* res = &buffer[index];
            if (res->generation != HANDLE_GENERATION(id)) { return nullptr; }
            //assert(res->generation == HANDLE_GENERATION(id));
            return res;
        }
    };


    struct MeshData
    {
        gfx::VertexLayoutDesc   vertexLayout;
        gfx::IndexFormat        indexFormat;

        uint32_t    numVertexBuffers;
        gfx::Buffer vertexBuffers[GFX_MAX_VERTEX_STREAMS];
        gfx::Buffer indexBuffer;

        MeshData*   nextSubmesh = nullptr;

        uint16_t    generation = HANDLE_GENERATION_START;
    };

    struct TextureData
    {
        uint16_t        generation = HANDLE_GENERATION_START;

        gfx::ImageDesc  desc;
        gfx::Image      image;
    };

    struct MaterialData
    {
        uint16_t        generation      = HANDLE_GENERATION_START;

        TextureData*    baseColorMap    = nullptr;
        TextureData*    roughnessMap    = nullptr;
        TextureData*    metalnessMap    = nullptr;
        TextureData*    normalVecMap    = nullptr;
        TextureData*    occlusionMap    = nullptr;
    };

    template <class TData>
    struct AssetToData
    {
        core::Asset key;
        TData*      data = nullptr;
    };

    struct MeshLibrary
    {
        ResourcePool<MeshData>  pool;
        AssetToData<MeshData>*  assetTable = nullptr;
        size_t                  assetTableSize = 0;
    };

    struct TextureLibrary
    {
        ResourcePool<TextureData>   pool;
        AssetToData<TextureData>*   assetTable = nullptr;
        size_t                      assetTableSize = 0;
    };

    struct MaterialLibrary
    {
        ResourcePool<MaterialData>  pool;
        AssetToData<MaterialData>*  assetTable = nullptr;
        size_t                      assetTableSize = 0;
    };

    template <class TLibrary, class TResource>
    TResource* LookupResource(TLibrary* lib, core::Asset key)
    {
        for (size_t i = 0; i < lib->assetTableSize; ++i) {
            if (lib->assetTable[i].key == key) {
                return lib->assetTable[i].data;
            }
        }
        return nullptr;
    }

    template <class TLibrary, class TResource>
    void InitializeResourceLibrary(TLibrary* lib, fnd::memory::MemoryArenaBase* memoryArena, size_t poolSize)
    {
        lib->pool.Initialize((uint32_t)poolSize, memoryArena);
        lib->assetTable = GT_NEW_ARRAY(AssetToData<TResource>, poolSize, memoryArena);
        lib->assetTableSize = poolSize;
    }

    template <class TLibrary, class TResource>
    AssetToData<TResource>* PushAsset(TLibrary* lib, core::Asset asset)
    {
        for (size_t i = 0; i < lib->assetTableSize; ++i) {
            if (lib->assetTable[i].key.id == 0) {
                lib->assetTable[i].key = asset;
                return &lib->assetTable[i];
            }
        }
        return nullptr;
    }

    static const size_t MAX_NUM_SUBMESHES = 32;  // @TODO @HACK
    struct StaticMeshRenderable
    {
        MeshData*       firstSubmesh;
        MaterialData*   materials[MAX_NUM_SUBMESHES];

        uint32_t        entityID = 0;
        StaticMesh      handle;
    };

    struct RenderableIndex
    {
        uint32_t    index = 0;
        uint16_t    generation = HANDLE_GENERATION_START;
    };

    struct RenderWorld
    {
        RenderWorldConfig   config;

        Renderer*           renderer;
      
        MeshLibrary     meshLibrary;
        TextureLibrary  textureLibrary;
        MaterialLibrary materialLibrary;
        
        ResourcePool<RenderableIndex>   staticMeshIndices;
        StaticMeshRenderable*           staticMeshes = nullptr;
        size_t                          firstFreeStaticMesh = 0;

        fnd::memory::MemoryArenaBase* creationArena = nullptr;
    };

    struct Renderer
    {
        gfx::Device*                    gfxDevice = nullptr;
        gfx::CommandBuffer              commandBuffer;
        fnd::memory::MemoryArenaBase*   creationArena = nullptr;
    };


    bool CreateRenderWorld(RenderWorld** outWorld, fnd::memory::MemoryArenaBase* memoryArena, RenderWorldConfig* config)
    {
        RenderWorld* world = GT_NEW(RenderWorld, memoryArena);
        
        InitializeResourceLibrary<MeshLibrary, MeshData>(&world->meshLibrary, memoryArena, config->meshLibrarySize);
        InitializeResourceLibrary<TextureLibrary, TextureData>(&world->textureLibrary, memoryArena, config->textureLibrarySize);
        InitializeResourceLibrary<MaterialLibrary, MaterialData>(&world->materialLibrary, memoryArena, config->materialLibrarySize);

        world->renderer = config->renderer;
        assert(world->renderer);
        world->staticMeshes = GT_NEW_ARRAY(StaticMeshRenderable, config->renderablePoolSize, memoryArena);
        world->staticMeshIndices.Initialize((uint32_t)config->renderablePoolSize, memoryArena);

        world->config = *config;
        world->creationArena = memoryArena;

        *outWorld = world;
        return true;
    }

    void DestroyRenderWorld(RenderWorld* world)
    {
        GT_DELETE(world, world->creationArena);
    }


    bool CreateRenderer(Renderer** outRenderer, fnd::memory::MemoryArenaBase* memoryArena, RendererConfig* config)
    {
        Renderer* renderer = GT_NEW(Renderer, memoryArena);

        renderer->creationArena = memoryArena;
        renderer->gfxDevice = config->gfxDevice;
        renderer->commandBuffer = gfx::GetImmediateCommandBuffer(renderer->gfxDevice);

        {   // load shaders
            size_t vCubeShaderCodeSize = 0;
            char* vCubeShaderCode = static_cast<char*>(LoadFileContents("VertexShaderCube.cso", &applicationArena, &vCubeShaderCodeSize));
            if (!vCubeShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
            }

            size_t pShaderCodeSize = 0;
            char* pShaderCode = static_cast<char*>(LoadFileContents("PixelShader.cso", &applicationArena, &pShaderCodeSize));
            if (!pShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t vBlitShaderCodeSize = 0;
            char* vBlitShaderCode = static_cast<char*>(LoadFileContents("BlitVertexShader.cso", &applicationArena, &vBlitShaderCodeSize));
            if (!vBlitShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
            }

            size_t pBlitShaderCodeSize = 0;
            char* pBlitShaderCode = static_cast<char*>(LoadFileContents("BlitPixelShader.cso", &applicationArena, &pBlitShaderCodeSize));
            if (!pBlitShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t pTonemapShaderCodeSize = 0;
            char* pTonemapShaderCode = static_cast<char*>(LoadFileContents("TonemapPixelShader.cso", &applicationArena, &pTonemapShaderCodeSize));
            if (!pTonemapShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t pBlurShaderCodeSize = 0;
            char* pBlurShaderCode = static_cast<char*>(LoadFileContents("SelectiveBlurPixelShader.cso", &applicationArena, &pBlurShaderCodeSize));
            if (!pBlurShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t vPaintShaderCodeSize = 0;
            char* vPaintShaderCode = static_cast<char*>(LoadFileContents("PaintVertexShader.cso", &applicationArena, &vPaintShaderCodeSize));
            if (!vPaintShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
            }

            size_t pPaintShaderCodeSize = 0;
            char* pPaintShaderCode = static_cast<char*>(LoadFileContents("PaintPixelShader.cso", &applicationArena, &pPaintShaderCodeSize));
            if (!pPaintShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load p shader\n");
            }


            size_t vCubemapShaderCodeSize = 0;
            char* vCubemapShaderCode = static_cast<char*>(LoadFileContents("CubemapVertexShader.cso", &applicationArena, &vCubemapShaderCodeSize));
            if (!vCubemapShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
            }

            size_t pCubemapShaderCodeSize = 0;
            char* pCubemapShaderCode = static_cast<char*>(LoadFileContents("CubemapPixelShader.cso", &applicationArena, &pCubemapShaderCodeSize));
            if (!pCubemapShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load p shader\n");
            }

            size_t pBRDFLUTShaderCodeSize = 0;
            char* pBRDFLUTShaderCode = static_cast<char*>(LoadFileContents("BRDFLUT.cso", &applicationArena, &pBRDFLUTShaderCodeSize));
            if (!pBRDFLUTShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load brdflut shader\n");
            }
        }

        *outRenderer = renderer;
        return true;
    }


    void DestroyRenderer(Renderer* renderer)
    {
        GT_DELETE(renderer, renderer->creationArena);
    }

    // @TODO these routines don't actually update existing entries

    bool UpdateMeshLibrary(RenderWorld* world, core::Asset assetID, MeshDesc* meshDesc, size_t numSubmeshes)
    {
        AssetToData<MeshData>* assetToData = PushAsset<MeshLibrary, MeshData>(&world->meshLibrary, assetID);
        
        MeshData*   first;
        uint32_t    id = 0;
        
        if (!world->meshLibrary.pool.Allocate(&first, &id)) { return false; }
        MeshData* it = first;
        for (size_t i = 1; i < numSubmeshes; ++i) {
            MeshData* next; 
            if (!world->meshLibrary.pool.Allocate(&next, &id)) { return false; }    // @TODO error handling
            it->nextSubmesh = next;
        }

        assetToData->data = first;
        
        it = first;
        for (size_t i = 0; i < numSubmeshes && it != nullptr; ++i) {
            auto desc = meshDesc[i];
            
            gfx::BufferDesc indexDesc;
            indexDesc.byteWidth = desc.indexDataSize;
            indexDesc.initialData = desc.indexData;
            indexDesc.initialDataSize = indexDesc.byteWidth;
            indexDesc.type = gfx::BufferType::BUFFER_TYPE_INDEX;
            indexDesc.usage = gfx::ResourceUsage::USAGE_IMMUTABLE;
            
            it->indexFormat = desc.indexFormat;
            it->indexBuffer = gfx::CreateBuffer(world->renderer->gfxDevice, &indexDesc);
            if (!GFX_CHECK_RESOURCE(it->indexBuffer)) {
                return false;
            }

            gfx::BufferDesc vertexDesc;
            vertexDesc.byteWidth = desc.vertexDataSize;
            vertexDesc.initialData = desc.vertexData;
            vertexDesc.initialDataSize = vertexDesc.byteWidth;
            vertexDesc.type = gfx::BufferType::BUFFER_TYPE_VERTEX;
            vertexDesc.usage = gfx::ResourceUsage::USAGE_IMMUTABLE;

            it->numVertexBuffers = 1;
            it->vertexLayout = desc.vertexLayout;
            it->vertexBuffers[0] = gfx::CreateBuffer(world->renderer->gfxDevice, &vertexDesc);
            if (!GFX_CHECK_RESOURCE(it->vertexBuffers[0])) {
                return false;
            }

            it = it->nextSubmesh;
        }

        return true;
    }


    bool UpdateTextureLibrary(RenderWorld* world, core::Asset assetID, TextureDesc* textureDesc)
    {
        AssetToData<TextureData>* assetToData = PushAsset<TextureLibrary, TextureData>(&world->textureLibrary, assetID);
        TextureData* texture;
        uint32_t id = 0;

        if (!world->textureLibrary.pool.Allocate(&texture, &id)) { return false; }
        texture->desc = textureDesc->desc;
        texture->image = gfx::CreateImage(world->renderer->gfxDevice, &texture->desc);
        if (!GFX_CHECK_RESOURCE(texture->image)) {
            return false;
        }
        assetToData->data = texture;

        return true;
    }

    bool UpdateMaterialLibrary(RenderWorld* world, core::Asset assetID, MaterialDesc* materialDesc)
    {
        AssetToData<MaterialData>* assetToData = PushAsset<MaterialLibrary, MaterialData>(&world->materialLibrary, assetID);

        MaterialData* material;
        uint32_t id = 0;
        if(!world->materialLibrary.pool.Allocate(&material, &id)) { return false; }

        material->baseColorMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->baseColorMap);
        material->roughnessMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->roughnessMap);
        material->metalnessMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->metalnessMap);
        material->normalVecMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->normalVecMap);
        material->occlusionMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->occlusionMap);

        assetToData->data = material;

        return true;
    }


    StaticMesh CreateStaticMesh(RenderWorld* world, uint32_t entityID, core::Asset mesh, core::Asset* materials, size_t numMaterials)
    {
        RenderableIndex* index;
        StaticMesh meshID;
        if (!world->staticMeshIndices.Allocate(&index, &meshID.id)) { return { INVALID_ID }; }
        
        index->index = (uint32_t)world->firstFreeStaticMesh++;
        if (index->index >= (uint32_t)world->config.renderablePoolSize) { return { INVALID_ID }; }

        StaticMeshRenderable* renderable = &world->staticMeshes[index->index];
        
        renderable->entityID = entityID;
        renderable->handle = meshID;
        renderable->firstSubmesh = LookupResource<MeshLibrary, MeshData>(&world->meshLibrary, mesh);
        numMaterials = numMaterials <= MAX_NUM_SUBMESHES ? numMaterials : MAX_NUM_SUBMESHES;
        for (size_t i = 0; i < numMaterials; ++i) {
            renderable->materials[i] = LookupResource<MaterialLibrary, MaterialData>(&world->materialLibrary, materials[i]);
        }

        return meshID;
    }

    void DestroyStaticMesh(RenderWorld* world, StaticMesh mesh)
    {
        RenderableIndex* index = world->staticMeshIndices.Get(mesh.id);
        assert(index->generation == HANDLE_GENERATION(mesh.id));
    
        uint32_t lastNonFree = (uint32_t)world->firstFreeStaticMesh - 1;
        StaticMeshRenderable* swap = &world->staticMeshes[lastNonFree];
        StaticMeshRenderable* target = &world->staticMeshes[index->index];
        RenderableIndex* swapIndex = world->staticMeshIndices.Get(swap->handle.id);
        swapIndex->index = index->index;

        memcpy(target, swap, sizeof(StaticMeshRenderable));
        
        world->staticMeshIndices.Free(mesh.id);
    }

    StaticMesh GetStaticMesh(RenderWorld* world, uint32_t entityID)
    {
        for (size_t i = 0; i < world->firstFreeStaticMesh; ++i) {
            if (entityID == world->staticMeshes[i].entityID) {
                return world->staticMeshes[i].handle;
            }
        }
        return { INVALID_ID };
    }

    void GetMaterials(RenderWorld* world, StaticMesh mesh, core::Asset* outMaterials, size_t* outNumMaterials)
    {
        
    }



    void Render(RenderWorld* world)
    {
        Renderer* renderer = world->renderer;
        

    }
}


bool renderer_get_interface(renderer::RendererInterface* outInterface)
{
    outInterface->CreateRenderWorld = &renderer::CreateRenderWorld;
    outInterface->DestroyRenderWorld = &renderer::DestroyRenderWorld;
    outInterface->UpdateMeshLibrary = &renderer::UpdateMeshLibrary;
    outInterface->UpdateTextureLibrary = &renderer::UpdateTextureLibrary;
    outInterface->UpdateMaterialLibrary = &renderer::UpdateMaterialLibrary;
    outInterface->CreateStaticMesh = &renderer::CreateStaticMesh;
    return true;
}