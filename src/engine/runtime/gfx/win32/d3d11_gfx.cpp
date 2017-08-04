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
        // @TODO
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
        // @TODO
    };

    struct D3D11CommandBuffer
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = HANDLE_GENERATION_START;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        ID3D11DeviceContext*    d3dDC = nullptr;

        bool            inRenderPass = false;
        // @TODO
    };

    struct D3D11SwapChain
    {
        Device*         associatedDevice = nullptr;
        uint16_t        generation = HANDLE_GENERATION_START;
        _ResourceState  resState = _ResourceState::STATE_EMPTY;

        SwapChainDesc   desc;

        IDXGISwapChain*             swapChain   = nullptr;
        ID3D11RenderTargetView*     rtv         = nullptr;
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


    DXGI_FORMAT g_vertexFormatTable[] = {
        DXGI_FORMAT::DXGI_FORMAT_UNKNOWN,
        DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT::DXGI_FORMAT_R32G32_FLOAT,
        DXGI_FORMAT::DXGI_FORMAT_R32G32B32_FLOAT,
        DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT
    };

    D3D11_PRIMITIVE_TOPOLOGY g_primitiveTypeTable[] = {
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        D3D11_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
    };

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
                inputElements[i] = { attrib.name, attrib.index, g_vertexFormatTable[(uint8_t)attrib.format], 0, attrib.offset, D3D11_INPUT_PER_VERTEX_DATA, 0};
                numInputElements++;
            }
            else {
                break;
            }
        }
        HRESULT res = device->d3dDevice->CreateInputLayout(inputElements, numInputElements, state->vertexShader->desc.code, state->vertexShader->desc.codeSize, &state->inputLayout);
        if (FAILED(res)) {
            device->interf->pipelineStatePool.ReleaseIndex(result.id);
            return { gfx::INVALID_ID };
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
        pBackBuffer->Release();

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
        renderPass->desc = *desc;
        return result;
    }


    void BeginDefaultRenderPass(Device* device, CommandBuffer cmdBuffer, SwapChain swapChain, RenderPassAction* action)
    {
        D3D11SwapChain* swpCh = device->interf->swapChainPool.Get(swapChain.id);
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(!cmdBuf->inRenderPass);
        cmdBuf->inRenderPass = true;
        cmdBuf->d3dDC->OMSetRenderTargets(1, &swpCh->rtv, nullptr);
        if (action->colors[0].action == Action::ACTION_CLEAR) {
            cmdBuf->d3dDC->ClearRenderTargetView(swpCh->rtv, action->colors[0].color);
        }
    }

    void EndRenderPass(Device* device, CommandBuffer cmdBuffer)
    {
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(cmdBuf->inRenderPass);
        cmdBuf->inRenderPass = false;
    }
    
    DXGI_FORMAT g_indexFormatTable[] = {
        DXGI_FORMAT::DXGI_FORMAT_R16_UINT,  // @TODO handle this nicer?
        DXGI_FORMAT::DXGI_FORMAT_R16_UINT,
        DXGI_FORMAT::DXGI_FORMAT_R16_UINT,
        DXGI_FORMAT::DXGI_FORMAT_R32_UINT,
    };

    void SubmitDrawCall(Device* device, CommandBuffer cmdBuffer, DrawCall* drawCall)
    {
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(cmdBuf->inRenderPass);
        
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
            cmdBuf->d3dDC->IASetIndexBuffer(indexBuffer, g_indexFormatTable[(uint8_t)pipelineState->desc.indexFormat], drawCall->elementOffset);
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
            cmdBuf->d3dDC->DrawIndexedInstanced(drawCall->numElements, drawCall->numInstances, drawCall->elementOffset, 0, 0);
        }
        else {
            cmdBuf->d3dDC->DrawInstanced(drawCall->numElements, drawCall->numInstances, 0, 0);
        }
    }


    void SetViewport(Device* device, CommandBuffer cmdBuffer, Viewport viewport)
    {
        D3D11CommandBuffer* cmdBuf = device->interf->cmdBufferPool.Get(cmdBuffer.id);
        assert(cmdBuf->inRenderPass);

        D3D11_VIEWPORT vp;
        ZeroMemory(&vp, sizeof(D3D11_VIEWPORT));
        vp.Width = viewport.width;
        vp.Height = viewport.height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = vp.TopLeftY = 0.0f;
        cmdBuf->d3dDC->RSSetViewports(1, &vp);
    }

    void PresentSwapChain(Device* device, SwapChain swapChain) 
    {
        D3D11SwapChain* swpChn = device->interf->swapChainPool.Get(swapChain.id);
        swpChn->swapChain->Present(0, 0);
    }
}