// ImGui Win32 + DirectX11 binding
// In this binding, ImTextureID is used to store a 'ID3D11ShaderResourceView*' texture identifier. Read the FAQ about ImTextureID in imgui.cpp.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you use this binding you'll need to call 4 functions: ImGui_ImplXXXX_Init(), ImGui_ImplXXXX_NewFrame(), ImGui::Render() and ImGui_ImplXXXX_Shutdown().
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

#include <engine/runtime/ImGui/imgui.h>
#include "imgui_impl_dx11.h"

#include <foundation/logging/logging.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include <engine/runtime/gfx/gfx.h>

// Data
static INT64                    g_Time = 0;
static INT64                    g_TicksPerSecond = 0;

static HWND                     g_hWnd = 0;
static gfx::Device*             g_gfxDevice = nullptr;
static gfx::Buffer              g_pVB;
static gfx::Buffer              g_pIB;
static gfx::Shader              g_pVertexShader;
static gfx::Shader              g_pPixelShader;
static gfx::Buffer              g_pVertexConstantBuffer;
static ID3D10Blob*              g_pVertexShaderBlob = NULL;
static ID3D10Blob *             g_pPixelShaderBlob = NULL;

static gfx::Image               g_fontImage;

static bool g_fontsLoaded = false;  // @HACK nasty hack


static gfx::PipelineState       g_pipelineState;
//static ID3D11PixelShader*       g_pPixelShader = NULL;
//static ID3D11SamplerState*      g_pFontSampler = NULL;
//static ID3D11ShaderResourceView*g_pFontTextureView = NULL;
//static ID3D11RasterizerState*   g_pRasterizerState = NULL;
//static ID3D11BlendState*        g_pBlendState = NULL;
//static ID3D11DepthStencilState* g_pDepthStencilState = NULL;
static int                      g_VertexBufferSize = 5000, g_IndexBufferSize = 10000;

struct VERTEX_CONSTANT_BUFFER
{
    float        mvp[4][4];
};

