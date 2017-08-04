#include "../gfx.h"
#include <foundation/memory/memory.h>
#include <foundation/memory/allocators.h>
#include <cassert>  // @TODO: override

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#define HANDLE_INDEX(handle)        (uint16_t)(handle)
#define HANDLE_GENERATION(handle)   (uint16_t)(handle >> 16)

#define MAKE_HANDLE(index, generation) (uint32_t)(((uint32_t)generation) << 16 | index); 

namespace gfx
{
    struct D3D11Buffer 
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation  = 1;
        _ResourceState  resState    = _ResourceState::STATE_EMPTY;

        BufferDesc desc;
        ID3D11Buffer*   buffer      = nullptr;
    };

    struct D3D11Image
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = 1;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        // @TODO
    };

    struct D3D11PipelineState
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = 1;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;
        
        // @TODO
    };
   
    struct D3D11Shader
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = 1;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        // @NOTE
        union {
            ID3D11VertexShader* as_vertexShader;
            ID3D11PixelShader* as_pixelShader;
            ID3D11GeometryShader* as_geometryShader;
            ID3D11HullShader* as_hullShader;
            ID3D11DomainShader* as_domainShader;
        };
    };

    struct D3D11RenderPass
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = 0;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        // @TODO
    };

    struct D3D11CommandBuffer
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = 0;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        // @TODO
    };

    struct D3D11SwapChain
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = 0;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        // @TODO
    };

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

        return device;
    }



    //
    //

    D3D11_USAGE g_resUsageTable[] = {
        D3D11_USAGE::D3D11_USAGE_DEFAULT,
        D3D11_USAGE::D3D11_USAGE_IMMUTABLE,
        D3D11_USAGE::D3D11_USAGE_DYNAMIC,
        D3D11_USAGE::D3D11_USAGE_DYNAMIC,   // D3d11 has no staging buffers so no distiction here
        D3D11_USAGE::D3D11_USAGE_STAGING
    };

    Buffer CreateBuffer(Device* device, BufferDesc* desc)
    {
        gfx::D3D11Buffer* buffer = nullptr;
        Buffer result{ gfx::INVALID_ID };
       
        if (!device->interf->bufferPool.Allocate(&buffer, &result.id)) {
            return { gfx::INVALID_ID };
        }

        D3D11_BUFFER_DESC d3d11Desc;
        ZeroMemory(&d3d11Desc, sizeof(d3d11Desc));
        d3d11Desc.ByteWidth = (UINT)desc->byteWidth;
        d3d11Desc.Usage = g_resUsageTable[(uint8_t)desc->usage];
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
        }
        HRESULT res = device->d3dDevice->CreateBuffer(&d3d11Desc, initialDataPtr, &buffer->buffer);
        if (FAILED(res)) {
            // @TODO: logging/error code
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
            return { gfx::INVALID_ID };
        }
        return result;
    }

}