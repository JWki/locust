#include "renderer.h"
#include <foundation/memory/memory.h>
#include <foundation/logging/logging.h>
#include <foundation/math/math.h>
#include <cassert>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <engine/runtime/ImGui/imgui.h>
#include <engine/runtime/win32/imgui_impl_dx11.h>
#include <math.h>

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef near
#undef far
#endif

// @NOTE include AFTER windows.h because of ARRAYSIZE 
#pragma warning(push, 0)    // lots of warnings in here  
#define PAR_SHAPES_IMPLEMENTATION
#include <engine/runtime/par_shapes-h.h>
#pragma warning(pop)



#define HANDLE_INDEX(handle)        (uint16_t)(handle)
#define HANDLE_GENERATION(handle)   (uint16_t)(handle >> 16)

#define HANDLE_GENERATION_START 1

#define MAKE_HANDLE(index, generation) (uint32_t)(((uint32_t)generation) << 16 | index); 


static void* LoadFileContents(const char* path, fnd::memory::MemoryArenaBase* memoryArena, size_t* fileSize = nullptr)
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

        uint32_t    numVertexBuffers = 0;
        gfx::Buffer vertexBuffers[GFX_MAX_VERTEX_STREAMS];
        gfx::Buffer indexBuffer;

        uint32_t    numElements = 0;

        core::Asset asset;

        MeshData*   nextSubmesh = nullptr;

        uint16_t    generation = HANDLE_GENERATION_START;
    };

    struct TextureData
    {
        uint16_t        generation = HANDLE_GENERATION_START;

        core::Asset     asset;

        gfx::ImageDesc  desc;
        gfx::Image      image;
    };

    struct MaterialData
    {
        uint16_t        generation      = HANDLE_GENERATION_START;

        core::Asset     asset;

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

    static const size_t MAX_NUM_SUBMESHES = 1024;  // @TODO @HACK
    struct StaticMeshRenderable
    {
        float           transform[16];

        MeshData*       firstSubmesh;
        MaterialData*   materials[MAX_NUM_SUBMESHES];

        uint32_t        entityID = 0;
        StaticMesh      handle;

        core::Asset     meshAssetHandle;
        core::Asset     materialAssetHandles[MAX_NUM_SUBMESHES];
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

        float           cameraTransform[16];
        float           cameraProjection[16];

        fnd::memory::MemoryArenaBase* creationArena = nullptr;
    };

    struct Renderer
    {
        gfx::Device*                    gfxDevice = nullptr;
        gfx::CommandBuffer              commandBuffer;
        fnd::memory::MemoryArenaBase*   creationArena = nullptr;

        gfx::Shader vCubeShader;
        gfx::Shader pShader;
        gfx::Shader vBlitShader;
        gfx::Shader pBlitShader;
        gfx::Shader pTonemapShader;
        gfx::Shader pBlurShader;
        gfx::Shader pBloomShader;
        gfx::Shader pLuminanceShader;
        gfx::Shader pBRDFLUTShader;
        gfx::Shader vCubemapShader;
        gfx::Shader pCubemapShader;
        gfx::Shader vPrefilterCubemapShader;
        gfx::Shader pPrefilterCubemapShader;

        gfx::PipelineState meshPipeline_16bit;
        gfx::PipelineState meshPipeline_32bit; 
        gfx::PipelineState cubemapPipeline;
        gfx::PipelineState blitPipeline;
        gfx::PipelineState tonemapPipeline;
        gfx::PipelineState blurPipeline;
        gfx::PipelineState bloomPipeline;
        gfx::PipelineState luminancePipeline;
        gfx::PipelineState brdfLUTPipeline;
        gfx::PipelineState prefilterPipeline;

        gfx::Image uiRenderTarget;
        gfx::Image mainRT;
        gfx::Image luminanceRT;
        gfx::Image bloomRT;
        gfx::Image mainDepthBuffer;
        gfx::Image brdfLUT;
        static const size_t NUM_CONVOLUTION_MIPS = 11;
        static const size_t NUM_CUBEMAPS = 1;
        size_t activeCubemap = 0;
        gfx::Image hdrCubemap[NUM_CUBEMAPS];
        gfx::Image hdrDiffuse[NUM_CUBEMAPS];

        gfx::Image prefilteredCubemap[NUM_CUBEMAPS];
        fnd::math::float2 prefilteredCubemapDimensions[NUM_CUBEMAPS];

        gfx::RenderPass mainPass;
        gfx::RenderPass uiPass;
        gfx::RenderPass luminancePass;
        gfx::RenderPass bloomPass;
        gfx::RenderPass bloomBlitPass;
        gfx::RenderPass brdfLUTPass;
       
        gfx::Buffer cubeVertexBuffer;
        gfx::Buffer cubeIndexBuffer;
        gfx::Buffer cBuffer;
        gfx::Buffer prefilterCBuffer;
    };


    struct ConstantData {
        float MVP[16];
        float MV[16];
        float VP[16];
        float view[16];
        float inverseView[16];
        float projection[16];
        float model[16];
        fnd::math::float4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        fnd::math::float4 lightDir = { 1.0f, -1.0f, 1.0f, 0.0f };
        float metallic = 0.0f;
        float roughness = 1.0f;
        uint32_t  useTextures = 1;
        float _padding0[1];
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

        util::Make4x4FloatMatrixIdentity(world->cameraTransform);
        util::Make4x4FloatMatrixIdentity(world->cameraProjection);

        *outWorld = world;
        return true;
    }

    void DestroyRenderWorld(RenderWorld* world)
    {
        GT_DELETE(world, world->creationArena);
    }



    bool SerializeRenderWorld(RenderWorld* world, void* buffer, size_t bufferSize, size_t* requiredBufferSize)
    {
        size_t requiredSize = world->config.renderablePoolSize * (sizeof(StaticMeshRenderable)) + world->staticMeshIndices.size * (sizeof(RenderableIndex) + sizeof(uint16_t)) + sizeof(ResourcePool<RenderableIndex>);
        if (requiredBufferSize != nullptr) {
            *requiredBufferSize = requiredSize;
        }
        if (buffer != nullptr) {
            if (bufferSize < requiredSize) { return false; }
            union {
                void* as_void;
                StaticMeshRenderable* as_static_renderable;
                ResourcePool<RenderableIndex>* as_pool;
                RenderableIndex* as_index;
                uint16_t* as_uint16_t;
                uint32_t* as_uint32_t;
                uint64_t* as_uint64_t;
            };

            /**
            Memory layout on disk:
            {
                uint64_t                                <- size in bytes
                ResourcePool                            <-
                RenderableIndex[resource pool size]     <- index table into renderables
                uint16_t[resource pool size]            <- index table into renderable index table (yeah)
                uint32_t                                <- numRenderables
                StaticMeshRenderable[num renderables]   <- 
            */

            as_void = buffer;

            uint64_t requiredSizeU64 = requiredSize;
            memcpy(as_uint64_t, &requiredSizeU64, sizeof(uint64_t));
            as_uint64_t++;
            memcpy(as_pool, &world->staticMeshIndices, sizeof(ResourcePool<RenderableIndex>));
            as_pool++;
            memcpy(as_index, world->staticMeshIndices.buffer, sizeof(RenderableIndex) * world->staticMeshIndices.size);
            as_index += world->staticMeshIndices.size;
            memcpy(as_uint16_t, world->staticMeshIndices.indexList, sizeof(uint16_t) * world->staticMeshIndices.size);
            as_uint16_t += world->staticMeshIndices.size;
            uint32_t numRenderables = (uint32_t)world->firstFreeStaticMesh;
            memcpy(as_uint32_t, &numRenderables, sizeof(uint32_t));
            as_uint32_t++;
            memcpy(as_static_renderable, world->staticMeshes, sizeof(StaticMeshRenderable) * numRenderables);
        }
        return true;
    }

    bool DeserializeRenderWorld(RenderWorld* world, void* buffer, size_t bufferSize, size_t* bytesRead)
    {
        union {
            void* as_void;
            StaticMeshRenderable* as_static_renderable;
            ResourcePool<RenderableIndex>* as_pool;
            RenderableIndex* as_index;
            uint16_t* as_uint16_t;
            uint32_t* as_uint32_t;
            uint64_t* as_uint64_t;
        };
        as_void = buffer;

        uint64_t bytesReadU64 = 0;
        memcpy(&bytesReadU64, as_uint64_t, sizeof(uint64_t));
        as_uint64_t++;

        if (bytesRead != nullptr) {
            *bytesRead = (size_t)bytesReadU64;
        }

        if (world->staticMeshIndices.buffer != nullptr) {
            GT_DELETE_ARRAY(world->staticMeshIndices.buffer, world->creationArena);
            GT_DELETE_ARRAY(world->staticMeshIndices.indexList, world->creationArena);
        }
        memcpy(&world->staticMeshIndices, as_pool, sizeof(ResourcePool<RenderableIndex>));
        as_pool++;

        world->staticMeshIndices.buffer = GT_NEW_ARRAY(RenderableIndex, world->staticMeshIndices.size, world->creationArena);
        world->staticMeshIndices.indexList = GT_NEW_ARRAY(uint16_t, world->staticMeshIndices.size, world->creationArena);

        memcpy(world->staticMeshIndices.buffer, as_index, sizeof(RenderableIndex) * world->staticMeshIndices.size);
        as_index += world->staticMeshIndices.size;
        memcpy(world->staticMeshIndices.indexList, as_uint16_t, sizeof(uint16_t) * world->staticMeshIndices.size);
        as_uint16_t += world->staticMeshIndices.size;
        
        uint32_t numRenderables = 0;
        memcpy(&numRenderables, as_uint32_t, sizeof(uint32_t));
        as_uint32_t++;

        world->firstFreeStaticMesh = numRenderables;
        if (world->staticMeshes != nullptr) {
            GT_DELETE_ARRAY(world->staticMeshes, world->creationArena);
        }
        world->staticMeshes = GT_NEW_ARRAY(StaticMeshRenderable, world->staticMeshIndices.size, world->creationArena);
        memcpy(world->staticMeshes, as_static_renderable, sizeof(StaticMeshRenderable) * world->firstFreeStaticMesh);
        for (auto i = 0; i < world->firstFreeStaticMesh; ++i) {
            {   // patch resource pointers
                auto renderable = &world->staticMeshes[i];
                renderable->firstSubmesh = LookupResource<MeshLibrary, MeshData>(&world->meshLibrary, renderable->meshAssetHandle);
                for (size_t i = 0; i < MAX_NUM_SUBMESHES; ++i) {
                    renderable->materials[i] = LookupResource<MaterialLibrary, MaterialData>(&world->materialLibrary, renderable->materialAssetHandles[i]);
                }
            }
        }

        return true;
    }



    void SetCameraTransform(RenderWorld* world, float* transform)
    {
        util::Copy4x4FloatMatrixCM(transform, world->cameraTransform);
    }

    void SetCameraProjection(RenderWorld* world, float* transform)
    {
        util::Copy4x4FloatMatrixCM(transform, world->cameraProjection);
    }

    float* GetCameraTransform(RenderWorld* world)
    {
        return world->cameraTransform;
    }

    float* GetCameraProjection(RenderWorld* world)
    {
        return world->cameraProjection;
    }
   
    // @TODO this is SLOW AS FUCK
    void UpdateWorldState(RenderWorld* world, WorldSnapshot* snapshot)
    {
        for (size_t i = 0; i < snapshot->numTransforms; ++i) {
            uint32_t id = snapshot->transforms[i].entityID;

            for (size_t j = 0; j < world->firstFreeStaticMesh; ++j) {
                if (world->staticMeshes[j].entityID == id) {
                    //auto pos = util::Get4x4FloatMatrixColumnCM(snapshot->transforms[i].transform, 3).xyz;
                    //GT_LOG_DEBUG("Renderer", "Updating entity #%i to position (%f, %f, %f)", id, pos.x, pos.y, pos.z);
                    util::Copy4x4FloatMatrixCM(snapshot->transforms[i].transform, world->staticMeshes[j].transform);
                }
            }
        }
    }

    bool CreateRenderer(Renderer** outRenderer, fnd::memory::MemoryArenaBase* memoryArena, RendererConfig* config)
    {
        Renderer* renderer = GT_NEW(Renderer, memoryArena);

        renderer->creationArena = memoryArena;
        renderer->gfxDevice = config->gfxDevice;
        renderer->commandBuffer = gfx::GetImmediateCommandBuffer(renderer->gfxDevice);

        {   // load shaders
            size_t vCubeShaderCodeSize = 0;
            char* vCubeShaderCode = static_cast<char*>(LoadFileContents("VertexShaderCube.cso", memoryArena, &vCubeShaderCodeSize));
            if (!vCubeShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
            }

            size_t pShaderCodeSize = 0;
            char* pShaderCode = static_cast<char*>(LoadFileContents("PixelShader.cso", memoryArena, &pShaderCodeSize));
            if (!pShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t vBlitShaderCodeSize = 0;
            char* vBlitShaderCode = static_cast<char*>(LoadFileContents("BlitVertexShader.cso", memoryArena, &vBlitShaderCodeSize));
            if (!vBlitShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
            }

            size_t pBlitShaderCodeSize = 0;
            char* pBlitShaderCode = static_cast<char*>(LoadFileContents("BlitPixelShader.cso", memoryArena, &pBlitShaderCodeSize));
            if (!pBlitShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t pTonemapShaderCodeSize = 0;
            char* pTonemapShaderCode = static_cast<char*>(LoadFileContents("TonemapPixelShader.cso", memoryArena, &pTonemapShaderCodeSize));
            if (!pTonemapShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t pBlurShaderCodeSize = 0;
            char* pBlurShaderCode = static_cast<char*>(LoadFileContents("SelectiveBlurPixelShader.cso", memoryArena, &pBlurShaderCodeSize));
            if (!pBlurShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t pBloomShaderCodeSize = 0;
            char* pBloomShaderCode = static_cast<char*>(LoadFileContents("BloomBlurPixelShader.cso", memoryArena, &pBloomShaderCodeSize));
            if (!pBloomShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t pLuminanceShaderCodeSize = 0;
            char* pLuminanceShaderCode = static_cast<char*>(LoadFileContents("LuminancePixelShader.cso", memoryArena, &pLuminanceShaderCodeSize));
            if (!pLuminanceShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
            }

            size_t vPaintShaderCodeSize = 0;
            char* vPaintShaderCode = static_cast<char*>(LoadFileContents("PaintVertexShader.cso", memoryArena, &vPaintShaderCodeSize));
            if (!vPaintShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
            }

            size_t pPaintShaderCodeSize = 0;
            char* pPaintShaderCode = static_cast<char*>(LoadFileContents("PaintPixelShader.cso", memoryArena, &pPaintShaderCodeSize));
            if (!pPaintShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load p shader\n");
            }


            size_t vCubemapShaderCodeSize = 0;
            char* vCubemapShaderCode = static_cast<char*>(LoadFileContents("CubemapVertexShader.cso", memoryArena, &vCubemapShaderCodeSize));
            if (!vCubemapShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
            }

            size_t pCubemapShaderCodeSize = 0;
            char* pCubemapShaderCode = static_cast<char*>(LoadFileContents("CubemapPixelShader.cso", memoryArena, &pCubemapShaderCodeSize));
            if (!pCubemapShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load p shader\n");
            }

            size_t pPrefilterCubemapShaderCodeSize = 0;
            char* pPrefilterCubemapShaderCode = static_cast<char*>(LoadFileContents("PrefilterCubemapPixelShader.cso", memoryArena, &pPrefilterCubemapShaderCodeSize));
            if (!pPrefilterCubemapShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load p shader\n");
            }

            size_t pBRDFLUTShaderCodeSize = 0;
            char* pBRDFLUTShaderCode = static_cast<char*>(LoadFileContents("BRDFLUT.cso", memoryArena, &pBRDFLUTShaderCodeSize));
            if (!pBRDFLUTShaderCode) {
                GT_LOG_ERROR("D3D11", "Failed to load brdflut shader\n");
            }


            gfx::ShaderDesc vCubeShaderDesc;
            vCubeShaderDesc.type = gfx::ShaderType::SHADER_TYPE_VS;
            vCubeShaderDesc.code = vCubeShaderCode;
            vCubeShaderDesc.codeSize = vCubeShaderCodeSize;

            gfx::ShaderDesc pShaderDesc;
            pShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pShaderDesc.code = pShaderCode;
            pShaderDesc.codeSize = pShaderCodeSize;

            gfx::ShaderDesc vBlitShaderDesc;
            vBlitShaderDesc.type = gfx::ShaderType::SHADER_TYPE_VS;
            vBlitShaderDesc.code = vBlitShaderCode;
            vBlitShaderDesc.codeSize = vBlitShaderCodeSize;
            gfx::ShaderDesc pBlitShaderDesc;
            pBlitShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pBlitShaderDesc.code = pBlitShaderCode;
            pBlitShaderDesc.codeSize = pBlitShaderCodeSize;

            gfx::ShaderDesc pTonemapShaderDesc;
            pTonemapShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pTonemapShaderDesc.code = pTonemapShaderCode;
            pTonemapShaderDesc.codeSize = pTonemapShaderCodeSize;

            gfx::ShaderDesc pBlurShaderDesc;
            pBlurShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pBlurShaderDesc.code = pBlurShaderCode;
            pBlurShaderDesc.codeSize = pBlurShaderCodeSize;

            gfx::ShaderDesc pBloomShaderDesc;
            pBloomShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pBloomShaderDesc.code = pBloomShaderCode;
            pBloomShaderDesc.codeSize = pBloomShaderCodeSize;

            gfx::ShaderDesc pLuminanceShaderDesc;
            pLuminanceShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pLuminanceShaderDesc.code = pLuminanceShaderCode;
            pLuminanceShaderDesc.codeSize = pLuminanceShaderCodeSize;

            gfx::ShaderDesc vPaintShaderDesc;
            vPaintShaderDesc.type = gfx::ShaderType::SHADER_TYPE_VS;
            vPaintShaderDesc.code = vPaintShaderCode;
            vPaintShaderDesc.codeSize = vPaintShaderCodeSize;
            gfx::ShaderDesc pPaintShaderDesc;
            pPaintShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pPaintShaderDesc.code = pPaintShaderCode;
            pPaintShaderDesc.codeSize = pPaintShaderCodeSize;

            gfx::ShaderDesc vCubemapShaderDesc;
            vCubemapShaderDesc.type = gfx::ShaderType::SHADER_TYPE_VS;
            vCubemapShaderDesc.code = vCubemapShaderCode;
            vCubemapShaderDesc.codeSize = vCubemapShaderCodeSize;
            gfx::ShaderDesc pCubemapShaderDesc;
            pCubemapShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pCubemapShaderDesc.code = pCubemapShaderCode;
            pCubemapShaderDesc.codeSize = pCubemapShaderCodeSize;

            gfx::ShaderDesc vPrefilterCubemapShaderDesc;
            vPrefilterCubemapShaderDesc.type = gfx::ShaderType::SHADER_TYPE_VS;
            vPrefilterCubemapShaderDesc.code = vBlitShaderCode;
            vPrefilterCubemapShaderDesc.codeSize = vBlitShaderCodeSize;
            gfx::ShaderDesc pPrefilterCubemapShaderDesc;
            pPrefilterCubemapShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pPrefilterCubemapShaderDesc.code = pPrefilterCubemapShaderCode;
            pPrefilterCubemapShaderDesc.codeSize = pPrefilterCubemapShaderCodeSize;

            gfx::ShaderDesc pBRDFLUTShaderDesc;
            pBRDFLUTShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
            pBRDFLUTShaderDesc.code = pBRDFLUTShaderCode;
            pBRDFLUTShaderDesc.codeSize = pBRDFLUTShaderCodeSize;


            renderer->vCubeShader = gfx::CreateShader(renderer->gfxDevice, &vCubeShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->vCubeShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create vertex shader");
            }

            renderer->pShader = gfx::CreateShader(renderer->gfxDevice, &pShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->pShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
            }

            renderer->vBlitShader = gfx::CreateShader(renderer->gfxDevice, &vBlitShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->vBlitShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create vertex shader");
            }

            renderer->pBlitShader = gfx::CreateShader(renderer->gfxDevice, &pBlitShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->pBlitShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
            }

            renderer->pTonemapShader = gfx::CreateShader(renderer->gfxDevice, &pTonemapShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->pTonemapShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
            }

            renderer->pBlurShader = gfx::CreateShader(renderer->gfxDevice, &pBlurShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->pBlurShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
            }

            renderer->pBloomShader = gfx::CreateShader(renderer->gfxDevice, &pBloomShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->pBloomShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
            }

            renderer->pLuminanceShader = gfx::CreateShader(renderer->gfxDevice, &pLuminanceShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->pLuminanceShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
            }

            renderer->vCubemapShader = gfx::CreateShader(renderer->gfxDevice, &vCubemapShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->vCubemapShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create vertex shader");
            }

            renderer->pCubemapShader = gfx::CreateShader(renderer->gfxDevice, &pCubemapShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->pCubemapShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
            }

            renderer->vPrefilterCubemapShader = gfx::CreateShader(renderer->gfxDevice, &vPrefilterCubemapShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->vPrefilterCubemapShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create vertex shader");
            }

            renderer->pPrefilterCubemapShader = gfx::CreateShader(renderer->gfxDevice, &pPrefilterCubemapShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->pPrefilterCubemapShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
            }

            renderer->pBRDFLUTShader = gfx::CreateShader(renderer->gfxDevice, &pBRDFLUTShaderDesc);
            if (!GFX_CHECK_RESOURCE(renderer->pBRDFLUTShader)) {
                GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
            }
        }
        
        {   // Create procedural cube mesh for cubemap draw calls
            
            auto cubeMesh = par_shapes_create_cube();
            par_shapes_translate(cubeMesh, -0.5f, -0.5f, -0.5f);
            float* cubePositions = cubeMesh->points;
            size_t numCubeVertices = cubeMesh->npoints;
            uint16_t* cubeIndices = cubeMesh->triangles;
            size_t numCubeIndices = 3 * cubeMesh->ntriangles;

            gfx::BufferDesc cubeVertexBufferDesc;
            cubeVertexBufferDesc.type = gfx::BufferType::BUFFER_TYPE_VERTEX;
            cubeVertexBufferDesc.byteWidth = sizeof(float) * 3 * numCubeVertices;
            cubeVertexBufferDesc.initialData = cubePositions;
            cubeVertexBufferDesc.initialDataSize = cubeVertexBufferDesc.byteWidth;
            renderer->cubeVertexBuffer = gfx::CreateBuffer(renderer->gfxDevice, &cubeVertexBufferDesc);
            if (!GFX_CHECK_RESOURCE(renderer->cubeVertexBuffer)) {
                GT_LOG_ERROR("Renderer", "Failed to create cubemap vertex buffer");
            }

            gfx::BufferDesc cubeIndexBufferDesc;
            cubeIndexBufferDesc.type = gfx::BufferType::BUFFER_TYPE_INDEX;
            cubeIndexBufferDesc.byteWidth = sizeof(uint16_t) * numCubeIndices;
            cubeIndexBufferDesc.initialData = cubeIndices;
            cubeIndexBufferDesc.initialDataSize = cubeIndexBufferDesc.byteWidth;
            renderer->cubeIndexBuffer = gfx::CreateBuffer(renderer->gfxDevice, &cubeIndexBufferDesc);
            if (!GFX_CHECK_RESOURCE(renderer->cubeIndexBuffer)) {
                GT_LOG_ERROR("Renderer", "Failed to create cubemap Index buffer");
            }
        }

        {   // Create pipeline states

            gfx::PipelineStateDesc meshPipelineState_16bit;
            meshPipelineState_16bit.indexFormat = gfx::IndexFormat::INDEX_FORMAT_UINT16;
            meshPipelineState_16bit.vertexShader = renderer->vCubeShader;
            meshPipelineState_16bit.pixelShader = renderer->pShader;
            meshPipelineState_16bit.vertexLayout.attribs[0] = { "POSITION", 0, offsetof(DefaultVertex, position), 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT3 };
            meshPipelineState_16bit.vertexLayout.attribs[1] = { "NORMAL", 0, offsetof(DefaultVertex, normal), 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT3 };
            meshPipelineState_16bit.vertexLayout.attribs[2] = { "TEXCOORD", 0, offsetof(DefaultVertex, uv), 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT2 };
            meshPipelineState_16bit.vertexLayout.attribs[3] = { "TEXCOORD", 1, offsetof(DefaultVertex, tangent), 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT3 };

            renderer->meshPipeline_16bit = gfx::CreatePipelineState(renderer->gfxDevice, &meshPipelineState_16bit);
            if (!GFX_CHECK_RESOURCE(renderer->meshPipeline_16bit)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for cube");
            }

            gfx::PipelineStateDesc meshPipelineState_32bit = meshPipelineState_16bit;
            meshPipelineState_32bit.indexFormat = gfx::IndexFormat::INDEX_FORMAT_UINT32;

            renderer->meshPipeline_32bit = gfx::CreatePipelineState(renderer->gfxDevice, &meshPipelineState_32bit);
            if (!GFX_CHECK_RESOURCE(renderer->meshPipeline_32bit)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for cube");
            }

            //
            gfx::PipelineStateDesc cubemapPipelineState;
            cubemapPipelineState.depthStencilState.enableDepth = false;
            cubemapPipelineState.rasterState.cullMode = gfx::CullMode::CULL_FRONT;
            cubemapPipelineState.indexFormat = gfx::IndexFormat::INDEX_FORMAT_UINT16;
            cubemapPipelineState.vertexShader = renderer->vCubemapShader;
            cubemapPipelineState.pixelShader = renderer->pCubemapShader;
            cubemapPipelineState.vertexLayout.attribs[0] = { "POSITION", 0, 0, 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT3 };
            renderer->cubemapPipeline = gfx::CreatePipelineState(renderer->gfxDevice, &cubemapPipelineState);
            if (!GFX_CHECK_RESOURCE(renderer->cubemapPipeline)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for cubemap");
            }

            gfx::PipelineStateDesc prefilterCubemapStateDesc;
            prefilterCubemapStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_NONE;
            prefilterCubemapStateDesc.vertexShader = renderer->vPrefilterCubemapShader;
            prefilterCubemapStateDesc.pixelShader = renderer->pPrefilterCubemapShader;
            prefilterCubemapStateDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLE_STRIP;
            renderer->prefilterPipeline = gfx::CreatePipelineState(renderer->gfxDevice, &prefilterCubemapStateDesc);
            if (!GFX_CHECK_RESOURCE(renderer->prefilterPipeline)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for cubemap prefiltering");
            }


            gfx::PipelineStateDesc blitPipelineStateDesc;
            blitPipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_NONE;
            blitPipelineStateDesc.vertexShader = renderer->vBlitShader;
            blitPipelineStateDesc.pixelShader = renderer->pBlitShader;
            blitPipelineStateDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLE_STRIP;

            blitPipelineStateDesc.depthStencilState.enableDepth = false;
            blitPipelineStateDesc.blendState.enableBlend = true;
            blitPipelineStateDesc.blendState.srcBlend = gfx::BlendFactor::BLEND_ONE;
            blitPipelineStateDesc.blendState.dstBlend = gfx::BlendFactor::BLEND_SRC_ALPHA;
            blitPipelineStateDesc.blendState.blendOp = gfx::BlendOp::BLEND_OP_ADD;
            blitPipelineStateDesc.blendState.writeMask = gfx::COLOR_WRITE_MASK_COLOR;
            renderer->blitPipeline = gfx::CreatePipelineState(renderer->gfxDevice, &blitPipelineStateDesc);
            if (!GFX_CHECK_RESOURCE(renderer->blitPipeline)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for blit");
            }

            gfx::PipelineStateDesc tonemapPipelineStateDesc;
            tonemapPipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_NONE;
            tonemapPipelineStateDesc.vertexShader = renderer->vBlitShader;
            tonemapPipelineStateDesc.pixelShader = renderer->pTonemapShader;
            tonemapPipelineStateDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLE_STRIP;

            tonemapPipelineStateDesc.depthStencilState.enableDepth = false;
            tonemapPipelineStateDesc.blendState.enableBlend = true;
            tonemapPipelineStateDesc.blendState.srcBlend = gfx::BlendFactor::BLEND_ONE;
            tonemapPipelineStateDesc.blendState.dstBlend = gfx::BlendFactor::BLEND_SRC_ALPHA;
            tonemapPipelineStateDesc.blendState.blendOp = gfx::BlendOp::BLEND_OP_ADD;
            tonemapPipelineStateDesc.blendState.writeMask = gfx::COLOR_WRITE_MASK_COLOR;
            renderer->tonemapPipeline = gfx::CreatePipelineState(renderer->gfxDevice, &tonemapPipelineStateDesc);
            if (!GFX_CHECK_RESOURCE(renderer->tonemapPipeline)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for tonemap");
            }

            gfx::PipelineStateDesc blurPipelineStateDesc;
            blurPipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_NONE;
            blurPipelineStateDesc.vertexShader = renderer->vBlitShader;
            blurPipelineStateDesc.pixelShader = renderer->pBlurShader;
            blurPipelineStateDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLE_STRIP;
            blurPipelineStateDesc.depthStencilState.enableDepth = false;
            blurPipelineStateDesc.blendState.enableBlend = false;
            blurPipelineStateDesc.blendState.srcBlend = gfx::BlendFactor::BLEND_SRC_ALPHA;
            blurPipelineStateDesc.blendState.dstBlend = gfx::BlendFactor::BLEND_INV_SRC_ALPHA;
            blurPipelineStateDesc.blendState.blendOp = gfx::BlendOp::BLEND_OP_ADD;
            blurPipelineStateDesc.blendState.writeMask = gfx::COLOR_WRITE_MASK_COLOR;
            renderer->blurPipeline = gfx::CreatePipelineState(renderer->gfxDevice, &blurPipelineStateDesc);
            if (!GFX_CHECK_RESOURCE(renderer->blurPipeline)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for blit");
            }

            gfx::PipelineStateDesc bloomPipelineStateDesc;
            bloomPipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_NONE;
            bloomPipelineStateDesc.vertexShader = renderer->vBlitShader;
            bloomPipelineStateDesc.pixelShader = renderer->pBloomShader;
            bloomPipelineStateDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLE_STRIP;
            bloomPipelineStateDesc.depthStencilState.enableDepth = false;
            bloomPipelineStateDesc.blendState.enableBlend = false;
            bloomPipelineStateDesc.blendState.srcBlend = gfx::BlendFactor::BLEND_SRC_ALPHA;
            bloomPipelineStateDesc.blendState.dstBlend = gfx::BlendFactor::BLEND_INV_SRC_ALPHA;
            bloomPipelineStateDesc.blendState.blendOp = gfx::BlendOp::BLEND_OP_ADD;
            bloomPipelineStateDesc.blendState.writeMask = gfx::COLOR_WRITE_MASK_COLOR;
            renderer->bloomPipeline = gfx::CreatePipelineState(renderer->gfxDevice, &bloomPipelineStateDesc);
            if (!GFX_CHECK_RESOURCE(renderer->bloomPipeline)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for blit");
            }

            gfx::PipelineStateDesc luminancePipelineStateDesc;
            luminancePipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_NONE;
            luminancePipelineStateDesc.vertexShader = renderer->vBlitShader;
            luminancePipelineStateDesc.pixelShader = renderer->pLuminanceShader;
            luminancePipelineStateDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLE_STRIP;
            renderer->luminancePipeline = gfx::CreatePipelineState(renderer->gfxDevice, &luminancePipelineStateDesc);
            if (!GFX_CHECK_RESOURCE(renderer->luminancePipeline)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for blit");
            }

            gfx::PipelineStateDesc brdfLUTPipelineStateDesc;
            brdfLUTPipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_NONE;
            brdfLUTPipelineStateDesc.vertexShader = renderer->vBlitShader;
            brdfLUTPipelineStateDesc.pixelShader = renderer->pBRDFLUTShader;
            brdfLUTPipelineStateDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLE_STRIP;
            renderer->brdfLUTPipeline = gfx::CreatePipelineState(renderer->gfxDevice, &brdfLUTPipelineStateDesc);
            if (!GFX_CHECK_RESOURCE(renderer->brdfLUTPipeline)) {
                GT_LOG_ERROR("Renderer", "Failed to create pipeline state for blit");
            }
        }

        {   // create render targets / procedural textures / buffers

            gfx::SamplerDesc defaultSamplerStateDesc;

            gfx::ImageDesc uiRenderTargetDesc;
            uiRenderTargetDesc.isRenderTarget = true;
            uiRenderTargetDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
            uiRenderTargetDesc.width = config->windowWidth;
            uiRenderTargetDesc.height = config->windowHeight;
            uiRenderTargetDesc.samplerDesc = &defaultSamplerStateDesc;
            renderer->uiRenderTarget = gfx::CreateImage(renderer->gfxDevice, &uiRenderTargetDesc);
            if (!GFX_CHECK_RESOURCE(renderer->uiRenderTarget)) {
                GT_LOG_ERROR("Renderer", "Failed to create render target for UI");
            }

            gfx::ImageDesc mainRTDesc;
            mainRTDesc.isRenderTarget = true;
            mainRTDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
            mainRTDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R16G16B16A16_FLOAT;
            mainRTDesc.width = config->windowWidth;
            mainRTDesc.height = config->windowHeight;
            mainRTDesc.samplerDesc = &defaultSamplerStateDesc;
            renderer->mainRT = gfx::CreateImage(renderer->gfxDevice, &mainRTDesc);
            if (!GFX_CHECK_RESOURCE(renderer->mainRT)) {
                GT_LOG_ERROR("Renderer", "Failed to create main render target");
            }

            gfx::ImageDesc luminanceRTDesc;
            luminanceRTDesc.isRenderTarget = true;
            luminanceRTDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
            luminanceRTDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R16G16B16A16_FLOAT;
            luminanceRTDesc.width = config->windowWidth;
            luminanceRTDesc.height = config->windowHeight;
            luminanceRTDesc.samplerDesc = &defaultSamplerStateDesc;
            renderer->luminanceRT = gfx::CreateImage(renderer->gfxDevice, &luminanceRTDesc);
            if (!GFX_CHECK_RESOURCE(renderer->luminanceRT)) {
                GT_LOG_ERROR("Renderer", "Failed to create main render target");
            }

            gfx::ImageDesc bloomRTDesc;
            bloomRTDesc.isRenderTarget = true;
            bloomRTDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
            bloomRTDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R16G16B16A16_FLOAT;
            bloomRTDesc.width = config->windowWidth;
            bloomRTDesc.height = config->windowHeight;
            bloomRTDesc.samplerDesc = &defaultSamplerStateDesc;
            renderer->bloomRT = gfx::CreateImage(renderer->gfxDevice, &bloomRTDesc);
            if (!GFX_CHECK_RESOURCE(renderer->bloomRT)) {
                GT_LOG_ERROR("Renderer", "Failed to create main render target");
            }

            //
            gfx::ImageDesc mainDepthBufferDesc;
            //mainDepthBufferDesc.isRenderTarget = true;
            mainDepthBufferDesc.isDepthStencilTarget = true;
            mainDepthBufferDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
            mainDepthBufferDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_D32_FLOAT_S8X24_UINT;
            mainDepthBufferDesc.width = config->windowWidth;
            mainDepthBufferDesc.height = config->windowHeight;
            mainDepthBufferDesc.samplerDesc = &defaultSamplerStateDesc;
            renderer->mainDepthBuffer = gfx::CreateImage(renderer->gfxDevice, &mainDepthBufferDesc);
            if (!GFX_CHECK_RESOURCE(renderer->mainDepthBuffer)) {
                GT_LOG_ERROR("Renderer", "Failed to create main depth buffer target");
            }

            //
            gfx::SamplerDesc brdfLUTSamplerDesc;
            brdfLUTSamplerDesc.wrapU = gfx::WrapMode::WRAP_CLAMP_TO_EDGE;
            brdfLUTSamplerDesc.wrapV = gfx::WrapMode::WRAP_CLAMP_TO_EDGE;

            gfx::ImageDesc brdfLUTDesc;
            brdfLUTDesc.isRenderTarget = true;
            brdfLUTDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
            brdfLUTDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R16G16B16A16_FLOAT;
            brdfLUTDesc.width = 512;
            brdfLUTDesc.height = 512;
            brdfLUTDesc.samplerDesc = &brdfLUTSamplerDesc;
            renderer->brdfLUT = gfx::CreateImage(renderer->gfxDevice, &brdfLUTDesc);
            if (!GFX_CHECK_RESOURCE(renderer->brdfLUT)) {
                GT_LOG_ERROR("Renderer", "Failed to create main render target");
            }

            //
            char fileNameBuf[512] = "";
            

            for (size_t i = 0; i < Renderer::NUM_CUBEMAPS; ++i)
            {   // hdr cubemap
                int width, height, numComponents;
                snprintf(fileNameBuf, 512, "../../hdrCubemap%llu.hdr", i);

                stbi_set_flip_vertically_on_load(1);
                auto image = stbi_loadf(fileNameBuf, &width, &height, &numComponents, 4);
                stbi_set_flip_vertically_on_load(0);
                //image = stbi_load_from_memory(buf, buf_len, &width, &height, &numComponents, 4);
                if (image == NULL) {
                    GT_LOG_ERROR("Assets", "Failed to load image %s:\n%s\n", fileNameBuf, stbi_failure_reason());
                }
                //assert(numComponents == 4);

                gfx::ImageDesc diffDesc;
                //paintTextureDesc.usage = gfx::ResourceUsage::USAGE_DYNAMIC;
                diffDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
                diffDesc.width = width;
                diffDesc.height = height;
                diffDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R32G32B32A32_FLOAT;
                diffDesc.samplerDesc = &defaultSamplerStateDesc;
                diffDesc.numDataItems = 1;
                void* data[] = { image };
                size_t size = sizeof(float) * width * height * 4;
                diffDesc.initialData = data;
                diffDesc.initialDataSizes = &size;
                renderer->hdrCubemap[i] = gfx::CreateImage(renderer->gfxDevice, &diffDesc);
                if (!GFX_CHECK_RESOURCE(renderer->hdrCubemap[i])) {
                    GT_LOG_ERROR("Renderer", "Failed to create texture");
                }
                stbi_image_free(image);

                gfx::ImageDesc prefilteredDesc;
                prefilteredDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
                prefilteredDesc.numMipmaps = Renderer::NUM_CONVOLUTION_MIPS;
                prefilteredDesc.width = width;
                prefilteredDesc.height = height;
                prefilteredDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R32G32B32A32_FLOAT;
                prefilteredDesc.samplerDesc = &defaultSamplerStateDesc;
                prefilteredDesc.isRenderTarget = true;
                renderer->prefilteredCubemap[i] = gfx::CreateImage(renderer->gfxDevice, &prefilteredDesc);
                if (!GFX_CHECK_RESOURCE(renderer->prefilteredCubemap[i])) {
                    GT_LOG_ERROR("Renderer", "Failed to create prefiltered cubemap RT");
                }
                renderer->prefilteredCubemapDimensions[i].x = (float)width;
                renderer->prefilteredCubemapDimensions[i].y = (float)height;

            }

            for (size_t i = 0; i < Renderer::NUM_CUBEMAPS; ++i)
            {   // hdr cubemap
                int width, height, numComponents;
                snprintf(fileNameBuf, 512, "../../hdrConvolvedDiffuse%llu.hdr", i);

                stbi_set_flip_vertically_on_load(1);
                auto image = stbi_loadf(fileNameBuf, &width, &height, &numComponents, 4);
                stbi_set_flip_vertically_on_load(0);
                //image = stbi_load_from_memory(buf, buf_len, &width, &height, &numComponents, 4);
                if (image == NULL) {
                    GT_LOG_ERROR("Assets", "Failed to load image %s:\n%s\n", fileNameBuf, stbi_failure_reason());
                }
                //assert(numComponents == 4);

                gfx::ImageDesc diffDesc;
                //paintTextureDesc.usage = gfx::ResourceUsage::USAGE_DYNAMIC;
                diffDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
                diffDesc.width = width;
                diffDesc.height = height;
                diffDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R32G32B32A32_FLOAT;
                diffDesc.samplerDesc = &defaultSamplerStateDesc;
                diffDesc.numDataItems = 1;
                void* data[] = { image };
                size_t size = sizeof(float) * width * height * 4;
                diffDesc.initialData = data;
                diffDesc.initialDataSizes = &size;
                renderer->hdrDiffuse[i] = gfx::CreateImage(renderer->gfxDevice, &diffDesc);
                if (!GFX_CHECK_RESOURCE(renderer->hdrDiffuse[i])) {
                    GT_LOG_ERROR("Renderer", "Failed to create texture");
                }
                stbi_image_free(image);
            }
        }

        {   // create constant buffers

            gfx::BufferDesc cBufferDesc;
            cBufferDesc.type = gfx::BufferType::BUFFER_TYPE_CONSTANT;
            cBufferDesc.byteWidth = sizeof(ConstantData);
            cBufferDesc.initialData = nullptr;
            cBufferDesc.initialDataSize = 0;
            cBufferDesc.usage = gfx::ResourceUsage::USAGE_STREAM;
            renderer->cBuffer = gfx::CreateBuffer(renderer->gfxDevice, &cBufferDesc);
            if (!GFX_CHECK_RESOURCE(renderer->cBuffer)) {
                GT_LOG_ERROR("Renderer", "Failed to create constant buffer");
            }


            gfx::BufferDesc prefilterCBufferDesc;
            prefilterCBufferDesc.type = gfx::BufferType::BUFFER_TYPE_CONSTANT;
            prefilterCBufferDesc.byteWidth = sizeof(fnd::math::float4);
            prefilterCBufferDesc.initialData = nullptr;
            prefilterCBufferDesc.initialDataSize = 0;
            prefilterCBufferDesc.usage = gfx::ResourceUsage::USAGE_STREAM;
            renderer->prefilterCBuffer = gfx::CreateBuffer(renderer->gfxDevice, &prefilterCBufferDesc);
            if (!GFX_CHECK_RESOURCE(renderer->prefilterCBuffer)) {
                GT_LOG_ERROR("Renderer", "Failed to create constant buffer");
            }
        }


        {   // create render passes
            gfx::RenderPassDesc mainRenderPassDesc;
            mainRenderPassDesc.colorAttachments[0].image = renderer->mainRT;
            mainRenderPassDesc.depthStencilAttachment.image = renderer->mainDepthBuffer;
            renderer->mainPass = gfx::CreateRenderPass(renderer->gfxDevice, &mainRenderPassDesc);
            if (!GFX_CHECK_RESOURCE(renderer->mainPass)) {
                GT_LOG_ERROR("Renderer", "Failed to create main render pass");
            }

            gfx::RenderPassDesc uiPassDesc;
            uiPassDesc.colorAttachments[0].image = renderer->uiRenderTarget;
            renderer->uiPass = gfx::CreateRenderPass(renderer->gfxDevice, &uiPassDesc);
            if (!GFX_CHECK_RESOURCE(renderer->uiPass)) {
                GT_LOG_ERROR("Renderer", "Failed to create render pass for UI");
            }

            gfx::RenderPassDesc luminancePassDesc;
            luminancePassDesc.colorAttachments[0].image = renderer->luminanceRT;
            renderer->luminancePass = gfx::CreateRenderPass(renderer->gfxDevice, &luminancePassDesc);
            if (!GFX_CHECK_RESOURCE(renderer->luminancePass)) {
                GT_LOG_ERROR("Renderer", "Failed to create render pass for UI");
            }

            gfx::RenderPassDesc bloomPassDesc;
            bloomPassDesc.colorAttachments[0].image = renderer->bloomRT;
            renderer->bloomPass = gfx::CreateRenderPass(renderer->gfxDevice, &bloomPassDesc);
            if (!GFX_CHECK_RESOURCE(renderer->bloomPass)) {
                GT_LOG_ERROR("Renderer", "Failed to create render pass for UI");
            }

            gfx::RenderPassDesc bloomBlitPassDesc;
            bloomBlitPassDesc.colorAttachments[0].image = renderer->mainRT;
            renderer->bloomBlitPass = gfx::CreateRenderPass(renderer->gfxDevice, &bloomBlitPassDesc);
            if (!GFX_CHECK_RESOURCE(renderer->bloomBlitPass)) {
                GT_LOG_ERROR("Renderer", "Failed to create render pass for UI");
            }

            gfx::RenderPassDesc brdfLUTPassDesc;
            brdfLUTPassDesc.colorAttachments[0].image = renderer->brdfLUT;
            renderer->brdfLUTPass = gfx::CreateRenderPass(renderer->gfxDevice, &brdfLUTPassDesc);
            if (!GFX_CHECK_RESOURCE(renderer->brdfLUTPass)) {
                GT_LOG_ERROR("Renderer", "Failed to create render pass for brdfLUT");
            }

            // initialize the brdf lut
            gfx::RenderPassAction clearAllAction;
            clearAllAction.colors[0].action = gfx::Action::ACTION_CLEAR;
            clearAllAction.depth.action = gfx::Action::ACTION_CLEAR;
            float pink[] = { 1.0f, 192.0f / 255.0f, 203.0f / 255.0f, 1.0f };
            memcpy(clearAllAction.colors[0].color, pink, sizeof(float) * 4);
            

            gfx::DrawCall brdfLUTDrawCall;
            brdfLUTDrawCall.pipelineState = renderer->brdfLUTPipeline;
            brdfLUTDrawCall.numElements = 4;
            gfx::BeginRenderPass(renderer->gfxDevice, renderer->commandBuffer, renderer->brdfLUTPass, &clearAllAction);
            gfx::Viewport brdfLutViewport;
            brdfLutViewport.width = 512;
            brdfLutViewport.height = 512;
            gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &brdfLUTDrawCall, &brdfLutViewport);
            gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);
            
            // prefilter all cubemaps
            for (size_t i = 0; i < Renderer::NUM_CUBEMAPS; ++i) {

                for (uint16_t mipmapLevel = 0; mipmapLevel < Renderer::NUM_CONVOLUTION_MIPS; ++mipmapLevel) {

                    gfx::RenderPassDesc prefilterPassDesc;
                    prefilterPassDesc.colorAttachments[0].image = renderer->prefilteredCubemap[i];
                    prefilterPassDesc.colorAttachments[0].mipmapLevel = mipmapLevel;
                    gfx::RenderPass prefilterPass = gfx::CreateRenderPass(renderer->gfxDevice, &prefilterPassDesc);
                    if (!GFX_CHECK_RESOURCE(prefilterPass)) {
                        GT_LOG_ERROR("Renderer", "Failed to create render pass for cubemap prefiltering");
                    }

                    gfx::DrawCall prefilterDrawCall;
                    prefilterDrawCall.numElements = 4;
                    prefilterDrawCall.pipelineState = renderer->prefilterPipeline;
                    prefilterDrawCall.psImageInputs[0] = renderer->hdrCubemap[i];
                    
                    float roughness = (float)mipmapLevel / float(Renderer::NUM_CONVOLUTION_MIPS - 1);
                    void* cBufferMem = gfx::MapBuffer(renderer->gfxDevice, renderer->prefilterCBuffer, gfx::MapType::MAP_WRITE_DISCARD);
                    fnd::math::float4 params(roughness, 0.0f, 0.0f, 0.0f);
                    memcpy(cBufferMem, &params, sizeof(float) * 4);
                    gfx::UnmapBuffer(renderer->gfxDevice, renderer->prefilterCBuffer);

                    prefilterDrawCall.psConstantInputs[0] = renderer->prefilterCBuffer;

                    gfx::BeginRenderPass(renderer->gfxDevice, renderer->commandBuffer, prefilterPass, &clearAllAction);
                    gfx::Viewport prefilterViewport;
                    prefilterViewport.width = renderer->prefilteredCubemapDimensions[i].x * powf(0.5f, (float)mipmapLevel);
                    prefilterViewport.height = renderer->prefilteredCubemapDimensions[i].y * powf(0.5f, (float)mipmapLevel);
                    gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &prefilterDrawCall, &prefilterViewport);
                    gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);
                }
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
        first->asset = assetID;
        for (size_t i = 1; i < numSubmeshes; ++i) {
            MeshData* next; 
            if (!world->meshLibrary.pool.Allocate(&next, &id)) { return false; }    // @TODO error handling
            next->asset = assetID;
            it->nextSubmesh = next;
            it = next;
        }

        assetToData->data = first;
        
        it = first;
        for (size_t i = 0; i < numSubmeshes && it != nullptr; ++i) {
            auto desc = meshDesc[i];
            
            it->numElements = (uint32_t)desc.numElements;

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
        texture->asset = assetID;
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

        material->asset = assetID;
        material->baseColorMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->baseColorMap);
        material->roughnessMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->roughnessMap);
        material->metalnessMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->metalnessMap);
        material->normalVecMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->normalVecMap);
        material->occlusionMap = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, materialDesc->occlusionMap);

        assetToData->data = material;

        return true;
    }

    core::Asset GetMeshAsset(RenderWorld* world, StaticMesh mesh)
    {
        auto* index = world->staticMeshIndices.Get(mesh.id);
        if (index == nullptr || HANDLE_GENERATION(mesh.id) != index->generation) {
            return { 0 };
        }
        StaticMeshRenderable* source = &world->staticMeshes[index->index];
        return source->firstSubmesh->asset;
    }

    void GetMaterials(RenderWorld* world, StaticMesh mesh, core::Asset* outMaterials, size_t* outNumMaterials)
    {
        auto* index = world->staticMeshIndices.Get(mesh.id);
        if(index == nullptr || HANDLE_GENERATION(mesh.id) != index->generation) {
            if (outNumMaterials != nullptr) {
                *outNumMaterials = 0;
            }
            return;
        }
        StaticMeshRenderable* source = &world->staticMeshes[index->index];
        auto it = source->firstSubmesh;
        size_t numMaterials = 0;
        while (it) {
            if (outMaterials != nullptr) {
                outMaterials[numMaterials].id = 0;
                if (source->materials[numMaterials] != nullptr) {
                    outMaterials[numMaterials] = source->materials[numMaterials]->asset;
                }
            }
            numMaterials++;
            it = it->nextSubmesh;
        }
        if (outNumMaterials != nullptr) {
            *outNumMaterials = numMaterials;
        }
    }

    StaticMeshRenderable* AllocateStaticMesh(RenderWorld* world, StaticMesh* outID)
    {
        RenderableIndex* index;
        StaticMesh meshID;
        if (!world->staticMeshIndices.Allocate(&index, &meshID.id)) { return { nullptr }; }

        index->index = (uint32_t)world->firstFreeStaticMesh++;
        if (index->index >= (uint32_t)world->config.renderablePoolSize) { return { nullptr }; }
        *outID = meshID;
        return &world->staticMeshes[index->index];
    }

    StaticMesh CreateStaticMesh(RenderWorld* world, uint32_t entityID, core::Asset mesh, core::Asset* materials, size_t numMaterials)
    {
        StaticMesh meshID;
        StaticMeshRenderable* renderable = AllocateStaticMesh(world, &meshID);
        if (renderable == nullptr) { return { INVALID_ID }; }

        memset(renderable, 0x0, sizeof(StaticMeshRenderable));

        renderable->entityID = entityID;
        renderable->handle = meshID;
        renderable->firstSubmesh = LookupResource<MeshLibrary, MeshData>(&world->meshLibrary, mesh);
        numMaterials = numMaterials <= MAX_NUM_SUBMESHES ? numMaterials : MAX_NUM_SUBMESHES;
        for (size_t i = 0; i < numMaterials; ++i) {
            renderable->materials[i] = LookupResource<MaterialLibrary, MaterialData>(&world->materialLibrary, materials[i]);
            renderable->materialAssetHandles[i] = materials[i];
        }

        renderable->meshAssetHandle = mesh;

        return meshID;
    }

    void DestroyStaticMesh(RenderWorld* world, StaticMesh mesh)
    {
        RenderableIndex* index = world->staticMeshIndices.Get(mesh.id);
        assert(index->generation == HANDLE_GENERATION(mesh.id));
    
        uint32_t lastNonFree = (uint32_t)world->firstFreeStaticMesh - 1;
        StaticMeshRenderable* swap = &world->staticMeshes[lastNonFree];
        RenderableIndex* swapIndex = world->staticMeshIndices.Get(swap->handle.id);

        StaticMeshRenderable* target = &world->staticMeshes[index->index];

        memcpy(target, swap, sizeof(StaticMeshRenderable));
        swapIndex->index = index->index;
        
        world->firstFreeStaticMesh--;
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

    StaticMesh CopyStaticMesh(RenderWorld* world, uint32_t entityID, StaticMesh mesh)
    {
        auto* index = world->staticMeshIndices.Get(mesh.id);
        assert(HANDLE_GENERATION(mesh.id) == index->generation);
        StaticMeshRenderable* source = &world->staticMeshes[index->index];

        StaticMesh meshID;
        StaticMeshRenderable* target = AllocateStaticMesh(world, &meshID);
        if (target == nullptr) { return { INVALID_ID }; }

        memcpy(target, source, sizeof(StaticMeshRenderable));
        target->entityID = entityID;
        target->handle = meshID;

        return meshID;
    }



    gfx::Image GetTextureHandle(RenderWorld* world, core::Asset assetID)
    {
        TextureData* data = LookupResource<TextureLibrary, TextureData>(&world->textureLibrary, assetID);
        if (data) {
            return data->image;
        }
        else {
            return { gfx::INVALID_ID };
        }
    }

    void RenderUI(Renderer* renderer, ImDrawData* drawData, gfx::SwapChain swapChain)
    {
        gfx::RenderPassAction uiPassAction;
        uiPassAction.colors[0].color[0] = 0.0f;
        uiPassAction.colors[0].color[1] = 0.0f;
        uiPassAction.colors[0].color[2] = 0.0f;
        uiPassAction.colors[0].color[3] = 1.0f;
        uiPassAction.colors[0].action = gfx::Action::ACTION_CLEAR;
        gfx::BeginRenderPass(renderer->gfxDevice, renderer->commandBuffer, renderer->uiPass, &uiPassAction);
        ImGui_ImplDX11_RenderDrawLists(drawData, &renderer->commandBuffer);
        gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);
    }

    void Render(RenderWorld* world, gfx::SwapChain swapChain)
    {
        Renderer* renderer = world->renderer;
        
        gfx::RenderPassAction clearAllAction;
        clearAllAction.colors[0].action = gfx::Action::ACTION_CLEAR;
        clearAllAction.depth.action = gfx::Action::ACTION_CLEAR;
        float pink[] = { 1.0f, 192.0f / 255.0f, 203.0f / 255.0f, 1.0f };
        memcpy(clearAllAction.colors[0].color, pink, sizeof(float) * 4);

        renderer->activeCubemap = renderer->activeCubemap < Renderer::NUM_CUBEMAPS ? renderer->activeCubemap : Renderer::NUM_CUBEMAPS - 1;

        {   // initial cbuffer setup
            void* cBufferMem = gfx::MapBuffer(renderer->gfxDevice, renderer->cBuffer, gfx::MapType::MAP_WRITE_DISCARD);
            if (cBufferMem != nullptr) {
                ConstantData object;
                util::Make4x4FloatMatrixIdentity(object.MVP);
                util::Make4x4FloatMatrixIdentity(object.MV);
                util::Make4x4FloatMatrixIdentity(object.VP);
                util::Make4x4FloatMatrixIdentity(object.view);
                util::Make4x4FloatMatrixIdentity(object.projection);
                util::Make4x4FloatMatrixIdentity(object.model);

                float modelView[16];
                util::MultiplyMatricesCM(world->cameraTransform, object.model, modelView);
                util::MultiplyMatricesCM(world->cameraProjection, modelView, object.MVP);
                util::Copy4x4FloatMatrixCM(world->cameraTransform, object.view);
                util::Inverse4x4FloatMatrixCM(world->cameraTransform, object.inverseView);
                util::Copy4x4FloatMatrixCM(object.model, object.model);
                util::Copy4x4FloatMatrixCM(modelView, object.MV);
                util::Copy4x4FloatMatrixCM(world->cameraProjection, object.projection);
                util::MultiplyMatricesCM(world->cameraProjection, world->cameraTransform, object.VP);

                object.color = fnd::math::float4();
                object.lightDir = fnd::math::float4(1.0f, -1.0f, 0.0f, 0.0f);
                object.roughness = 0.0f;
                object.metallic = 0.0f;
                object.useTextures = 1;
                memcpy(cBufferMem, &object, sizeof(ConstantData));
                gfx::UnmapBuffer(renderer->gfxDevice, renderer->cBuffer);
            }
        }

        // if this is the first frame:
        static size_t frameIndex = 0;
        if (frameIndex == 0) {
            // prefilter all cubemaps
            
        }

        // Draw calls
        gfx::DrawCall cubemapDrawCall;
        gfx::DrawCall meshDrawCall;

        {   // prepare draw calls

            cubemapDrawCall.vertexBuffers[0] = renderer->cubeVertexBuffer;
            cubemapDrawCall.vertexOffsets[0] = 0;
            cubemapDrawCall.vertexStrides[0] = sizeof(float) * 3;
            cubemapDrawCall.indexBuffer = renderer->cubeIndexBuffer;
            cubemapDrawCall.numElements = 36;
            cubemapDrawCall.pipelineState = renderer->cubemapPipeline;
            cubemapDrawCall.vsConstantInputs[0] = renderer->cBuffer;
            cubemapDrawCall.psImageInputs[0] = renderer->prefilteredCubemap[renderer->activeCubemap];

            meshDrawCall.psImageInputs[9] = renderer->prefilteredCubemap[renderer->activeCubemap];
            meshDrawCall.psImageInputs[10] = renderer->hdrDiffuse[renderer->activeCubemap];
            meshDrawCall.psImageInputs[11] = renderer->brdfLUT;
        }

        {   // main render pass
            gfx::BeginRenderPass(renderer->gfxDevice, renderer->commandBuffer, renderer->mainPass, &clearAllAction);

            gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &cubemapDrawCall);
            

            for (size_t i = 0; i < world->firstFreeStaticMesh; ++i) {
                auto staticMesh = &world->staticMeshes[i];
                
                //GT_LOG_DEBUG("Renderer", "Rendering mesh #%llu", i);

                void* cBufferMem = gfx::MapBuffer(renderer->gfxDevice, renderer->cBuffer, gfx::MapType::MAP_WRITE_DISCARD);
                if (cBufferMem != nullptr) {
                    ConstantData object;
                    util::Make4x4FloatMatrixIdentity(object.MVP);
                    util::Make4x4FloatMatrixIdentity(object.MV);
                    util::Make4x4FloatMatrixIdentity(object.VP);
                    util::Make4x4FloatMatrixIdentity(object.view);
                    util::Make4x4FloatMatrixIdentity(object.projection);
                    util::Make4x4FloatMatrixIdentity(object.model);

                    util::Copy4x4FloatMatrixCM(staticMesh->transform, object.model);

                    float modelView[16];
                    util::MultiplyMatricesCM(world->cameraTransform, object.model, modelView);
                    util::MultiplyMatricesCM(world->cameraProjection, modelView, object.MVP);
                    util::Copy4x4FloatMatrixCM(world->cameraTransform, object.view);
                    util::Inverse4x4FloatMatrixCM(world->cameraTransform, object.inverseView);
                    util::Copy4x4FloatMatrixCM(object.model, object.model);
                    util::Copy4x4FloatMatrixCM(modelView, object.MV);
                    util::Copy4x4FloatMatrixCM(world->cameraProjection, object.projection);
                    util::MultiplyMatricesCM(world->cameraProjection, world->cameraTransform, object.VP);

                    object.color = fnd::math::float4();
                    object.lightDir = fnd::math::float4(1.0f, -1.0f, 0.0f, 0.0f);
                    object.roughness = 0.0f;
                    object.metallic = 0.0f;
                    object.useTextures = 1;
                    memcpy(cBufferMem, &object, sizeof(ConstantData));
                    gfx::UnmapBuffer(renderer->gfxDevice, renderer->cBuffer);
                }
                meshDrawCall.vsConstantInputs[0] = renderer->cBuffer;
                meshDrawCall.psConstantInputs[0] = renderer->cBuffer;

                size_t materialIndex = 0;
                auto it = staticMesh->firstSubmesh;
                while (it != nullptr) {
              
                    if (staticMesh->materials[materialIndex++] != nullptr) {
                        auto material = staticMesh->materials[materialIndex - 1];
                        
                        meshDrawCall.vertexBuffers[0] = it->vertexBuffers[0];
                        meshDrawCall.vertexOffsets[0] = 0;
                        meshDrawCall.vertexStrides[0] = sizeof(DefaultVertex);

                        meshDrawCall.indexBuffer = it->indexBuffer;

                        if (it->indexFormat == gfx::IndexFormat::INDEX_FORMAT_UINT16) {
                            meshDrawCall.pipelineState = renderer->meshPipeline_16bit;
                        }
                        else {
                            meshDrawCall.pipelineState = renderer->meshPipeline_32bit;
                        }

                        meshDrawCall.numElements = it->numElements;

                        meshDrawCall.psImageInputs[0] = material->baseColorMap->image;
                        meshDrawCall.psImageInputs[1] = material->roughnessMap->image;
                        meshDrawCall.psImageInputs[2] = material->metalnessMap->image;
                        meshDrawCall.psImageInputs[3] = material->normalVecMap->image;;
                        meshDrawCall.psImageInputs[4] = material->occlusionMap->image;
                        
                        gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &meshDrawCall);
                    }
                    it = it->nextSubmesh;
                }
            }
            gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);
        }

        gfx::RenderPassAction blitAction;

        {   // luminance
            blitAction.colors[0].action = gfx::Action::ACTION_CLEAR;

            gfx::DrawCall luminanceDrawCall;
            luminanceDrawCall.pipelineState = renderer->luminancePipeline;
            luminanceDrawCall.numElements = 4;
            luminanceDrawCall.psImageInputs[0] = renderer->mainRT;

            gfx::BeginRenderPass(renderer->gfxDevice, renderer->commandBuffer, renderer->luminancePass, &blitAction);
            gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &luminanceDrawCall);
            gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);

            blitAction.colors[0].action = gfx::Action::ACTION_LOAD;
        }

        {   // bloom blur
            blitAction.colors[0].action = gfx::Action::ACTION_CLEAR;

            gfx::DrawCall blurDrawCall;
            blurDrawCall.pipelineState = renderer->bloomPipeline;
            blurDrawCall.numElements = 4;
            blurDrawCall.psImageInputs[0] = renderer->mainRT;
            blurDrawCall.psImageInputs[1] = renderer->luminanceRT;

            gfx::BeginRenderPass(renderer->gfxDevice, renderer->commandBuffer, renderer->bloomPass, &blitAction);
            gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &blurDrawCall);
            gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);
        }

        /*
        {   // bloom blit
            blitAction.colors[0].action = gfx::Action::ACTION_LOAD;

            gfx::DrawCall blitDrawCall;
            blitDrawCall.pipelineState = renderer->blitPipeline;
            blitDrawCall.numElements = 4;
    
            blitDrawCall.psImageInputs[0] = renderer->bloomRT;
            gfx::BeginRenderPass(renderer->gfxDevice, renderer->commandBuffer, renderer->bloomBlitPass, &blitAction);
            gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &blitDrawCall);
            gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);
        }*/

        {   // tonemapping
            blitAction.colors[0].action = gfx::Action::ACTION_CLEAR;

            gfx::DrawCall tonemapDrawCall;
            tonemapDrawCall.pipelineState = renderer->tonemapPipeline;
            tonemapDrawCall.numElements = 4;
            tonemapDrawCall.psImageInputs[0] = renderer->mainRT;

            gfx::BeginDefaultRenderPass(renderer->gfxDevice, renderer->commandBuffer, swapChain, &blitAction);
            gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &tonemapDrawCall);
            gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);


            blitAction.colors[0].action = gfx::Action::ACTION_LOAD;
        }

        {   // UI blur
            gfx::DrawCall blurDrawCall;
            blurDrawCall.pipelineState = renderer->blurPipeline;
            blurDrawCall.numElements = 4;
            blurDrawCall.psImageInputs[0] = renderer->mainRT;
            blurDrawCall.psImageInputs[1] = renderer->uiRenderTarget;

            gfx::BeginDefaultRenderPass(renderer->gfxDevice, renderer->commandBuffer, swapChain, &blitAction);
            gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &blurDrawCall);
            gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);
        }

        {   // UI blit

            gfx::DrawCall blitDrawCall;
            blitDrawCall.pipelineState = renderer->blitPipeline;
            blitDrawCall.numElements = 4;
            blitDrawCall.psImageInputs[0] = renderer->mainRT;

            blitDrawCall.psImageInputs[0] = renderer->uiRenderTarget;
            gfx::BeginDefaultRenderPass(renderer->gfxDevice, renderer->commandBuffer, swapChain, &blitAction);
            gfx::SubmitDrawCall(renderer->gfxDevice, renderer->commandBuffer, &blitDrawCall);
            gfx::EndRenderPass(renderer->gfxDevice, renderer->commandBuffer);
        }


        frameIndex++;
    }


    size_t* GetActiveCubemap(RenderWorld* world)
    {
        return &world->renderer->activeCubemap;
    }
        
}