// This is the main rendering function that you have to implement and provide to ImGui (via setting up 'RenderDrawListsFn' in the ImGuiIO structure)
// If text or lines are blurry when integrating ImGui in your engine:
// - in your Render function, try translating your projection matrix by (0.5f,0.5f) or (0.375f,0.375f)
void ImGui_ImplDX11_RenderDrawLists(ImDrawData* draw_data, gfx::CommandBuffer* commandBuffer)
{
    gfx::Device* device = g_gfxDevice;

    // Create and grow vertex/index buffers if needed
    if (!GFX_CHECK_RESOURCE(g_pVB) || g_VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (GFX_CHECK_RESOURCE(g_pVB)) { gfx::DestroyBuffer(device, g_pVB); g_pVB = gfx::Buffer(); }
        g_VertexBufferSize = draw_data->TotalVtxCount + 5000;
        gfx::BufferDesc desc;
        desc.usage = gfx::ResourceUsage::USAGE_STREAM;
        desc.type = gfx::BufferType::BUFFER_TYPE_VERTEX;
        desc.byteWidth = g_VertexBufferSize * sizeof(ImDrawVert);
        if (!GFX_CHECK_RESOURCE((g_pVB = gfx::CreateBuffer(device, &desc)))) {
            return;
        }
    }
    if (!GFX_CHECK_RESOURCE(g_pIB) || g_IndexBufferSize < draw_data->TotalIdxCount)
    {
        if (GFX_CHECK_RESOURCE(g_pIB)) { gfx::DestroyBuffer(device, g_pIB); g_pIB = gfx::Buffer(); }
        g_IndexBufferSize = draw_data->TotalIdxCount + 10000;
        gfx::BufferDesc desc;
        desc.usage = gfx::ResourceUsage::USAGE_STREAM;
        desc.type = gfx::BufferType::BUFFER_TYPE_INDEX;
        desc.byteWidth = g_IndexBufferSize * sizeof(ImDrawIdx);
        if (!GFX_CHECK_RESOURCE((g_pIB = gfx::CreateBuffer(device, &desc)))) {
            return;
        }
    }

    // Copy and convert all vertices into a single contiguous buffer
    ImDrawVert* vtx_dst = (ImDrawVert*)gfx::MapBuffer(device, g_pVB, gfx::MapType::MAP_WRITE_DISCARD);
    ImDrawIdx* idx_dst = (ImDrawIdx*)gfx::MapBuffer(device, g_pIB, gfx::MapType::MAP_WRITE_DISCARD);
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
    }
    gfx::UnmapBuffer(device, g_pVB);
    gfx::UnmapBuffer(device, g_pIB);

    // Setup orthographic projection matrix into our constant buffer
    {
        VERTEX_CONSTANT_BUFFER* constant_buffer = (VERTEX_CONSTANT_BUFFER*)gfx::MapBuffer(device, g_pVertexConstantBuffer, gfx::MapType::MAP_WRITE_DISCARD);
        float L = 0.0f;
        float R = ImGui::GetIO().DisplaySize.x;
        float B = ImGui::GetIO().DisplaySize.y;
        float T = 0.0f;
        float mvp[4][4] =
        {
            { 2.0f/(R-L),   0.0f,           0.0f,       0.0f },
            { 0.0f,         2.0f/(T-B),     0.0f,       0.0f },
            { 0.0f,         0.0f,           0.5f,       0.0f },
            { (R+L)/(L-R),  (T+B)/(B-T),    0.5f,       1.0f },
        };
        memcpy(&constant_buffer->mvp, mvp, sizeof(mvp));
        UnmapBuffer(device, g_pVertexConstantBuffer);
    }

    // Setup viewport
    gfx::Viewport vp;
    vp.width = ImGui::GetIO().DisplaySize.x;
    vp.height = ImGui::GetIO().DisplaySize.y;

    // prepare the draw call
    unsigned int stride = sizeof(ImDrawVert);
    unsigned int offset = 0;

    gfx::DrawCall drawCall;
    drawCall.vertexBuffers[0] = g_pVB;
    drawCall.vertexOffsets[0] = offset;
    drawCall.vertexStrides[0] = stride;
    drawCall.indexBuffer = g_pIB;
    drawCall.pipelineState = g_pipelineState;
    drawCall.vsConstantInputs[0] = g_pVertexConstantBuffer;
    
    /*
    ctx->IASetInputLayout(g_pInputLayout);
    ctx->IASetVertexBuffers(0, 1, &g_pVB, &stride, &offset);
    ctx->IASetIndexBuffer(g_pIB, sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(g_pVertexShader, NULL, 0);
    ctx->VSSetConstantBuffers(0, 1, &g_pVertexConstantBuffer);
    ctx->PSSetShader(g_pPixelShader, NULL, 0);
    ctx->PSSetSamplers(0, 1, &g_pFontSampler);*/

    // Setup render state
    /*const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx->OMSetBlendState(g_pBlendState, blend_factor, 0xffffffff);
    ctx->OMSetDepthStencilState(g_pDepthStencilState, 0);
    ctx->RSSetState(g_pRasterizerState);
    */

    // Render command lists
    int vtx_offset = 0;
    int idx_offset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                
                gfx::Rect r = { (uint32_t)pcmd->ClipRect.x, (uint32_t)pcmd->ClipRect.y, (uint32_t)pcmd->ClipRect.z, (uint32_t)pcmd->ClipRect.w };
                gfx::Image tex = { (uint32_t)(uintptr_t)pcmd->TextureId };
                if (GFX_CHECK_RESOURCE(tex)) {
                    drawCall.psImageInputs[0] = tex;
                }
                else {
                    drawCall.psImageInputs[0] = { gfx::INVALID_ID };
                }
                drawCall.numElements = pcmd->ElemCount;
                drawCall.elementOffset = idx_offset;
                //assert((idx_offset % 2 == 0));
                drawCall.startVertexLocation = vtx_offset;
                gfx::SubmitDrawCall(device, *commandBuffer, &drawCall, &vp, &r);
                //ctx->DrawIndexed(pcmd->ElemCount, idx_offset, vtx_offset);
            }
            idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
    }
}

