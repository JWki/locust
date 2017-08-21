#include "../gfx.h"
#include <foundation/memory/memory.h>
#include <foundation/memory/allocators.h>
#include <cassert>  // @TODO: override

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#define HANDLE_INDEX(handle)        (uint16_t)(handle)
#define HANDLE_GENERATION(handle)   (uint16_t)(handle >> 16)

#define HANDLE_GENERATION_START 1

#define MAKE_HANDLE(index, generation) (uint32_t)(((uint32_t)generation) << 16 | index); 

namespace gfx
{
    struct D3D11Buffer 
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation  = HANDLE_GENERATION_START;
        _ResourceState  resState    = _ResourceState::STATE_EMPTY;

        BufferDesc desc;
        ID3D11Buffer*   buffer      = nullptr;
    };

    struct D3D11Image
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = HANDLE_GENERATION_START;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        ImageDesc       desc;
        
        ID3D11ShaderResourceView*   srv = nullptr;
        ID3D11RenderTargetView*     rtv = nullptr;
        ID3D11DepthStencilView*     dsv = nullptr;
        ID3D11SamplerState*         sampler;    // @TODO split this out into own resource 

        union {
            ID3D11Texture2D*    as_2DTexture;
            ID3D11Texture2D*    as_cubeTexture;     // @TODO: merge w/2D texture?
            ID3D11Texture3D*    as_3DTexture;
            // @TODO: ARRAY TEXTURES
        };

        // @TODO: cubemaps
    };



    struct D3D11Shader
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = HANDLE_GENERATION_START;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        ShaderDesc      desc;
        // @NOTE
        union {
            ID3D11VertexShader* as_vertexShader;
            ID3D11PixelShader* as_pixelShader;
            ID3D11GeometryShader* as_geometryShader;
            ID3D11HullShader* as_hullShader;
            ID3D11DomainShader* as_domainShader;
        };
    };

    struct D3D11PipelineState
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = HANDLE_GENERATION_START;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;
        
        PipelineStateDesc desc;

        ID3D11InputLayout* inputLayout  = nullptr;

        ID3D11BlendState*           blendState          = nullptr;
        uint8_t                     blendWriteMask      = 0;
        float                       blendColor[4];

        ID3D11RasterizerState*      rasterizerState     = nullptr;
        ID3D11DepthStencilState*    depthStencilState   = nullptr;

        D3D11Shader*    vertexShader    = nullptr;
        D3D11Shader*    pixelShader     = nullptr;
        D3D11Shader*    geometryShader  = nullptr;
        D3D11Shader*    hullShader      = nullptr;
        D3D11Shader*    domainShader    = nullptr;
        // @TODO
    };


    struct D3D11RenderPass
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = HANDLE_GENERATION_START;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        RenderPassDesc  desc;

        // @HACK only needed for default render passes (swap chain render passes)
        // @TODO make this proper so a swap chain actually contains a fully formed image resource object
        ID3D11RenderTargetView* backbuffer = nullptr;
        ID3D11DepthStencilView* depthBuffer = nullptr;
        uint32_t        width = 0;;
        uint32_t        height = 0;
        // @TODO
    };

    struct D3D11CommandBuffer
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = HANDLE_GENERATION_START;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        ID3D11DeviceContext*    d3dDC = nullptr;

        D3D11RenderPass*        renderPass = nullptr;
        // @TODO
    };

    struct D3D11SwapChain
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = HANDLE_GENERATION_START;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        SwapChainDesc   desc;

        ID3D11Texture2D*            depthBuffer         = nullptr;
        IDXGISwapChain*             swapChain           = nullptr;
        ID3D11RenderTargetView*     rtv                 = nullptr;
        ID3D11DepthStencilView*     dsv                 = nullptr;
        D3D11RenderPass*            defaultRenderPass   = nullptr;
        // @TODO
    };


    void D3D11ReleaseResource(D3D11Buffer* buffer)
    {
        if (buffer->buffer != nullptr) {
            buffer->buffer->Release();
        }
    }

    void D3D11ReleaseResource(D3D11Shader* shader)
    {
        if (shader->as_vertexShader == nullptr) { return; }
        switch (shader->desc.type) {
            case ShaderType::SHADER_TYPE_VS:
                shader->as_vertexShader->Release();
                break;
            case ShaderType::SHADER_TYPE_PS:
                shader->as_vertexShader->Release();
                break;
            case ShaderType::SHADER_TYPE_GS:
                shader->as_vertexShader->Release();
                break;
            case ShaderType::SHADER_TYPE_HS:
                shader->as_vertexShader->Release();
                break;
            case ShaderType::SHADER_TYPE_DS:
                shader->as_vertexShader->Release();
                break;
        }
    }

    void D3D11ReleaseResource(D3D11Image* image)
    {
        // @TODO don't leak the resource here
    }

    void D3D11ReleaseResource(D3D11RenderPass* renderPass)
    {
        // no-op, we hold no d3d11 resources
    }

    void D3D11ReleaseResource(D3D11SwapChain* swapChain)
    {
        if (swapChain->swapChain != nullptr) {
            swapChain->swapChain->Release();
        }
    }

    template <class TResource> 
    struct ResourcePool
    {
        uint32_t    size        = 0;
        TResource*  buffer      = nullptr;
        uint16_t*   indexList   = nullptr;
        uint32_t    indexListHead   = 0;
        uint32_t    indexListTail   = 0;

        void Initialize(uint32_t bufferSize, fnd::memory::MemoryArenaBase* memoryArena)
        {
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
            res->resState = _ResourceState::STATE_ALLOC;
            *resource = res;
            *id = MAKE_HANDLE(index, res->generation);
            return true;
        }

        void Free(uint32_t id)
        {
            uint16_t index = HANDLE_INDEX(id);
            TResource* res = &buffer[index];
            assert(res->generation == HANDLE_GENERATION(id));
            D3D11ReleaseResource(res);
            
            res->generation++;
            res->resState = _ResourceState::STATE_EMPTY;

            ReleaseIndex(index);
        }

        TResource* Get(uint32_t id)
        {
            uint16_t index = HANDLE_INDEX(id);
            TResource* res = &buffer[index];
            assert(res->generation == HANDLE_GENERATION(id));
            return res;
        }
    };

    struct Interface
    {
        fnd::memory::MemoryArenaBase* memoryArena = nullptr;

        ResourcePool<D3D11Buffer>           bufferPool;
        ResourcePool<D3D11Image>            imagePool;
        ResourcePool<D3D11PipelineState>    pipelineStatePool;
        ResourcePool<D3D11Shader>           shaderPool;
        ResourcePool<D3D11RenderPass>       passPool;
        ResourcePool<D3D11CommandBuffer>    cmdBufferPool;
        ResourcePool<D3D11SwapChain>        swapChainPool;

        Device*     deviceList = nullptr;
        uint32_t    numDevices = 0;

        IDXGIFactory1*  idxgiFactory = nullptr;
    };

    struct Device
    {
        Interface*              interf = nullptr;
        DeviceInfo              info;
        IDXGIAdapter1*          dxgiAdapter = nullptr;
        ID3D11Device*           d3dDevice   = nullptr;
        ID3D11DeviceContext*    d3dDC       = nullptr;
        CommandBuffer           dcAsCmdBuffer;

        PipelineState           pipelineStateCache;
    };


    bool CreateInterface(Interface** outInterface, InterfaceDesc* desc, fnd::memory::MemoryArenaBase* memoryArena)
    {
        Interface* interf = GT_NEW(Interface, memoryArena);
        *outInterface = interf;

        interf->memoryArena = memoryArena;
        
        /* Build device list */
        interf->deviceList = GT_NEW_ARRAY(Device, desc->maxNumDevices, memoryArena);

        HRESULT res = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&interf->idxgiFactory);
        if (FAILED(res)) {
            // @TODO add logging, error code, whatever
            return false;
        }
        IDXGIAdapter1* adapter = nullptr;
        for (int i = 0; interf->idxgiFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            auto& device = interf->deviceList[i];
            device.dxgiAdapter = adapter;
            device.info.index = i;
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            memset(device.info.friendlyName, 0x0, GFX_DEVICE_INFO_NAME_LEN);
            size_t foo = 0;
            wcstombs_s(&foo, device.info.friendlyName, desc.Description, 128);
            device.interf = interf;
            interf->numDevices++;
        }
        
        /* Initialize resource pools*/
        interf->bufferPool.Initialize(desc->bufferPoolSize, memoryArena);
        interf->imagePool.Initialize(desc->imagePoolSize, memoryArena);
        interf->pipelineStatePool.Initialize(desc->pipelinePoolSize, memoryArena);
        interf->shaderPool.Initialize(desc->shaderPoolSize, memoryArena);
        interf->passPool.Initialize(desc->renderPassPoolSize, memoryArena);
        interf->cmdBufferPool.Initialize(desc->cmdBufferPoolSize, memoryArena);
        interf->swapChainPool.Initialize(desc->maxNumSwapChains, memoryArena);


        return true;
    }

    void EnumerateDevices(Interface* interf, DeviceInfo* outInfo, uint32_t* numDevices)
    {
        *numDevices = interf->numDevices;
        for (uint32_t i = 0; i < *numDevices; ++i) {
            outInfo[i] = interf->deviceList[i].info;
        }
    }

    Device* GetDevice(Interface* interf, uint32_t index)
    {
        if (interf->deviceList[index].dxgiAdapter == nullptr) { return nullptr; }

        Device* device = &interf->deviceList[index];

        /* Device creation */
        UINT deviceFlags = 0;
#ifdef GT_DEVELOPMENT
        deviceFlags |= D3D11_CREATE_DEVICE_FLAG::D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevels = { D3D_FEATURE_LEVEL_11_0 };
        HRESULT res = D3D11CreateDevice(device->dxgiAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, deviceFlags, NULL, 0, D3D11_SDK_VERSION, &device->d3dDevice, NULL, &device->d3dDC);
        if (FAILED(res)) {
            // @TODO log things?
            return nullptr;
        }

        D3D11CommandBuffer* immediateBuffer;
        assert(interf->cmdBufferPool.Allocate(&immediateBuffer, &device->dcAsCmdBuffer.id));
        immediateBuffer->associatedDevice = device;
        immediateBuffer->d3dDC = device->d3dDC;
        immediateBuffer->resState = _ResourceState::STATE_VALID;

        return device;
    }

    CommandBuffer GetImmediateCommandBuffer(Device* device)
    {
        return device->dcAsCmdBuffer;
    }

    //
    //


    //
    //

    D3D11_USAGE g_resUsageTable[] = {
        D3D11_USAGE::D3D11_USAGE_DEFAULT,
        D3D11_USAGE::D3D11_USAGE_IMMUTABLE,
        D3D11_USAGE::D3D11_USAGE_DYNAMIC,
        D3D11_USAGE::D3D11_USAGE_DYNAMIC,   // @NOTE D3d11 has no streaming buffers so no distiction here
        D3D11_USAGE::D3D11_USAGE_STAGING
    };

    Buffer CreateBuffer(Device* device, BufferDesc* desc)
    {
        gfx::D3D11Buffer* buffer = nullptr;
        Buffer result{ gfx::INVALID_ID };
       
        if (!device->interf->bufferPool.Allocate(&buffer, &result.id)) {
            return { gfx::INVALID_ID };
        }

        ResourceUsage usage = desc->usage == ResourceUsage::_DEFAULT ? ResourceUsage::USAGE_IMMUTABLE : desc->usage;

        D3D11_BUFFER_DESC d3d11Desc;
        ZeroMemory(&d3d11Desc, sizeof(d3d11Desc));
        d3d11Desc.ByteWidth = (UINT)desc->byteWidth;
        d3d11Desc.Usage = g_resUsageTable[(uint8_t)usage];
        if (usage != ResourceUsage::USAGE_IMMUTABLE) {
            d3d11Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
        if (usage == ResourceUsage::USAGE_STAGING) {
            d3d11Desc.CPUAccessFlags |= D3D11_CPU_ACCESS_READ;
        }
        switch (desc->type) {
            case BufferType::BUFFER_TYPE_VERTEX:
                d3d11Desc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER;
            break;
            case BufferType::BUFFER_TYPE_INDEX:
                d3d11Desc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_INDEX_BUFFER;
            break;
            case BufferType::BUFFER_TYPE_CONSTANT:
                d3d11Desc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER;
            break;
            default:
                d3d11Desc.BindFlags = 0;
            break;
        }


        D3D11_SUBRESOURCE_DATA* initialDataPtr = nullptr;
        D3D11_SUBRESOURCE_DATA initialData;
        if (desc->initialData != nullptr) {
            ZeroMemory(&initialData, sizeof(initialData));
            initialData.pSysMem = desc->initialData;
            initialDataPtr = &initialData;
        }
        else {
            if (usage == ResourceUsage::USAGE_IMMUTABLE) {
                // @TODO: logging
                device->interf->bufferPool.Free(result.id);
                return { gfx::INVALID_ID };
            }
        }
        HRESULT res = device->d3dDevice->CreateBuffer(&d3d11Desc, initialDataPtr, &buffer->buffer);
        if (FAILED(res)) {
            // @TODO: logging/error code
            device->interf->bufferPool.Free(result.id);
            return { gfx::INVALID_ID };
        }

        buffer->associatedDevice = device;
        buffer->desc = *desc;
        buffer->resState = _ResourceState::STATE_VALID;

        return result;
    }

    Shader CreateShader(Device* device, ShaderDesc* desc)
    {
        gfx::D3D11Shader* shader = nullptr;
        Shader result{ gfx::INVALID_ID };
        if (!device->interf->shaderPool.Allocate(&shader, &result.id)) {
            return { gfx::INVALID_ID };
        }
        HRESULT res = S_OK;
        switch (desc->type) {
        case ShaderType::SHADER_TYPE_VS:
            res = device->d3dDevice->CreateVertexShader(desc->code, (size_t)desc->codeSize, nullptr, &shader->as_vertexShader);
            break;
        case ShaderType::SHADER_TYPE_PS:
            res = device->d3dDevice->CreatePixelShader(desc->code, (size_t)desc->codeSize, nullptr, &shader->as_pixelShader);
            break;
        case ShaderType::SHADER_TYPE_GS:
            res = device->d3dDevice->CreateGeometryShader(desc->code, (size_t)desc->codeSize, nullptr, &shader->as_geometryShader);
            break;
        case ShaderType::SHADER_TYPE_HS:
            res = device->d3dDevice->CreateHullShader(desc->code, (size_t)desc->codeSize, nullptr, &shader->as_hullShader);
            break;
        case ShaderType::SHADER_TYPE_DS:
            res = device->d3dDevice->CreateDomainShader(desc->code, (size_t)desc->codeSize, nullptr, &shader->as_domainShader);
            break;
        }
        if (FAILED(res)) {
            device->interf->shaderPool.Free(result.id);
            return { gfx::INVALID_ID };
        }
        shader->associatedDevice = device;
        shader->desc = *desc;
        return result;
    }


    DXGI_FORMAT g_pixelFormatTable[] = {
        DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT::DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UINT,
        DXGI_FORMAT::DXGI_FORMAT_R16G16B16A16_UINT,
        DXGI_FORMAT::DXGI_FORMAT_D32_FLOAT_S8X24_UINT
    };

    UINT g_pixelFormatComponentCount[] = {
        4, 4, 4, 4, 4, 4
    };

    UINT g_pixelFormatComponentSize[] = {
        1, 1, 1, 2, 1, 2
    };

    D3D11_SRV_DIMENSION g_imageSRVDimensionTable[] = {
        D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D,
        D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D,
        D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURECUBE,
        D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE3D,
        D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2DARRAY
    };

    D3D11_RTV_DIMENSION g_imageRTVDimensionTable[] = {
        D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D,
        D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2D,
        D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_UNKNOWN,   // @NOTE
        D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE3D,
        D3D11_RTV_DIMENSION::D3D11_RTV_DIMENSION_TEXTURE2DARRAY
    };

    D3D11_TEXTURE_ADDRESS_MODE g_imageWrapModeTable[] = {
        D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP,
        D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_WRAP,
        D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP,
        D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_MIRROR
    };

    D3D11_FILTER g_minLinearFilterModes[] = {
        D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        D3D11_FILTER::D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT,
        D3D11_FILTER::D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT,
        D3D11_FILTER::D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
        D3D11_FILTER::D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
        D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_LINEAR
    };

    D3D11_FILTER g_minNearestFilterModes[] = {
        D3D11_FILTER::D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR,
        D3D11_FILTER::D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR,
        D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_POINT,
        D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_POINT,
        D3D11_FILTER::D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR,
        D3D11_FILTER::D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
        D3D11_FILTER::D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR
    };

    D3D11_FILTER* g_filterModeTableFront[] = {
        g_minLinearFilterModes,
        g_minLinearFilterModes,
        g_minNearestFilterModes
    };

    Image CreateImage(Device* device, ImageDesc* desc)
    {
        D3D11Image* image = nullptr;
        Image result{ gfx::INVALID_ID };
        if (!device->interf->imagePool.Allocate(&image, &result.id)) {
            return { gfx::INVALID_ID };
        }
        
        D3D11_TEXTURE2D_DESC texDesc;
        texDesc.Width = desc->width;
        texDesc.Height = desc->height;
        texDesc.MipLevels = desc->numMipmaps;
        texDesc.ArraySize = 1;    // @TODO
        texDesc.Format = g_pixelFormatTable[(uint8_t)desc->pixelFormat];

        ResourceUsage usage = desc->usage == ResourceUsage::_DEFAULT ? ResourceUsage::USAGE_IMMUTABLE : desc->usage;

        texDesc.CPUAccessFlags = 0;
        if (usage != ResourceUsage::USAGE_IMMUTABLE) {
            texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        }
        if (usage == ResourceUsage::USAGE_STAGING) {
            texDesc.CPUAccessFlags |= D3D11_CPU_ACCESS_READ;
        }
        texDesc.SampleDesc.Count = 1;   // @TODO: Sample count
        texDesc.SampleDesc.Quality = 0;

        texDesc.Usage = g_resUsageTable[(uint8_t)usage];
        if (!desc->isDepthStencilTarget) {
            texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        }
        else {
            texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            texDesc.Usage = D3D11_USAGE_DEFAULT;
        }
        if (desc->isRenderTarget) {
            texDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
            texDesc.Usage = D3D11_USAGE_DEFAULT;
            // @TODO: specify if bindable as depth target?
        }

        texDesc.MiscFlags = 0;
        if (desc->numMipmaps > 1) {
            texDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
        }
        if (desc->type == ImageType::IMAGE_TYPE_CUBE) {
            texDesc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
        }

        size_t numDataItems = desc->numDataItems;
        if (desc->type == ImageType::IMAGE_TYPE_CUBE || true) {     // @HACK
            assert(numDataItems <= 6);
        }
        D3D11_SUBRESOURCE_DATA pData[6];    // @TODO: Support more than 6?
        D3D11_SUBRESOURCE_DATA* pDataPtr = numDataItems > 0 ? &pData[0] : nullptr;
        UINT numComponents = g_pixelFormatComponentCount[(uint8_t)desc->pixelFormat];
        UINT componentSize = g_pixelFormatComponentSize[(uint8_t)desc->pixelFormat];
        for (int i = 0; i < numDataItems; ++i) {
            pData[i].pSysMem = desc->initialData[i];
            pData[i].SysMemPitch = desc->width * numComponents * componentSize;
            pData[i].SysMemSlicePitch = pData[i].SysMemPitch * desc->height;  // @TODO: Verify?
        }
        assert((desc->isRenderTarget || desc->isDepthStencilTarget) || !(usage == ResourceUsage::USAGE_IMMUTABLE && pDataPtr == nullptr));
        
        // Create the texture
        HRESULT res = S_OK;
        switch (desc->type) {
        case ImageType::IMAGE_TYPE_2D: {
            res = device->d3dDevice->CreateTexture2D(&texDesc, pDataPtr, &image->as_2DTexture);
        } break;
        default: {
                assert(false);  // @TODO
            } break;
        }
        if (FAILED(res)) {
            device->interf->imagePool.Free(result.id);
            return { gfx::INVALID_ID };
        }
        
        // Create a shader resource view if we're not a depth stencil buffer
        if (!desc->isDepthStencilTarget) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
            ZeroMemory(&srvDesc, sizeof(srvDesc));
            srvDesc.Format = texDesc.Format;
            srvDesc.ViewDimension = g_imageSRVDimensionTable[(uint8_t)desc->type];
            switch (desc->type) {
            case ImageType::IMAGE_TYPE_2D: {
                srvDesc.Texture2D.MipLevels = desc->numMipmaps;
                srvDesc.Texture2D.MostDetailedMip = 0;
            } break;
            default: {
                assert(false);  // @TODO
            } break;
            }
            // @TODO: avoid aliasing the I3D11TextureXXX union here
            res = device->d3dDevice->CreateShaderResourceView(image->as_2DTexture, &srvDesc, &image->srv);
            if (FAILED(res)) {
                device->interf->imagePool.Free(result.id);
                return { gfx::INVALID_ID };
            }

            image->sampler = nullptr;
            if (desc->samplerDesc != nullptr) {
                // Create a sampler @HACK expose samplers to gfx API later
                D3D11_SAMPLER_DESC samplerDesc;
                ZeroMemory(&samplerDesc, sizeof(samplerDesc));
                samplerDesc.AddressU = g_imageWrapModeTable[(uint8_t)desc->samplerDesc->wrapU];
                samplerDesc.AddressV = g_imageWrapModeTable[(uint8_t)desc->samplerDesc->wrapV];
                samplerDesc.AddressW = g_imageWrapModeTable[(uint8_t)desc->samplerDesc->wrapW];
                samplerDesc.MinLOD = 0.0f;
                samplerDesc.MaxLOD = 0.0f;
                samplerDesc.MipLODBias = 0.0f;
                samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
                samplerDesc.Filter = g_filterModeTableFront[(uint8_t)desc->samplerDesc->minFilter][(uint8_t)desc->samplerDesc->magFilter];
                res = device->d3dDevice->CreateSamplerState(&samplerDesc, &image->sampler);
                if (FAILED(res)) {
                    device->interf->imagePool.Free(result.id);
                    return { gfx::INVALID_ID };
                }
            }

        }
        // Create a render target view if we need to
        if (desc->isRenderTarget) {
            D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc;
            ZeroMemory(&render_target_view_desc, sizeof(render_target_view_desc));
            render_target_view_desc.Format = texDesc.Format;
            render_target_view_desc.ViewDimension = g_imageRTVDimensionTable[(uint8_t)desc->type];
            switch (desc->type) {
            case ImageType::IMAGE_TYPE_2D: {
                render_target_view_desc.Texture2D.MipSlice = 0;
            } break;
            default:
                assert(false);
                break;
            }
            res = device->d3dDevice->CreateRenderTargetView(image->as_2DTexture, &render_target_view_desc, &image->rtv);
            if (FAILED(res)) {
                device->interf->imagePool.Free(result.id);
                return { gfx::INVALID_ID };
            }
        }

        image->associatedDevice = device;
        image->resState = _ResourceState::STATE_VALID;
        image->desc = *desc;
        image->desc.usage = usage;
        image->desc.numDataItems = numDataItems;
        return result;
    }


    DXGI_FORMAT g_vertexFormatTable[] = {
        DXGI_FORMAT::DXGI_FORMAT_UNKNOWN,
        DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT,
        DXGI_FORMAT::DXGI_FORMAT_R32G32B32_FLOAT,
        DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT,

        DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM
    };

    D3D11_PRIMITIVE_TOPOLOGY g_primitiveTypeTable[] = {
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
    };

    D3D11_BLEND g_blendFactorTable[] = {
        D3D11_BLEND_ZERO,
        D3D11_BLEND_ONE,
        D3D11_BLEND_SRC_COLOR,
        D3D11_BLEND_INV_SRC_COLOR,
        D3D11_BLEND_SRC_ALPHA,
        D3D11_BLEND_INV_SRC_ALPHA,
        D3D11_BLEND_DEST_ALPHA,
        D3D11_BLEND_INV_DEST_ALPHA,
        D3D11_BLEND_DEST_COLOR,
        D3D11_BLEND_INV_DEST_COLOR,
        D3D11_BLEND_SRC_ALPHA_SAT,
        D3D11_BLEND_BLEND_FACTOR,
        D3D11_BLEND_INV_BLEND_FACTOR,
        D3D11_BLEND_SRC1_COLOR,
        D3D11_BLEND_INV_SRC1_COLOR,
        D3D11_BLEND_SRC1_ALPHA,
        D3D11_BLEND_INV_SRC1_ALPHA
    };

    D3D11_BLEND_OP g_blendOpTable[] = {
        D3D11_BLEND_OP_ADD,
        D3D11_BLEND_OP_SUBTRACT,
        D3D11_BLEND_OP_REV_SUBTRACT,
        D3D11_BLEND_OP_MIN,
        D3D11_BLEND_OP_MAX
    };


    D3D11_FILL_MODE g_fillModeTable[] = {
        D3D11_FILL_WIREFRAME,
        D3D11_FILL_SOLID
    };

    D3D11_CULL_MODE g_cullModeTable[] = {
        D3D11_CULL_NONE, 
        D3D11_CULL_FRONT,
        D3D11_CULL_BACK
    };

    BOOL g_cullOrderCClockwise[] = {
        FALSE, TRUE
    };


    D3D11_COMPARISON_FUNC g_compareFuncTable[] = {
        D3D11_COMPARISON_NEVER,
        D3D11_COMPARISON_LESS,
        D3D11_COMPARISON_EQUAL,
        D3D11_COMPARISON_LESS_EQUAL,
        D3D11_COMPARISON_GREATER,
        D3D11_COMPARISON_NOT_EQUAL,
        D3D11_COMPARISON_GREATER_EQUAL,
        D3D11_COMPARISON_ALWAYS
    };

    D3D11_DEPTH_WRITE_MASK g_depthWriteMaskTable[] = {
        D3D11_DEPTH_WRITE_MASK_ZERO,
        D3D11_DEPTH_WRITE_MASK_ALL
    };

    D3D11_STENCIL_OP g_stencilOpTable[] = {
        D3D11_STENCIL_OP_KEEP,
        D3D11_STENCIL_OP_ZERO,
        D3D11_STENCIL_OP_REPLACE,
        D3D11_STENCIL_OP_INCR_SAT,
        D3D11_STENCIL_OP_DECR_SAT,
        D3D11_STENCIL_OP_INVERT,
        D3D11_STENCIL_OP_INCR,
        D3D11_STENCIL_OP_DECR
    };

    ID3D11BlendState* D3D11CreateBlendState(ID3D11Device* device, BlendStateDesc* descIn)
    {
        D3D11_BLEND_DESC desc;
        desc.AlphaToCoverageEnable = descIn->alphaToCoverage;
        desc.IndependentBlendEnable = false;
        for (int i = 0; i < 8; ++i) {
            desc.RenderTarget[i].BlendEnable = descIn->enableBlend;
            desc.RenderTarget[i].BlendOp = g_blendOpTable[(uint8_t)descIn->blendOp];
            desc.RenderTarget[i].BlendOpAlpha = g_blendOpTable[(uint8_t)descIn->blendOpAlpha];
            desc.RenderTarget[i].DestBlend = g_blendFactorTable[(uint8_t)descIn->dstBlend];
            desc.RenderTarget[i].DestBlendAlpha = g_blendFactorTable[(uint8_t)descIn->dstBlendAlpha];
            desc.RenderTarget[i].SrcBlend = g_blendFactorTable[(uint8_t)descIn->srcBlend];
            desc.RenderTarget[i].SrcBlendAlpha = g_blendFactorTable[(uint8_t)descIn->srcBlendAlpha];
            desc.RenderTarget[i].RenderTargetWriteMask = descIn->writeMask;
        }
        ID3D11BlendState* out = nullptr;
        HRESULT res = device->CreateBlendState(&desc, &out);
        if (FAILED(res)) {
            return nullptr;
        }
        return out;
    }

    ID3D11RasterizerState* D3D11CreateRasterState(ID3D11Device* device, RasterizerStateDesc* descIn)
    {
        D3D11_RASTERIZER_DESC desc;
        desc.AntialiasedLineEnable = descIn->enableAALine;
        desc.CullMode = g_cullModeTable[(uint8_t)descIn->cullMode];
        desc.DepthBias = descIn->depthBias;
        desc.DepthBiasClamp = descIn->depthBiasClamp;
        desc.SlopeScaledDepthBias = descIn->slopeScaledDepthBias;
        desc.DepthClipEnable = descIn->enableDepthClip;
        desc.FillMode = g_fillModeTable[(uint8_t)descIn->fillMode];
        desc.FrontCounterClockwise = g_cullOrderCClockwise[(uint8_t)descIn->cullOrder];
        desc.MultisampleEnable = descIn->enableMultisample;
        desc.ScissorEnable = descIn->enableScissor;
        ID3D11RasterizerState* out = nullptr;
        HRESULT res = device->CreateRasterizerState(&desc, &out);
        if (FAILED(res)) {
            return nullptr;
        }
        return out;
    }

    ID3D11DepthStencilState* D3D11CreateDepthStencilState(ID3D11Device* device, DepthStencilStateDesc* descIn)
    {
        D3D11_DEPTH_STENCIL_DESC desc;
        
        {   // fill out front face op desc
            D3D11_DEPTH_STENCILOP_DESC opDesc;
            opDesc.StencilDepthFailOp = g_stencilOpTable[(uint8_t)descIn->frontFace.stencilDepthFailOp];
            opDesc.StencilFailOp = g_stencilOpTable[(uint8_t)descIn->frontFace.stencilFailOp];
            opDesc.StencilPassOp = g_stencilOpTable[(uint8_t)descIn->frontFace.stencilPassOp];
            opDesc.StencilFunc = g_compareFuncTable[(uint8_t)descIn->frontFace.stencilFunc];
            desc.FrontFace = opDesc;
        }
        {   // fill out back face op desc
            D3D11_DEPTH_STENCILOP_DESC opDesc;
            opDesc.StencilDepthFailOp = g_stencilOpTable[(uint8_t)descIn->backFace.stencilDepthFailOp];
            opDesc.StencilFailOp = g_stencilOpTable[(uint8_t)descIn->backFace.stencilFailOp];
            opDesc.StencilPassOp = g_stencilOpTable[(uint8_t)descIn->backFace.stencilPassOp];
            opDesc.StencilFunc = g_compareFuncTable[(uint8_t)descIn->backFace.stencilFunc];
            desc.BackFace = opDesc;
        }

        desc.DepthEnable = descIn->enableDepth;
        desc.StencilEnable = descIn->enableStencil;
        desc.DepthWriteMask = g_depthWriteMaskTable[(uint8_t)descIn->depthWriteMask];
        desc.StencilReadMask = descIn->stencilReadMask;
        desc.StencilWriteMask = descIn->stencilWriteMask;
        desc.DepthFunc = g_compareFuncTable[(uint8_t)descIn->depthFunc];
        
        ID3D11DepthStencilState* out = nullptr;
        HRESULT res = device->CreateDepthStencilState(&desc, &out);
        if (FAILED(res)) {
            return nullptr;
        }
        return out;
    }

    PipelineState CreatePipelineState(Device* device, PipelineStateDesc* desc)
    {
        gfx::D3D11PipelineState* state = nullptr;
        PipelineState result{ gfx::INVALID_ID };
        if (!device->interf->pipelineStatePool.Allocate(&state, &result.id)) {
            return { gfx::INVALID_ID };
        }

        state->associatedDevice = device;
        state->desc = *desc;
        if (state->desc.primitiveType == PrimitiveType::_DEFAULT) {
            state->desc.primitiveType = PrimitiveType::PRIMITIVE_TYPE_TRIANGLES;
        }
        
        state->blendState = D3D11CreateBlendState(device->d3dDevice, &desc->blendState);
        state->blendWriteMask = desc->blendState.writeMask;
        memcpy(state->blendColor, desc->blendState.color, sizeof(float) * 4);
        state->rasterizerState = D3D11CreateRasterState(device->d3dDevice, &desc->rasterState);
        state->depthStencilState = D3D11CreateDepthStencilState(device->d3dDevice, &desc->depthStencilState);

        state->vertexShader = device->interf->shaderPool.Get(desc->vertexShader.id);
        state->pixelShader = device->interf->shaderPool.Get(desc->pixelShader.id);
        if (GFX_CHECK_RESOURCE(desc->geometryShader)) {
            state->geometryShader = device->interf->shaderPool.Get(desc->geometryShader.id);
        }
        if (GFX_CHECK_RESOURCE(desc->hullShader)) {
            state->hullShader = device->interf->shaderPool.Get(desc->hullShader.id);
        }
        if (GFX_CHECK_RESOURCE(desc->domainShader)) {
            state->domainShader = device->interf->shaderPool.Get(desc->domainShader.id);
        }

        D3D11_INPUT_ELEMENT_DESC inputElements[GFX_MAX_VERTEX_ATTRIBS];
        UINT numInputElements = 0;
        for (UINT i = 0; i < GFX_MAX_VERTEX_ATTRIBS; ++i) {
            if (desc->vertexLayout.attribs[i].format != VertexFormat::VERTEX_FORMAT_INVALID) {
                auto& attrib = desc->vertexLayout.attribs[i];
                inputElements[i] = { attrib.name, attrib.index, g_vertexFormatTable[(uint8_t)attrib.format], attrib.slot, attrib.offset, D3D11_INPUT_PER_VERTEX_DATA, 0};
                numInputElements++;
            }
            else {
                break;
            }
        }
        if (numInputElements > 0) {
            HRESULT res = device->d3dDevice->CreateInputLayout(inputElements, numInputElements, state->vertexShader->desc.code, state->vertexShader->desc.codeSize, &state->inputLayout);
            if (FAILED(res)) {
                device->interf->pipelineStatePool.ReleaseIndex(result.id);
                return { gfx::INVALID_ID };
            }
        }
        else {
            state->inputLayout = nullptr;
        }
        return result;
    }

    SwapChain CreateSwapChain(Device* device, SwapChainDesc* desc)
    {
        D3D11SwapChain* swapChain;
        SwapChain result;
        if (!device->interf->swapChainPool.Allocate(&swapChain, &result.id)) {
            return { gfx::INVALID_ID };
        }

        DXGI_MODE_DESC backBufferDesc = {};
        backBufferDesc.Width = desc->width;
        backBufferDesc.Height = desc->height;
        backBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        DXGI_SAMPLE_DESC sampleDesc = {};
        sampleDesc.Count = 1;	// no multisampling

        DXGI_SWAP_CHAIN_DESC d3d11Desc = {};
        d3d11Desc.BufferCount = 1;  // @NOTE double buffering, ignore hint
        d3d11Desc.BufferDesc = backBufferDesc;
        d3d11Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d3d11Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        d3d11Desc.OutputWindow = (HWND)desc->window;
        d3d11Desc.SampleDesc = sampleDesc;
        d3d11Desc.Windowed = true;

        HRESULT res = device->interf->idxgiFactory->CreateSwapChain(device->d3dDevice, &d3d11Desc, &swapChain->swapChain);
        if (FAILED(res)) {
            device->interf->swapChainPool.Free(result.id);
            return { gfx::INVALID_ID };
        }

        // create render target view for this swapchain
        //  @TODO actually create an image object (as attachment) and expose that to the API user?
        ID3D11Texture2D* pBackBuffer;
        D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc; 
        ZeroMemory(&render_target_view_desc, sizeof(render_target_view_desc));
        render_target_view_desc.Format = d3d11Desc.BufferDesc.Format;
        render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        res = swapChain->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
        if (res != S_OK) {
            swapChain->swapChain->Release();
            device->interf->swapChainPool.Free(result.id);
            return { gfx::INVALID_ID };
        }
        res = device->d3dDevice->CreateRenderTargetView(pBackBuffer, &render_target_view_desc, &swapChain->rtv);

        // create depth stencil view for this swap chain
        D3D11_TEXTURE2D_DESC Desc;
        ZeroMemory(&Desc, sizeof(D3D10_TEXTURE2D_DESC));
        Desc.ArraySize = 1;
        Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;    // @TODO make configurable
        Desc.Width = desc->width;
        Desc.Height = desc->height;
        Desc.MipLevels = 1;
        Desc.SampleDesc.Count = 1;

        res = device->d3dDevice->CreateTexture2D(&Desc, NULL, &swapChain->depthBuffer);
        if (FAILED(res)) {
            swapChain->swapChain->Release();
            device->interf->swapChainPool.Free(result.id);
            return { gfx::INVALID_ID };
        }
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
        ZeroMemory(&dsvDesc, sizeof(dsvDesc));
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION::D3D11_DSV_DIMENSION_TEXTURE2D;
        res = device->d3dDevice->CreateDepthStencilView(swapChain->depthBuffer, &dsvDesc, &swapChain->dsv);
        if (FAILED(res)) {
            swapChain->swapChain->Release();
            swapChain->rtv->Release();
            device->interf->swapChainPool.Free(result.id);
            return { gfx::INVALID_ID };
        }

        pBackBuffer->Release();
        //  create a default render pass for this swap chain
        uint32_t id = 0;
        if(!device->interf->passPool.Allocate(&swapChain->defaultRenderPass, &id)) {
            swapChain->swapChain->Release();
            swapChain->rtv->Release();
            swapChain->dsv->Release();
            device->interf->swapChainPool.Free(result.id);
            return { gfx::INVALID_ID };
        }
        swapChain->defaultRenderPass->width = desc->width;
        swapChain->defaultRenderPass->height = desc->height;
        swapChain->defaultRenderPass->backbuffer = swapChain->rtv;

        swapChain->associatedDevice = device;
        swapChain->desc = *desc;
        swapChain->desc.bufferCountHint = 2;    // @NOTE double buffering is the only supported mode in this backend
        return result;
    }

    void ResizeSwapChain(Device* device, SwapChain handle, uint32_t width, uint32_t height)
    {
        D3D11SwapChain* swapChain = device->interf->swapChainPool.Get(handle.id);
        if (swapChain->rtv) {
            swapChain->rtv->Release();
            swapChain->rtv = nullptr;
        }

        swapChain->swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

        DXGI_SWAP_CHAIN_DESC desc;
        swapChain->swapChain->GetDesc(&desc);
        ID3D11Texture2D* pBackBuffer;
        D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc;
        ZeroMemory(&render_target_view_desc, sizeof(render_target_view_desc));
        render_target_view_desc.Format = desc.BufferDesc.Format;
        render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        HRESULT res = swapChain->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
        if (res != S_OK) {
            // @TODO error handling
        }
        assert(res == S_OK);
        res = device->d3dDevice->CreateRenderTargetView(pBackBuffer, &render_target_view_desc, &swapChain->rtv);
        pBackBuffer->Release();
    }

    RenderPass CreateRenderPass(Device* device, RenderPassDesc* desc)
    {
        D3D11RenderPass* renderPass;
        RenderPass result;
        if (!device->interf->passPool.Allocate(&renderPass, &result.id)) {
            return { gfx::INVALID_ID };
        }
        if (GFX_CHECK_RESOURCE(desc->depthStencilAttachment.image)) {
            D3D11Image* image = device->interf->imagePool.Get(desc->depthStencilAttachment.image.id);
            renderPass->width = image->desc.width;
            renderPass->height = image->desc.height;
            // create a depth stencil view
            D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
            ZeroMemory(&dsvDesc, sizeof(dsvDesc));
            dsvDesc.Format = g_pixelFormatTable[(uint8_t)image->desc.pixelFormat];
            dsvDesc.ViewDimension = D3D11_DSV_DIMENSION::D3D11_DSV_DIMENSION_TEXTURE2D;
            HRESULT res = device->d3dDevice->CreateDepthStencilView(image->as_2DTexture, &dsvDesc, &image->dsv);
            if (FAILED(res)) {
                device->interf->passPool.Free(result.id);
                return { gfx::INVALID_ID };
            }
        }
        if (GFX_CHECK_RESOURCE(desc->colorAttachments[0].image)) {
            D3D11Image* colorMain = device->interf->imagePool.Get(desc->colorAttachments[0].image.id);
            renderPass->width = colorMain->desc.width;
            renderPass->height = colorMain->desc.height;
        }
        renderPass->resState = _ResourceState::STATE_VALID;
        renderPass->associatedDevice = device;
        renderPass->desc = *desc;
        return result;
    }


    void BeginDefaultRenderPass(Device* device, CommandBuffer cmdBuffer, SwapChain swapChain, RenderPassAction* action)
    {
        D3D11SwapChain* swpCh = device->interf->swapChainPool.Get(swapChain.id);
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(cmdBuf->renderPass == nullptr);
        cmdBuf->renderPass = swpCh->defaultRenderPass;
        assert(cmdBuf->renderPass != nullptr);

        //cmdBuf->d3dDC->OMSetRenderTargets(1, &swpCh->rtv, nullptr);
        if (action->colors[0].action == Action::ACTION_CLEAR) {
            cmdBuf->d3dDC->ClearRenderTargetView(swpCh->rtv, action->colors[0].color);
            
        }
        if (swpCh->dsv != nullptr) {
            UINT dsClearFlags = 0;
            if (action->depth.action == Action::ACTION_CLEAR) {
                dsClearFlags |= D3D11_CLEAR_DEPTH;
            }
            if (action->stencil.action == Action::ACTION_CLEAR) {
                dsClearFlags |= D3D11_CLEAR_STENCIL;
            }
            cmdBuf->d3dDC->ClearDepthStencilView(swpCh->dsv, dsClearFlags, action->depth.value, action->stencil.value);
            cmdBuf->renderPass->depthBuffer = swpCh->dsv;
        }
    }

    void BeginRenderPass(Device* device, CommandBuffer cmdBuffer, RenderPass renderPass, RenderPassAction* action)
    {
        D3D11RenderPass* pass = device->interf->passPool.Get(renderPass.id);
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(cmdBuf->renderPass == nullptr);
        cmdBuf->renderPass = pass;
        assert(cmdBuf->renderPass != nullptr);
        // @TODO store D3D11Image pointers in render pass when creating it instead of looking them up here?
        // @NOTE actually no, this detects stale data
        for (int i = 0; i < GFX_MAX_COLOR_ATTACHMENTS; ++i) {
            if (!GFX_CHECK_RESOURCE(pass->desc.colorAttachments[i].image)) {
                break;
            }
            D3D11Image* colorAttachment = device->interf->imagePool.Get(pass->desc.colorAttachments[i].image.id);
            if (action->colors[0].action == Action::ACTION_CLEAR) {
                cmdBuf->d3dDC->ClearRenderTargetView(colorAttachment->rtv, action->colors[0].color);
            }
        }
        if (GFX_CHECK_RESOURCE(pass->desc.depthStencilAttachment.image)) {
            D3D11Image* depthAttachment = device->interf->imagePool.Get(pass->desc.depthStencilAttachment.image.id);
            UINT dsClearFlags = 0;
            if (action->depth.action == Action::ACTION_CLEAR) {
                dsClearFlags |= D3D11_CLEAR_DEPTH;
            }
            if (action->stencil.action == Action::ACTION_CLEAR) {
                dsClearFlags |= D3D11_CLEAR_STENCIL;
            }
            cmdBuf->d3dDC->ClearDepthStencilView(depthAttachment->dsv, dsClearFlags, action->depth.value, action->stencil.value);
            cmdBuf->renderPass->depthBuffer = depthAttachment->dsv;
        }
    }

    void EndRenderPass(Device* device, CommandBuffer cmdBuffer)
    {
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(cmdBuf->renderPass != nullptr);
        cmdBuf->renderPass = nullptr;
    }
    
    DXGI_FORMAT g_indexFormatTable[] = {
        DXGI_FORMAT::DXGI_FORMAT_R16_UINT,  // @TODO handle this nicer?
        DXGI_FORMAT::DXGI_FORMAT_R16_UINT,
        DXGI_FORMAT::DXGI_FORMAT_R16_UINT,
        DXGI_FORMAT::DXGI_FORMAT_R32_UINT,
    };

    void SubmitDrawCall(Device* device, CommandBuffer cmdBuffer, DrawCall* drawCall, Viewport* viewport, Rect* scissorRect)
    {
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(cmdBuf->renderPass != nullptr);
      
        if (drawCall->pipelineState.id != device->pipelineStateCache.id) {
            // @NOTE / @TODO: is this too expensive? can we just do that
            // maybe only reset pipeline state actually
            cmdBuf->d3dDC->ClearState();
        }

        // @TODO this is awkward to do, due to ClearState()....
        D3D11_RECT scissor{ 0, 0, (LONG)cmdBuf->renderPass->width, (LONG)cmdBuf->renderPass->height };
        D3D11_VIEWPORT vp;
        ZeroMemory(&vp, sizeof(vp));
        vp.Width = (float)cmdBuf->renderPass->width;
        vp.Height = (float)cmdBuf->renderPass->height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        if (viewport != nullptr) { vp.Width = viewport->width; vp.Height = viewport->height; }
        if (scissorRect != nullptr) { 
            scissor.top = scissorRect->top; scissor.bottom = scissorRect->bottom; 
            scissor.left = scissorRect->left; scissor.right = scissorRect->right;
        }

        cmdBuf->d3dDC->RSSetScissorRects(1, &scissor);
        cmdBuf->d3dDC->RSSetViewports(1, &vp);
        // @HACK 
        if (cmdBuf->renderPass->backbuffer != nullptr) {
            cmdBuf->d3dDC->OMSetRenderTargets(1, &cmdBuf->renderPass->backbuffer, cmdBuf->renderPass->depthBuffer);
        }
        else {
            ID3D11RenderTargetView* renderTargets[GFX_MAX_COLOR_ATTACHMENTS];
            UINT numRenderTargets = 0;
            for (int i = 0; i < GFX_MAX_COLOR_ATTACHMENTS; ++i) {
                if (!GFX_CHECK_RESOURCE(cmdBuf->renderPass->desc.colorAttachments[i].image)) {
                    break;
                }
                D3D11Image* colorAttachment = device->interf->imagePool.Get(cmdBuf->renderPass->desc.colorAttachments[i].image.id);
                renderTargets[numRenderTargets++] = colorAttachment->rtv;
            }
            ID3D11DepthStencilView* dsv = nullptr;
            if (GFX_CHECK_RESOURCE(cmdBuf->renderPass->desc.depthStencilAttachment.image)) {
                D3D11Image* depthImage = device->interf->imagePool.Get(cmdBuf->renderPass->desc.depthStencilAttachment.image.id);
                dsv = depthImage->dsv;
            }
            cmdBuf->d3dDC->OMSetRenderTargets(numRenderTargets, renderTargets, dsv);
        }

        D3D11PipelineState* pipelineState = device->interf->pipelineStatePool.Get(drawCall->pipelineState.id);

        uint32_t numVertexBuffers = 0;
        ID3D11Buffer* vertexBuffers[GFX_MAX_VERTEX_STREAMS];
        for (uint32_t i = 0; i < GFX_MAX_VERTEX_STREAMS; ++i) {
            if (GFX_CHECK_RESOURCE(drawCall->vertexBuffers[i])) {
                numVertexBuffers++;
                vertexBuffers[i] = device->interf->bufferPool.Get(drawCall->vertexBuffers[i].id)->buffer;
            }
            else {
                break;
            }
        }
        cmdBuf->d3dDC->IASetVertexBuffers(0, numVertexBuffers, vertexBuffers, drawCall->vertexStrides, drawCall->vertexOffsets);
        if (pipelineState->desc.indexFormat != IndexFormat::INDEX_FORMAT_NONE) {
            ID3D11Buffer* indexBuffer = device->interf->bufferPool.Get(drawCall->indexBuffer.id)->buffer;
            cmdBuf->d3dDC->IASetIndexBuffer(indexBuffer, g_indexFormatTable[(uint8_t)pipelineState->desc.indexFormat], 0);  // @NOTE allow offset here?
        }
        
        cmdBuf->d3dDC->OMSetBlendState(pipelineState->blendState, pipelineState->blendColor, pipelineState->blendWriteMask);
        cmdBuf->d3dDC->RSSetState(pipelineState->rasterizerState);
        cmdBuf->d3dDC->OMSetDepthStencilState(pipelineState->depthStencilState, 0);

        uint32_t numVSConstantBuffers = 0;
        ID3D11Buffer* vsConstantBuffers[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];
        uint32_t numPSConstantBuffers = 0;
        ID3D11Buffer* psConstantBuffers[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];
        uint32_t numGSConstantBuffers = 0;
        ID3D11Buffer* gsConstantBuffers[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];
        uint32_t numHSConstantBuffers = 0;
        ID3D11Buffer* hsConstantBuffers[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];
        uint32_t numDSConstantBuffers = 0;
        ID3D11Buffer* dsConstantBuffers[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];

        for (uint32_t i = 0; i < GFX_MAX_CONSTANT_INPUTS_PER_STAGE; ++i) {
            if (GFX_CHECK_RESOURCE(drawCall->vsConstantInputs[i])) {
                numVSConstantBuffers++;
                vsConstantBuffers[i] = device->interf->bufferPool.Get(drawCall->vsConstantInputs[i].id)->buffer;
            }
            if (GFX_CHECK_RESOURCE(drawCall->psConstantInputs[i])) {
                numPSConstantBuffers++;
                psConstantBuffers[i] = device->interf->bufferPool.Get(drawCall->psConstantInputs[i].id)->buffer;
            }
            if (GFX_CHECK_RESOURCE(drawCall->gsConstantInputs[i])) {
                numGSConstantBuffers++;
                gsConstantBuffers[i] = device->interf->bufferPool.Get(drawCall->gsConstantInputs[i].id)->buffer;
            }
            if (GFX_CHECK_RESOURCE(drawCall->hsConstantInputs[i])) {
                numHSConstantBuffers++;
                hsConstantBuffers[i] = device->interf->bufferPool.Get(drawCall->hsConstantInputs[i].id)->buffer;
            }
            if (GFX_CHECK_RESOURCE(drawCall->dsConstantInputs[i])) {
                numDSConstantBuffers++;
                dsConstantBuffers[i] = device->interf->bufferPool.Get(drawCall->dsConstantInputs[i].id)->buffer;
            }
        }

        if (numVSConstantBuffers > 0) {
            cmdBuf->d3dDC->VSSetConstantBuffers(0, numVSConstantBuffers, vsConstantBuffers);
        }
        if (numPSConstantBuffers > 0) {
            cmdBuf->d3dDC->PSSetConstantBuffers(0, numPSConstantBuffers, psConstantBuffers);
        }
        if (numGSConstantBuffers > 0) {
            cmdBuf->d3dDC->GSSetConstantBuffers(0, numGSConstantBuffers, gsConstantBuffers);
        }
        if (numHSConstantBuffers > 0) {
            cmdBuf->d3dDC->HSSetConstantBuffers(0, numHSConstantBuffers, hsConstantBuffers);
        }
        if (numDSConstantBuffers > 0) {
            cmdBuf->d3dDC->DSSetConstantBuffers(0, numDSConstantBuffers, dsConstantBuffers);
        }

        // @HACK - ish should do the same as with buffers (single bind call)
        for (int i = 0; i < GFX_MAX_IMAGE_INPUTS_PER_STAGE; ++i) {
            if (GFX_CHECK_RESOURCE(drawCall->psImageInputs[i])) {
                auto imageObj = device->interf->imagePool.Get(drawCall->psImageInputs[i].id);
                auto srv = imageObj->srv;
                auto sampler = imageObj->sampler;
                cmdBuf->d3dDC->PSSetSamplers(i, 1, &sampler);
                cmdBuf->d3dDC->PSSetShaderResources(i, 1, &srv);
            }
        }

        cmdBuf->d3dDC->VSSetShader(pipelineState->vertexShader->as_vertexShader, nullptr, 0);
        cmdBuf->d3dDC->PSSetShader(pipelineState->pixelShader->as_pixelShader, nullptr, 0);
        if (pipelineState->geometryShader != nullptr) {
            cmdBuf->d3dDC->GSSetShader(pipelineState->geometryShader->as_geometryShader, nullptr, 0);
        }
        if (pipelineState->hullShader != nullptr) {
            cmdBuf->d3dDC->HSSetShader(pipelineState->geometryShader->as_hullShader, nullptr, 0);
        }
        if (pipelineState->domainShader != nullptr) {
            cmdBuf->d3dDC->DSSetShader(pipelineState->geometryShader->as_domainShader, nullptr, 0);
        }
        cmdBuf->d3dDC->IASetPrimitiveTopology(g_primitiveTypeTable[(uint8_t)pipelineState->desc.primitiveType]);
        cmdBuf->d3dDC->IASetInputLayout(pipelineState->inputLayout);
        if (pipelineState->desc.indexFormat != IndexFormat::INDEX_FORMAT_NONE) {
            if (drawCall->numInstances > 1) {
                cmdBuf->d3dDC->DrawIndexedInstanced(drawCall->numElements, drawCall->numInstances, drawCall->elementOffset, drawCall->startVertexLocation, drawCall->startInstanceLocation);
            }
            else {
                cmdBuf->d3dDC->DrawIndexed(drawCall->numElements, drawCall->elementOffset, drawCall->startVertexLocation);
            }
        }
        else {
            if (drawCall->numInstances > 1) {
                cmdBuf->d3dDC->DrawInstanced(drawCall->numElements, drawCall->numInstances, drawCall->startVertexLocation, drawCall->startInstanceLocation);
            }
            else {
                cmdBuf->d3dDC->Draw(drawCall->numElements, drawCall->startVertexLocation);
            }
        }

    
    }


    void SetViewport(Device* device, CommandBuffer cmdBuffer, Viewport viewport)
    {
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(cmdBuf->renderPass != nullptr);

        D3D11_VIEWPORT vp;
        ZeroMemory(&vp, sizeof(D3D11_VIEWPORT));
        vp.Width = viewport.width;
        vp.Height = viewport.height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = vp.TopLeftY = 0.0f;
        cmdBuf->d3dDC->RSSetViewports(1, &vp);
    }

    void SetScissor(Device* device, CommandBuffer cmdBuffer, Rect scissorRect)
    {
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(cmdBuf->renderPass != nullptr);

        D3D11_RECT r = { (LONG)scissorRect.left, (LONG)scissorRect.top, (LONG)scissorRect.right, (LONG)scissorRect.bottom };
        cmdBuf->d3dDC->RSSetScissorRects(1, &r);
    }


    void PresentSwapChain(Device* device, SwapChain swapChain) 
    {
        D3D11SwapChain* swpChn = device->interf->swapChainPool.Get(swapChain.id);
        swpChn->swapChain->Present(0, 0);
    }

    D3D11_MAP g_mapTypeTable[] = {
        D3D11_MAP_READ_WRITE,
        D3D11_MAP_READ,
        D3D11_MAP_WRITE,
        D3D11_MAP_READ_WRITE,
        D3D11_MAP_WRITE_DISCARD,
        D3D11_MAP_WRITE_NO_OVERWRITE
    };

    void* MapBuffer(Device* device, Buffer buffer, MapType mapType)
    {
        D3D11Buffer* bufferObj = device->interf->bufferPool.Get(buffer.id);
        if (!bufferObj) { return nullptr; }
        D3D11_MAPPED_SUBRESOURCE subres;
        ZeroMemory(&subres, sizeof(subres));
        HRESULT res = device->d3dDC->Map(bufferObj->buffer, 0, g_mapTypeTable[(uint8_t)mapType], 0, &subres);
        if (FAILED(res)) {
            return nullptr;
        }
        return subres.pData;
    }

    void UnmapBuffer(Device* device, Buffer buffer)
    {
        D3D11Buffer* bufferObj = device->interf->bufferPool.Get(buffer.id);
        if (!bufferObj) { return; }
        device->d3dDC->Unmap(bufferObj->buffer, 0);
    }


    void DestroyBuffer(Device* device, Buffer buffer)
    {
        device->interf->bufferPool.Free(buffer.id);
    }

    void DestroyImage(Device* device, Image image)
    {
        device->interf->imagePool.Free(image.id);
    }
}