bool renderer_get_interface(renderer::RendererInterface* outInterface)
{
    outInterface->CreateRenderWorld = &renderer::CreateRenderWorld;
    outInterface->DestroyRenderWorld = &renderer::DestroyRenderWorld;

    outInterface->SerializeRenderWorld = &renderer::SerializeRenderWorld;
    outInterface->DeserializeRenderWorld = &renderer::DeserializeRenderWorld;

    outInterface->UpdateMeshLibrary = &renderer::UpdateMeshLibrary;
    outInterface->UpdateTextureLibrary = &renderer::UpdateTextureLibrary;
    outInterface->UpdateMaterialLibrary = &renderer::UpdateMaterialLibrary;
    outInterface->CreateStaticMesh = &renderer::CreateStaticMesh;
    outInterface->DestroyStaticMesh = &renderer::DestroyStaticMesh;

    outInterface->GetStaticMesh = &renderer::GetStaticMesh;
    outInterface->CopyStaticMesh = &renderer::CopyStaticMesh;

    outInterface->GetMaterials = &renderer::GetMaterials;
    outInterface->GetMeshAsset = &renderer::GetMeshAsset;

    outInterface->Render = &renderer::Render;
    outInterface->RenderUI = &renderer::RenderUI;

    outInterface->GetTextureHandle = &renderer::GetTextureHandle;
    
    outInterface->CreateRenderer = &renderer::CreateRenderer;
    outInterface->DestroyRenderer = &renderer::DestroyRenderer;

    outInterface->SetCameraTransform = &renderer::SetCameraTransform;
    outInterface->SetCameraProjection = &renderer::SetCameraProjection;
    outInterface->GetCameraTransform = &renderer::GetCameraTransform;
    outInterface->GetCameraProjection = &renderer::GetCameraProjection;

    outInterface->UpdateWorldState = &renderer::UpdateWorldState;

    outInterface->GetActiveCubemap = &renderer::GetActiveCubemap;

    return true;
}