IMGUI_API LRESULT ImGui_ImplDX11_WndProcHandler(HWND, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGuiIO& io = ImGui::GetIO();
    switch (msg)
    {
    case WM_LBUTTONDOWN:
        io.MouseDown[0] = true;
        return true;
    case WM_LBUTTONUP:
        io.MouseDown[0] = false;
        return true;
    case WM_RBUTTONDOWN:
        io.MouseDown[1] = true;
        return true;
    case WM_RBUTTONUP:
        io.MouseDown[1] = false;
        return true;
    case WM_MBUTTONDOWN:
        io.MouseDown[2] = true;
        return true;
    case WM_MBUTTONUP:
        io.MouseDown[2] = false;
        return true;
    case WM_MOUSEWHEEL:
        io.MouseWheel += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1.0f : -1.0f;
        return true;
    case WM_MOUSEMOVE:
        io.MousePos.x = (signed short)(lParam);
        io.MousePos.y = (signed short)(lParam >> 16);
        return true;
    case WM_KEYDOWN:
        if (wParam < 256)
            io.KeysDown[wParam] = 1;
        return true;
    case WM_KEYUP:
        if (wParam < 256)
            io.KeysDown[wParam] = 0;
        return true;
    case WM_CHAR:
        // You can also use ToAscii()+GetKeyboardState() to retrieve characters.
        if (wParam > 0 && wParam < 0x10000)
            io.AddInputCharacter((unsigned short)wParam);
        return true;
    }
    return 0;
}

static void ImGui_ImplDX11_CreateFontsTexture()
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Upload texture to graphics system
    
    gfx::ImageDesc desc;
    desc.type = gfx::ImageType::IMAGE_TYPE_2D;
    desc.width = width;
    desc.height = height;
    desc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R8G8B8A8_UNORM;
    desc.numDataItems = 1;
    desc.initialData = (void**)&pixels;
    size_t size = width * height * 4;
    desc.initialDataSizes = &size;
    g_fontImage = gfx::CreateImage(g_gfxDevice, &desc);
       
    // Store our identifier
    io.Fonts->TexID = (void*)(uintptr_t)g_fontImage.id;
}

bool    ImGui_ImplDX11_CreateDeviceObjects()
{
    if (!g_gfxDevice)
        return false;
    if (!GFX_CHECK_RESOURCE(g_fontImage))
        ImGui_ImplDX11_InvalidateDeviceObjects();
        
    // By using D3DCompile() from <d3dcompiler.h> / d3dcompiler.lib, we introduce a dependency to a given version of d3dcompiler_XX.dll (see D3DCOMPILER_DLL_A)
    // If you would like to use this DX11 sample code but remove this dependency you can: 
    //  1) compile once, save the compiled shader blobs into a file or source code and pass them to CreateVertexShader()/CreatePixelShader() [preferred solution]
    //  2) use code to detect any version of the DLL and grab a pointer to D3DCompile from the DLL. 
    // See https://github.com/ocornut/imgui/pull/638 for sources and details.
    gfx::VertexLayoutDesc vertexLayout;
    // Create the vertex shader
    {
        static const char* vertexShader =
            "cbuffer vertexBuffer : register(b0) \
            {\
            float4x4 ProjectionMatrix; \
            };\
            struct VS_INPUT\
            {\
            float2 pos : POSITION;\
            float4 col : COLOR0;\
            float2 uv  : TEXCOORD0;\
            };\
            \
            struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float4 col : COLOR0;\
            float2 uv  : TEXCOORD0;\
            };\
            \
            PS_INPUT main(VS_INPUT input)\
            {\
            PS_INPUT output;\
            output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\
            output.col = input.col;\
            output.uv  = input.uv;\
            return output;\
            }";

        ID3D10Blob* pErrorBlob = nullptr;
        D3DCompile(vertexShader, strlen(vertexShader), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &g_pVertexShaderBlob, &pErrorBlob);
        if (pErrorBlob) {
            GT_LOG_ERROR("%s\n", (char*)pErrorBlob->GetBufferPointer());
        }
        if (g_pVertexShaderBlob == NULL) // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
            return false;

        gfx::ShaderDesc vShaderDesc;
        vShaderDesc.type = gfx::ShaderType::SHADER_TYPE_VS;
        vShaderDesc.code = (char*)g_pVertexShaderBlob->GetBufferPointer();
        vShaderDesc.codeSize = g_pVertexShaderBlob->GetBufferSize();
        if (!GFX_CHECK_RESOURCE((g_pVertexShader = gfx::CreateShader(g_gfxDevice, &vShaderDesc)))) {
            return false;
        }
            
        // Create the input layout
        
        vertexLayout.attribs[0] = { "POSITION", 0, (size_t)(&((ImDrawVert*)0)->pos), 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT2 };
        vertexLayout.attribs[1] = { "TEXCOORD", 0, (size_t)(&((ImDrawVert*)0)->uv), 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT2 };
        vertexLayout.attribs[2] = { "COLOR", 0, (size_t)(&((ImDrawVert*)0)->col), 0, gfx::VertexFormat::VERTEX_FORMAT_R8G8B8A8_UNNORM };
        
        // Create the constant buffer
        {
            gfx::BufferDesc desc;
            desc.type = gfx::BufferType::BUFFER_TYPE_CONSTANT;
            desc.byteWidth = sizeof(VERTEX_CONSTANT_BUFFER);
            desc.usage = gfx::ResourceUsage::USAGE_DYNAMIC;
            if (!GFX_CHECK_RESOURCE((g_pVertexConstantBuffer = gfx::CreateBuffer(g_gfxDevice, &desc)))) {
                return false;
            }
        }
    }

    // Create the pixel shader
    {
        static const char* pixelShader =
            "struct PS_INPUT\
            {\
            float4 pos : SV_POSITION;\
            float4 col : COLOR0;\
            float2 uv  : TEXCOORD0;\
            };\
            sampler sampler0;\
            Texture2D texture0;\
            \
            float4 main(PS_INPUT input) : SV_Target\
            {\
            float4 out_col = input.col * texture0.Sample(sampler0, input.uv); \
            return out_col; \
            }";

        D3DCompile(pixelShader, strlen(pixelShader), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &g_pPixelShaderBlob, NULL);
        if (g_pPixelShaderBlob == NULL)  // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
            return false;
        gfx::ShaderDesc pShaderDesc;
        pShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
        pShaderDesc.code = (char*)g_pPixelShaderBlob->GetBufferPointer();
        pShaderDesc.codeSize = g_pPixelShaderBlob->GetBufferSize();
        if (!GFX_CHECK_RESOURCE((g_pPixelShader = gfx::CreateShader(g_gfxDevice, &pShaderDesc)))) {
            return false;
        }
    }

    // create the pipeline

    gfx::PipelineStateDesc pipelineDesc;

    // blend state setup
    pipelineDesc.blendState.enableBlend = true;
    pipelineDesc.blendState.srcBlend = gfx::BlendFactor::BLEND_SRC_ALPHA;
    pipelineDesc.blendState.dstBlend = gfx::BlendFactor::BLEND_INV_SRC_ALPHA;
    pipelineDesc.blendState.blendOp = gfx::BlendOp::BLEND_OP_ADD;
    pipelineDesc.blendState.srcBlendAlpha = gfx::BlendFactor::BLEND_ZERO;
    pipelineDesc.blendState.dstBlendAlpha = gfx::BlendFactor::BLEND_INV_SRC_ALPHA;
    pipelineDesc.blendState.blendOpAlpha = gfx::BlendOp::BLEND_OP_ADD;
    pipelineDesc.blendState.writeMask = gfx::COLOR_WRITE_MASK_ALL;
    // rasterizer state setup
    pipelineDesc.rasterState.cullMode = gfx::CullMode::CULL_NONE;
    pipelineDesc.rasterState.enableScissor = true;
    
    // depth stencil state setup - NONE becasue nothing to do here, we have smart defaults!

    // shaders + input assembly 
    pipelineDesc.vertexShader = g_pVertexShader;
    pipelineDesc.pixelShader = g_pPixelShader;
    pipelineDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_UINT16;
    pipelineDesc.vertexLayout = vertexLayout;
    pipelineDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLES;
    if (!GFX_CHECK_RESOURCE((g_pipelineState = gfx::CreatePipelineState(g_gfxDevice, &pipelineDesc)))) {
        return false;
    }
    
    // Create the blending setup
    ////{
    ////    D3D11_BLEND_DESC desc;
    ////    ZeroMemory(&desc, sizeof(desc));
    ////    desc.AlphaToCoverageEnable = false;
    ////    desc.RenderTarget[0].BlendEnable = true;
    ////    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    ////    desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    ////    desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    ////    desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    ////    desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    ////    desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    ////    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ////    g_pd3dDevice->CreateBlendState(&desc, &g_pBlendState);
    ////}

    ////// Create the rasterizer state
    ////{
    ////    D3D11_RASTERIZER_DESC desc;
    ////    ZeroMemory(&desc, sizeof(desc));
    ////    desc.FillMode = D3D11_FILL_SOLID;
    ////    desc.CullMode = D3D11_CULL_NONE;
    ////    desc.ScissorEnable = true;
    ////    desc.DepthClipEnable = true;
    ////    g_pd3dDevice->CreateRasterizerState(&desc, &g_pRasterizerState);
    ////}

    ////// Create depth-stencil State
    ////{
    ////    D3D11_DEPTH_STENCIL_DESC desc;
    ////    ZeroMemory(&desc, sizeof(desc));
    ////    desc.DepthEnable = false;
    ////    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ////    desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ////    desc.StencilEnable = false;
    ////    desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    ////    desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    ////    desc.BackFace = desc.FrontFace;
    ////    g_pd3dDevice->CreateDepthStencilState(&desc, &g_pDepthStencilState);
    ////}

    ImGui_ImplDX11_CreateFontsTexture();
    g_fontsLoaded = true;
    return true;
}

void    ImGui_ImplDX11_InvalidateDeviceObjects()
{
    if (!g_gfxDevice)
        return;

    if (GFX_CHECK_RESOURCE(g_fontImage)) { gfx::DestroyImage(g_gfxDevice, g_fontImage); g_fontImage = gfx::Image(); }
    //if (g_pFontTextureView) { g_pFontTextureView->Release(); g_pFontTextureView = NULL; ImGui::GetIO().Fonts->TexID = NULL; } // We copied g_pFontTextureView to io.Fonts->TexID so let's clear that as well.
    if (GFX_CHECK_RESOURCE(g_pIB)) { gfx::DestroyBuffer(g_gfxDevice, g_pIB); g_pIB = gfx::Buffer(); }
    if (GFX_CHECK_RESOURCE(g_pVB)) { gfx::DestroyBuffer(g_gfxDevice, g_pVB); g_pVB = gfx::Buffer(); }

    /*if (g_pBlendState) { g_pBlendState->Release(); g_pBlendState = NULL; }
    if (g_pDepthStencilState) { g_pDepthStencilState->Release(); g_pDepthStencilState = NULL; }
    if (g_pRasterizerState) { g_pRasterizerState->Release(); g_pRasterizerState = NULL; }
    if (g_pPixelShader) { g_pPixelShader->Release(); g_pPixelShader = NULL; }
    if (g_pPixelShaderBlob) { g_pPixelShaderBlob->Release(); g_pPixelShaderBlob = NULL; }
    if (g_pVertexConstantBuffer) { g_pVertexConstantBuffer->Release(); g_pVertexConstantBuffer = NULL; }
    if (g_pInputLayout) { g_pInputLayout->Release(); g_pInputLayout = NULL; }
    if (g_pVertexShader) { g_pVertexShader->Release(); g_pVertexShader = NULL; }
    if (g_pVertexShaderBlob) { g_pVertexShaderBlob->Release(); g_pVertexShaderBlob = NULL; }*/
}

bool    ImGui_ImplDX11_Init(void* hwnd, gfx::Device* device)
{
    g_hWnd = (HWND)hwnd;
    g_gfxDevice = device;
    
    if (!QueryPerformanceFrequency((LARGE_INTEGER *)&g_TicksPerSecond))
        return false;
    if (!QueryPerformanceCounter((LARGE_INTEGER *)&g_Time))
        return false;

    ImGuiIO& io = ImGui::GetIO();
    io.KeyMap[ImGuiKey_Tab] = VK_TAB;                       // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array that we will update during the application lifetime.
    io.KeyMap[ImGuiKey_LeftArrow] = VK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = VK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = VK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = VK_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = VK_PRIOR;
    io.KeyMap[ImGuiKey_PageDown] = VK_NEXT;
    io.KeyMap[ImGuiKey_Home] = VK_HOME;
    io.KeyMap[ImGuiKey_End] = VK_END;
    io.KeyMap[ImGuiKey_Delete] = VK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = VK_BACK;
    io.KeyMap[ImGuiKey_Enter] = VK_RETURN;
    io.KeyMap[ImGuiKey_Escape] = VK_ESCAPE;
    io.KeyMap[ImGuiKey_A] = 'A';
    io.KeyMap[ImGuiKey_C] = 'C';
    io.KeyMap[ImGuiKey_V] = 'V';
    io.KeyMap[ImGuiKey_X] = 'X';
    io.KeyMap[ImGuiKey_Y] = 'Y';
    io.KeyMap[ImGuiKey_Z] = 'Z';

    io.RenderDrawListsFn = nullptr; // ImGui_ImplDX11_RenderDrawLists;  // Alternatively you can set this to NULL and call ImGui::GetDrawData() after ImGui::Render() to get the same ImDrawData pointer.
    io.ImeWindowHandle = g_hWnd;

    return true;
}

void ImGui_ImplDX11_Shutdown()
{
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui::Shutdown();
    g_gfxDevice = nullptr;
    g_hWnd = (HWND)0;
}

void ImGui_ImplDX11_NewFrame()
{
    if (!g_fontsLoaded)
        ImGui_ImplDX11_CreateDeviceObjects();

    ImGuiIO& io = ImGui::GetIO();

    // Setup display size (every frame to accommodate for window resizing)
    RECT rect;
    GetClientRect(g_hWnd, &rect);
    io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

    // Setup time step
    INT64 current_time;
    QueryPerformanceCounter((LARGE_INTEGER *)&current_time);
    io.DeltaTime = (float)(current_time - g_Time) / g_TicksPerSecond;
    g_Time = current_time;

    // Read keyboard modifiers inputs
    io.KeyCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    io.KeyShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    io.KeyAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    io.KeySuper = false;
    // io.KeysDown : filled by WM_KEYDOWN/WM_KEYUP events
    // io.MousePos : filled by WM_MOUSEMOVE events
    // io.MouseDown : filled by WM_*BUTTON* events
    // io.MouseWheel : filled by WM_MOUSEWHEEL events

    // Hide OS mouse cursor if ImGui is drawing it
    if (io.MouseDrawCursor)
        SetCursor(NULL);

    // Start the frame
    ImGui::NewFrame();
}