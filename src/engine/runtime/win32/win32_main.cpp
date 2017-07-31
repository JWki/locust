
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <engine/runtime/ImGui/imgui.h>
#include <engine/runtime/win32/imgui_impl_dx11.h>

#include <tchar.h>
#include <stdio.h>
#include <malloc.h>

#include <foundation/int_types.h>
#include <foundation/memory/memory.h>
#include <foundation/memory/allocators.h>
#include <foundation/logging/logging.h>

#include <foundation/math/math.h>
#define IS_POW_OF_TWO(n) ((n & (n - 1)) == 0)

class SimpleMemoryTracker
{
    size_t m_usedMemory = 0;
public:
    ~SimpleMemoryTracker()
    {
        if (m_usedMemory > 0) {
            GT_LOG_INFO("Memory", "Too much memory used!");
        }
    }

    GT_FORCE_INLINE void TrackAllocation(void* memory, size_t size, size_t alignemnt, fnd::SourceInfo scInfo)
    {
        m_usedMemory += size;
    }

    GT_FORCE_INLINE void UntrackAllocation(void* memory, size_t size)
    {
        m_usedMemory -= size;
    }

    inline size_t GetUsedMemorySize()
    {
        return m_usedMemory;
    }
};

class ExtendedMemoryTracker;

namespace {
    static ExtendedMemoryTracker* g_memTrackListHead = nullptr;
    static ExtendedMemoryTracker* g_memTrackListTail = nullptr;
}

static ExtendedMemoryTracker* GetMemTrackerListHead() { return g_memTrackListHead; }

class ExtendedMemoryTracker
{
    struct AllocInfo
    {
        void* ptr = nullptr;
        size_t size = 0;
        size_t alignment = 0;
        fnd::SourceInfo info;
    };
    AllocInfo* m_allocations = nullptr;
    size_t m_numAllocations = 0;
    size_t m_capacity = 0;
    fnd::memory::MemoryArenaBase* m_arena = nullptr;

    const char* m_name = "";

    ExtendedMemoryTracker* m_next = nullptr;
    ExtendedMemoryTracker* m_prev = nullptr;

    void Register()
    {
        if (g_memTrackListHead == nullptr) {
            g_memTrackListHead = g_memTrackListTail = this;
        }
        else {
            m_prev = g_memTrackListTail;
            g_memTrackListTail->m_next = this;
            g_memTrackListTail = this;
        }

    }

    void Unregister()
    {
        if (m_next) { m_next->m_prev = m_prev; }
        if (m_prev) { m_prev->m_next = m_next; }
        if (g_memTrackListHead == this) { g_memTrackListHead = g_memTrackListTail = nullptr; }
        if (g_memTrackListTail == this) { g_memTrackListTail = m_prev; }
    }

public:
    ExtendedMemoryTracker()
    {
        Register();
    }

    ~ExtendedMemoryTracker()
    {
        Unregister();

        for (int i = 0; i < m_numAllocations; ++i) {
            char buf[512] = "";
            if (m_allocations[i].ptr != nullptr) {
                auto& info = m_allocations[i];
                GT_LOG_WARNING("Memory", "Leaky allocation, %lli bytes leaked, allocated from\n%s(%lli)", info.size, info.info.file, info.info.line);
            }
        }
    }

    inline void SetArena(fnd::memory::MemoryArenaBase* arena)
    {
        m_arena = arena;
    }

    inline void SetName(const char* name) { m_name = name; }
    inline const char* GetName() { return m_name; }

    inline ExtendedMemoryTracker* GetNext() { return m_next; }

    GT_FORCE_INLINE void TrackAllocation(void* memory, size_t size, size_t alignment, fnd::SourceInfo scInfo)
    {
        if (!m_allocations || m_numAllocations == m_capacity) {
            if (!m_arena) { return; }
            if (m_allocations) {
                GT_DELETE_ARRAY(m_allocations, m_arena);
            }
            using Type = AllocInfo;
            auto arenaAsPtr = m_arena;
            auto count = m_capacity + 1024;
            m_allocations = GT_NEW_ARRAY(AllocInfo, m_capacity + 1024, m_arena);
        }
        AllocInfo info;
        info.alignment = alignment;
        info.size = size;
        info.ptr = memory;
        info.info = scInfo;
        m_allocations[m_numAllocations++] = info;
    }

    GT_FORCE_INLINE void UntrackAllocation(void* memory, size_t size)
    {
        for (int i = 0; i < m_numAllocations; ++i) {
            if (m_allocations[i].ptr == memory) {
                m_allocations[i].ptr = nullptr;
            }
        }
    }

    inline size_t GetUsedMemorySize()
    {
        size_t result = 0;
        for (int i = 0; i < m_numAllocations; ++i) {
            if (m_allocations[i].ptr != nullptr) {
                result += m_allocations[i].size;
            }
        }
        return result;
    }
};


#ifdef GT_DEVELOPMENT
typedef fnd::memory::SimpleTrackingArena<fnd::memory::TLSFAllocator, ExtendedMemoryTracker> HeapArena;
typedef fnd::memory::SimpleTrackingArena<fnd::memory::LinearAllocator, ExtendedMemoryTracker> LinearArena;
#else
typedef fnd::memory::SimpleMemoryArena<fnd::memory::TLSFAllocator>    HeapArena;
typedef fnd::memory::SimpleMemoryArena<fnd::memory::LinearAllocator>  LinearArena;
#endif

class SimpleFilterPolicy
{
public:
    bool Filter(fnd::logging::LogCriteria criteria)
    {
        return criteria.channel.hash != fnd::logging::LogChannel("Renderer").hash;
    }
};

class SimpleFormatPolicy
{
public:
    void Format(char* buf, size_t bufSize, fnd::logging::LogCriteria criteria, const char* format, va_list args)
    {
        size_t offset = snprintf(buf, bufSize, "[%s]    ", criteria.channel.str);
        vsnprintf(buf + offset, bufSize - offset, format, args);
    }
};


class IDEConsoleFormatter
{
public:
    void Format(char* buf, size_t bufSize, fnd::logging::LogCriteria criteria, const char* format, va_list args)
    {
        size_t offset = snprintf(buf, bufSize, "%s(%lli): [%s]    ", criteria.scInfo.file, criteria.scInfo.line, criteria.channel.str);
        vsnprintf(buf + offset, bufSize - offset, format, args);
    }
};

class IDEConsoleWriter
{
public:
    void Write(const char* msg)
    {
#ifdef _MSC_VER
        OutputDebugStringA(msg);
        OutputDebugStringA("\n");
#endif
    }
};

class ConsoleWriter
{
public:
    void Write(const char* msg)
    {
        printf("%s\n", msg);
    }
};

#include <foundation/sockets/sockets.h>

class NetworkFilterPolicy
{
public:
    bool Filter(fnd::logging::LogCriteria criteria)
    {
        return criteria.channel.hash != fnd::logging::LogChannel("TCP Logger").hash
            && criteria.channel.hash != fnd::logging::LogChannel("Renderer").hash;
    }
};

class NetworkFormatPolicy
{
public:
    void Format(char* buf, size_t bufSize, fnd::logging::LogCriteria criteria, const char* format, va_list args)
    {
        size_t offset = snprintf(buf, bufSize, "[%s]    ", criteria.channel.str);
        vsnprintf(buf + offset, bufSize - offset, format, args);
    }
};

class TCPWriter
{
    fnd::sockets::TCPConnectionSocket m_socket;
public:
    TCPWriter() = default;
    TCPWriter(fnd::sockets::TCPConnectionSocket socket)
        :   m_socket(socket) {}
    void Write(const char* msg)
    {
        if (m_socket.IsConnected()) {
            m_socket.Send(const_cast<char*>(msg), strlen(msg) + 1);
        }
    }
    
};

class NetworkLogger : public fnd::logging::Logger<NetworkFilterPolicy, NetworkFormatPolicy, TCPWriter>
{
public:
    NetworkLogger() = default;
    void SetSocket(fnd::sockets::TCPConnectionSocket socket)
    {
        m_writer = TCPWriter(socket);
    }
};
    
    
typedef fnd::logging::Logger<SimpleFilterPolicy, SimpleFormatPolicy, ConsoleWriter> SimpleLogger;
typedef fnd::logging::Logger<SimpleFilterPolicy, IDEConsoleFormatter, IDEConsoleWriter> IDEConsoleLogger;


#define GIGABYTES(n) (MEGABYTES(n) * (size_t)1024)
#define MEGABYTES(n) (KILOBYTES(n) * 1024)
#define KILOBYTES(n) (n * 1024)

static_assert(GIGABYTES(8) > MEGABYTES(4), "some size type is wrong");

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

static ID3D11Device*            g_pd3dDevice = NULL;
static ID3D11DeviceContext*     g_pd3dDeviceContext = NULL;
static IDXGISwapChain*          g_pSwapChain = NULL;
static ID3D11RenderTargetView*  g_mainRenderTargetView = NULL;

void ImGui_Style_SetDark(float alpha_)
{
    ImGuiStyle& style = ImGui::GetStyle();

    // light style from Pac�me Danhiez (user itamago) https://github.com/ocornut/imgui/pull/511#issuecomment-175719267
    style.Alpha = 1.0f;
    style.FrameRounding = 3.0f;
    style.Colors[ImGuiCol_Text] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, 0.94f);
    style.Colors[ImGuiCol_ChildWindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.94f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.94f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 1.00f, 1.00f, 0.51f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.98f, 0.98f, 0.98f, 0.53f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.69f, 0.69f, 0.69f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
    style.Colors[ImGuiCol_ComboBg] = ImVec4(0.86f, 0.86f, 0.86f, 0.99f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_Column] = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    style.Colors[ImGuiCol_ColumnHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
    style.Colors[ImGuiCol_ColumnActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.50f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    style.Colors[ImGuiCol_CloseButton] = ImVec4(0.59f, 0.59f, 0.59f, 0.50f);
    style.Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
    style.Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    style.Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);


    for (int i = 0; i <= ImGuiCol_COUNT; i++)
    {
        ImVec4& col = style.Colors[i];
        float H, S, V;
        ImGui::ColorConvertRGBtoHSV(col.x, col.y, col.z, H, S, V);

        if (S < 0.1f)
        {
            V = 1.0f - V;
        }
        ImGui::ColorConvertHSVtoRGB(H, S, V, col.x, col.y, col.z);
        if (col.w < 1.00f)
        {
            col.w *= alpha_;
        }
    }
}



void CreateRenderTarget()
{
    DXGI_SWAP_CHAIN_DESC sd;
    g_pSwapChain->GetDesc(&sd);

    // Create the render target
    ID3D11Texture2D* pBackBuffer;
    D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc;
    ZeroMemory(&render_target_view_desc, sizeof(render_target_view_desc));
    render_target_view_desc.Format = sd.BufferDesc.Format;
    render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, &render_target_view_desc, &g_mainRenderTargetView);
    //g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

HRESULT CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    }

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 1, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return E_FAIL;

    CreateRenderTarget();

    return S_OK;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}



double PCFreq = 0.0;
__int64 CounterStart = 0;

void StartCounter()
{
    LARGE_INTEGER li;
    if (!QueryPerformanceFrequency(&li)) {
        GT_LOG_INFO("Timing", "Shit");
    }

    PCFreq = double(li.QuadPart);

    QueryPerformanceCounter(&li);
    CounterStart = li.QuadPart;
}
double GetCounter()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return double(li.QuadPart - CounterStart) / PCFreq;
}


extern LRESULT ImGui_ImplDX11_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplDX11_WndProcHandler(hWnd, msg, wParam, lParam)) {
        //return true;
    }
    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}


template <class T>
struct PrintFormatString
{
    static constexpr char* Get()
    {
        return "";
    }
};

template <>
struct PrintFormatString<float>
{
    static constexpr char* Get()
    {
        return "%f";
    }
};
template <>
struct PrintFormatString<int>
{
    static constexpr char* Get()
    {
        return "%i";
    }
};
template <>
struct PrintFormatString<double>
{
    static constexpr char* Get()
    {
        return "%f";
    }
};


template <class TElement, size_t ELEMENT_COUNT>
void PrintVector(const fnd::math::Vector<TElement, ELEMENT_COUNT>& vec)
{
    printf("{ ");
    for (size_t i = 0; i < ELEMENT_COUNT; ++i) {
        printf(PrintFormatString<TElement>().Get(), vec.elements[i]);
        if (i < (ELEMENT_COUNT - 1)) {
            printf(", ");
        }
    }
    printf(" }\n");
}


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

#ifdef _MSC_VER
#ifdef GT_DEBUG
#pragma comment(lib, "sodium-debug.lib")
#else
#pragma comment(lib, "sodium-release.lib")
#endif
#endif
#include <engine/runtime/netcode_io/netcode.h>

static uint8_t private_key[NETCODE_KEY_BYTES] = { 0x60, 0x6a, 0xbe, 0x6e, 0xc9, 0x19, 0x10, 0xea,
0x9a, 0x65, 0x62, 0xf6, 0x6f, 0x2b, 0x30, 0xe4,
0x43, 0x71, 0xd6, 0x2c, 0xd1, 0x99, 0x27, 0x26,
0x6b, 0x3c, 0x60, 0xf4, 0xb7, 0x15, 0xab, 0xa1 };


namespace gfx
{
    //
    //  @TODO Resource handling, actual abstraction...
    //
    
    /*
        @NOTE

        -> resource types:
            -> Buffers (vertex, index, constant... treat generic if possible (usage hints/flags)?
            -> Textures / Surfaces - unified concept for textures and render targets?
            -> Shaders / Pipeline State (blend/raster/whatever state, input layout, etc) - split pipeline state up into blend/raster/etc states and shader bindings?
            -> UAVs, structured buffers...?
        -> need API to
            -> create/destroy resources
            -> update resource data
            -> transition resource state? automate behind the scenes?
    */


    static const size_t MAX_VERTEX_STREAMS = 8;
    static const size_t MAX_CONSTANT_BUFFERS = 8;
    static const size_t MAX_RENDER_TARGETS = 8;

    enum class RenderCmdType : uint16_t
    {
        DRAW_BATCH,
        UPDATE_BUFFER_DATA
    };

    typedef void(*RenderCmdDispatchFunc)(void*);

    struct RenderCmd
    {
        RenderCmdType           type;
        uint32_t                size;
        RenderCmdDispatchFunc   Dispatch;
    };

    struct D3D11VertexBuffer
    {
        ID3D11Buffer*   buffer;
        uint32_t        stride;
        uint32_t        offset;
    };

    struct D3D11IndexBuffer
    {
        ID3D11Buffer*   buffer;
        DXGI_FORMAT     format;
        uint32_t        offset;
    };

    struct D3D11ConstantBuffer
    {
        ID3D11Buffer*   buffer;
    };

    namespace commands {

        struct DrawBatchCmd
        {
            static const RenderCmdType Type = RenderCmdType::DRAW_BATCH;
            static RenderCmdDispatchFunc Dispatch;

            uint32_t        numVertexBuffers;
            ID3D11Buffer*   vertexBuffers[MAX_VERTEX_STREAMS];
            uint32_t        vertexBufferStrides[MAX_VERTEX_STREAMS];
            uint32_t        vertexBufferOffsets[MAX_VERTEX_STREAMS];
            
            D3D11_PRIMITIVE_TOPOLOGY vertexTopology;

            ID3D11Buffer*   indexBuffer;
            uint32_t        indexBufferOffset;
            DXGI_FORMAT     indexFormat;
            uint32_t        indexCount;

            uint32_t        numConstantBuffers;
            ID3D11Buffer*   constantBuffers[MAX_CONSTANT_BUFFERS];
            uint32_t        constantBufferOffsets[MAX_CONSTANT_BUFFERS];
            uint32_t        constantBufferSizes[MAX_CONSTANT_BUFFERS];

            ID3D11VertexShader*     vertexShader;
            ID3D11PixelShader*      pixelShader;

            ID3D11InputLayout*      inputLayout;
        };

        struct UpdateBufferDataCmd
        {
            static const RenderCmdType Type = RenderCmdType::UPDATE_BUFFER_DATA;
            static RenderCmdDispatchFunc Dispatch;

            ID3D11Buffer*   targetBuffer;

            void*           data;
            uint32_t        numBytes;
        };
    }

    namespace dispatch {

        void DispatchDrawBatchCmd(void* data)
        {
            auto cmd = static_cast<commands::DrawBatchCmd*>(data);
            
            g_pd3dDeviceContext->IASetInputLayout(cmd->inputLayout);
            g_pd3dDeviceContext->IASetVertexBuffers(0, cmd->numVertexBuffers, cmd->vertexBuffers, cmd->vertexBufferStrides, cmd->vertexBufferOffsets);
            g_pd3dDeviceContext->IASetPrimitiveTopology(cmd->vertexTopology);
            g_pd3dDeviceContext->IASetIndexBuffer(cmd->indexBuffer, cmd->indexFormat, cmd->indexBufferOffset);
            
            g_pd3dDeviceContext->VSSetShader(cmd->vertexShader, nullptr, 0);
            g_pd3dDeviceContext->PSSetShader(cmd->pixelShader, nullptr, 0);
            
            g_pd3dDeviceContext->VSSetConstantBuffers(0, cmd->numConstantBuffers, cmd->constantBuffers);
            g_pd3dDeviceContext->PSSetConstantBuffers(0, cmd->numConstantBuffers, cmd->constantBuffers);
            
            g_pd3dDeviceContext->DrawIndexed(cmd->indexCount, 0, 0);
        }

        void DispatchUpdateBufferDataCmd(void* data)
        {
            auto cmd = static_cast<commands::UpdateBufferDataCmd*>(data);

            D3D11_MAPPED_SUBRESOURCE resource;
            HRESULT res = S_OK;
            if((res = g_pd3dDeviceContext->Map(cmd->targetBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &resource)) != S_OK) {}
            memcpy(resource.pData, cmd->data, cmd->numBytes);
            g_pd3dDeviceContext->Unmap(cmd->targetBuffer, 0);
        }
    }

    RenderCmdDispatchFunc commands::DrawBatchCmd::Dispatch = dispatch::DispatchDrawBatchCmd;
    RenderCmdDispatchFunc commands::UpdateBufferDataCmd::Dispatch = dispatch::DispatchUpdateBufferDataCmd;


    enum class RenderTargetAction : uint16_t
    {
        CLEAR_TO_COLOR,
        NONE
    };

   

    struct RenderPass
    {
        uint32_t                    numRenderTargets;
        ID3D11RenderTargetView*     renderTargets[MAX_RENDER_TARGETS];

        RenderTargetAction          beginAction;

        fnd::math::float4           clearColor;
        D3D11_VIEWPORT              viewport;
        
    };


    class CommandBuffer
    {
        char* m_bufferStart;
        char* m_next;
        size_t m_bufferSize;
        size_t m_numCommands = 0;
    public:
        CommandBuffer(void* buffer, size_t bufferSize)
            :   m_bufferStart(reinterpret_cast<char*>(buffer)),
                m_next(reinterpret_cast<char*>(buffer)),
                m_bufferSize(bufferSize) {}

        void Flush()
        {
            m_numCommands = 0;
            m_next = m_bufferStart;
        }

        template <class TCmd>
        TCmd* AllocateCommand()
        {
            size_t totalSize = sizeof(RenderCmd) + sizeof(TCmd);
            if ((reinterpret_cast<uintptr_t>(m_next + totalSize) - reinterpret_cast<uintptr_t>(m_bufferStart)) > m_bufferSize) {
                return nullptr;
            }
            union {
                RenderCmd* as_cmd_header;
                TCmd* as_cmd;
                char* as_char;
            };
            as_char = m_next;
            as_cmd_header->type = TCmd::Type;
            as_cmd_header->Dispatch = TCmd::Dispatch;
            as_cmd_header->size = sizeof(TCmd);

            as_cmd_header++;
            
            as_cmd = GT_PLACEMENT_NEW(as_cmd) TCmd();
            m_next += totalSize;
            m_numCommands++;
            return as_cmd;
        }

        RenderCmd* GetCommands(size_t* numCommands)
        {
            *numCommands = m_numCommands;
            return reinterpret_cast<RenderCmd*>(m_bufferStart);
        }
    };

    void SubmitCommandBuffers(RenderPass* renderPass, CommandBuffer** buffers, size_t numBuffers)
    {
        g_pd3dDeviceContext->OMSetRenderTargets(renderPass->numRenderTargets, renderPass->renderTargets, nullptr);
        switch (renderPass->beginAction) {
            case RenderTargetAction::CLEAR_TO_COLOR: {
                for (uint32_t i = 0; i < renderPass->numRenderTargets; ++i) {
                    g_pd3dDeviceContext->ClearRenderTargetView(renderPass->renderTargets[i], static_cast<float*>(renderPass->clearColor));
                }
            } break;
            default: {

            } break;
        }

        g_pd3dDeviceContext->RSSetViewports(1, &renderPass->viewport);

        for (size_t i = 0; i < numBuffers; ++i) {
            auto buffer = buffers[i];
            size_t numCommands = 0;
            union {
                RenderCmd* as_cmd_header;
                char* as_char;
                void* as_void;
            };
            as_cmd_header = buffer->GetCommands(&numCommands);
            while (numCommands > 0) {
                void* cmd = as_char + sizeof(RenderCmd);
                as_cmd_header->Dispatch(cmd);
                as_char += as_cmd_header->size + sizeof(RenderCmd);
                --numCommands;
            }
        }

    }
        
}


struct Task
{
    void* data = nullptr;
    void(*Func)(void*) = nullptr;
};

struct WorkerThread
{
    Task task;
};


#ifdef GT_SHARED_LIB
#ifdef _MSC_VER
#define GT_RUNTIME_API extern "C" __declspec(dllexport)
#else
#define GT_RUNTIME_API
#endif
#else
#define GT_RUNTIME_API
#endif


#define GT_TOOL_SERVER_PORT 8080
#define GT_MAX_TOOL_CONNECTIONS 32

class ToolServer
{
    fnd::sockets::TCPListenSocket       m_listenSocket;
    size_t                              m_maxNumConnections = 0;
    fnd::sockets::TCPConnectionSocket*  m_connections = nullptr;
    bool*                               m_slotIsFree = nullptr;
    fnd::memory::MemoryArenaBase*       m_memoryArena = nullptr;

    char*                               m_receiveBuffer = nullptr;
    NetworkLogger*                      m_tcpLoggers = nullptr;

    bool AddConnection(fnd::sockets::Address* address, fnd::sockets::TCPConnectionSocket* connection)
    {
        
        GT_LOG_INFO("Tools Server", "New connection from { %d.%d.%d.%d:%d }", address->GetA(), address->GetB(), address->GetC(), address->GetD(), address->GetPort());
        size_t index = m_maxNumConnections + 1;
        for (size_t i = 0; i < m_maxNumConnections; ++i) {
            if (m_slotIsFree[i]) {
                index = i;
                break;
            }
        }
        if (index > m_maxNumConnections) { 
            GT_LOG_WARNING("Tools Server", "Too many open connections, rejecting");
            connection->Close();
            return false; 
        }
        m_slotIsFree[index] = false;
        m_connections[index] = *connection;
        m_tcpLoggers[index].SetSocket(*connection);
        return true;
    }

public:
    static const size_t                 RECEIVE_BUFFER_SIZE = MEGABYTES(5);

    bool Start(fnd::memory::MemoryArenaBase* arena, Port port, size_t maxConnections)
    {
        m_memoryArena = arena;
        if (maxConnections > GT_MAX_TOOL_CONNECTIONS || arena == nullptr) {
            return false;
        }
        m_maxNumConnections = maxConnections;
        m_connections = GT_NEW_ARRAY(fnd::sockets::TCPConnectionSocket, maxConnections, arena);
        m_slotIsFree = GT_NEW_ARRAY(bool, maxConnections, arena);
        m_tcpLoggers = GT_NEW_ARRAY(NetworkLogger, maxConnections, arena);
        for (size_t i = 0; i < maxConnections; ++i) { m_slotIsFree[i] = true; }
        m_receiveBuffer = reinterpret_cast<char*>(arena->Allocate(RECEIVE_BUFFER_SIZE, 16, GT_SOURCE_INFO));
        return m_listenSocket.Listen(port, maxConnections);
    }

    void Tick()
    {
        if (!m_listenSocket.IsListening()) { return; }
        fnd::sockets::TCPConnectionSocket incomingConnection;
        fnd::sockets::Address incomingAddress;
        if (m_listenSocket.HasConnection(&incomingAddress, &incomingConnection)) {
            AddConnection(&incomingAddress, &incomingConnection);
        }
        for (size_t i = 0; i < m_maxNumConnections; ++i) {
            auto& connection = m_connections[i];
            size_t numReceivedBytes = connection.Receive(m_receiveBuffer, RECEIVE_BUFFER_SIZE);
            if (numReceivedBytes > 0) {
                GT_LOG_DEBUG("Tools Server", "Received msg: %s", m_receiveBuffer);
            }
        }
    }
};

GT_RUNTIME_API
int win32_main(int argc, char* argv[])
{
    fnd::sockets::InitializeSocketLayer();
    fnd::sockets::UDPSocket socket;
   
    using namespace fnd;

#ifdef GT_DEVELOPMENT
    SimpleLogger logger;
    IDEConsoleLogger ideLogger;
#endif
    
    GT_LOG_INFO("Application", "Initialized logging systems");

#ifdef GT_DEVELOPMENT
    const size_t debugHeapSize = MEGABYTES(500);
    memory::TLSFAllocator debugAllocator(malloc(debugHeapSize), debugHeapSize);
    HeapArena debugArena(&debugAllocator);

    debugArena.GetTrackingPolicy()->SetName("Debug Heap");
#endif

    const size_t reservedMemorySize = GIGABYTES(2);
    void* reservedMemory = malloc(reservedMemorySize);

    memory::LinearAllocator applicationAllocator(reservedMemory, reservedMemorySize);
    LinearArena applicationArena(&applicationAllocator);
#ifdef GT_DEVELOPMENT
    applicationArena.GetTrackingPolicy()->SetName("Application Stack");
    applicationArena.GetTrackingPolicy()->SetArena(&debugArena);
#endif

    static const size_t sandboxedHeapSize = MEGABYTES(500);     // 0.5 gigs of memory for free form allocations @TODO subdivide further for individual 3rd party libs etc
    void* sandboxedHeap = applicationArena.Allocate(sandboxedHeapSize, 4, GT_SOURCE_INFO);

    memory::TLSFAllocator sandboxAllocator(sandboxedHeap, sandboxedHeapSize);
    HeapArena sandboxArena(&sandboxAllocator);
#ifdef GT_DEVELOPMENT
    sandboxArena.GetTrackingPolicy()->SetName("Sandbox Heap");
    sandboxArena.GetTrackingPolicy()->SetArena(&debugArena);
#endif

    GT_LOG_INFO("Application", "Initialized memory systems");
    
   
    const size_t NUM_WORKER_THREADS = 4;
    WorkerThread workerThreads[NUM_WORKER_THREADS];
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
        CreateThread(NULL, 0, [](void* data) -> DWORD {
            do {
                auto worker = static_cast<WorkerThread*>(data);
                if (worker->task.Func != nullptr) {
                    worker->task.Func(worker->task.data);

                    worker->task.Func = nullptr;
                    worker->task.data = nullptr;
                }
            } while (true);
            return 0;
        }, reinterpret_cast<void*>(&workerThreads[i]), 0, NULL);
    }
    GT_LOG_INFO("Application", "Created %lli worker threads", NUM_WORKER_THREADS);

    //
    
    ToolServer toolServer;
    if (!toolServer.Start(&applicationArena, GT_TOOL_SERVER_PORT, GT_MAX_TOOL_CONNECTIONS)) {
        GT_LOG_ERROR("Application", "Failed to initialize tools server");
    }
    
    //
    bool exitFlag = false;
    bool restartFlag = false;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, _T("void"), NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, _T("void"), _T("void"), WS_OVERLAPPEDWINDOW, 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, wc.hInstance, NULL);

    if (!hwnd) {
        GT_LOG_ERROR("Application", "failed to create a window\n");
        return 1;
    }

    GT_LOG_INFO("Application", "Created application window");

    const float bgColor[] = { 100.0f / 255.0f, 149.0f / 255.0f, 237.0f / 255.0f };

    // Initialize Direct3D
    if (CreateDeviceD3D(hwnd) < 0)
    {
        CleanupDeviceD3D();
        UnregisterClass(_T("void"), wc.hInstance);
        return 1;
    }

    GT_LOG_INFO("Application", "Initialized gfx device");

    ImGui::GetIO().UserData = &sandboxArena;
    ImGui::GetIO().MemAllocFn = [](size_t size) -> void* {
        auto arena = static_cast<HeapArena*>(ImGui::GetIO().UserData);
        return arena->Allocate(size, 4, GT_SOURCE_INFO);
    };
    ImGui::GetIO().MemFreeFn = [](void* ptr) -> void {
        auto arena = static_cast<HeapArena*>(ImGui::GetIO().UserData);
        arena->Free(ptr);
    };

    ImGui_ImplDX11_Init(hwnd, g_pd3dDevice, g_pd3dDeviceContext);

    ImGui_Style_SetDark(0.8f);

    GT_LOG_INFO("Application", "Initialized UI");

    // Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);


    //
    struct Vertex
    {
        math::float4 position;
        math::float4 color;
    };

    Vertex triangleVertices[] = {
        { { 0.0f, 0.5f, 0.0f, 1.0f },{ 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.45f, -0.5, 0.0f, 1.0f },{ 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.45f, -0.5f, 0.0f, 1.0f },{ 0.0f, 0.0f, 1.0f, 1.0f } }
    };

    uint16_t triangleIndices[] = {
        0, 1, 2
    };

    ID3D11Buffer* vBuffer = nullptr;
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        ZeroMemory(&bufferDesc, sizeof(bufferDesc));

        bufferDesc.Usage = D3D11_USAGE::D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER;
        bufferDesc.ByteWidth = sizeof(Vertex) * 3;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        auto result = g_pd3dDevice->CreateBuffer(&bufferDesc, nullptr, &vBuffer);
        if (result != S_OK) {
            GT_LOG_ERROR("D3D11", "failed to create vertex buffer\n");
        }
    }

    ID3D11Buffer* iBuffer = nullptr;
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        ZeroMemory(&bufferDesc, sizeof(bufferDesc));

        bufferDesc.Usage = D3D11_USAGE::D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_INDEX_BUFFER;
        bufferDesc.ByteWidth = sizeof(uint16_t) * 3;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;


        auto result = g_pd3dDevice->CreateBuffer(&bufferDesc, nullptr, &iBuffer);
        if (result != S_OK) {
            GT_LOG_ERROR("D3D11", "failed to create index buffer\n");
        }
    }

    ID3D11VertexShader* vShader;
    ID3D11PixelShader* fShader;

    size_t vShaderCodeSize = 0;
    char* vShaderCode = static_cast<char*>(LoadFileContents("VertexShader.cso", &applicationArena, &vShaderCodeSize));
    if (!vShaderCode) {
        GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
    }

    size_t fShaderCodeSize = 0;
    char* fShaderCode = static_cast<char*>(LoadFileContents("PixelShader.cso", &applicationArena, &fShaderCodeSize));
    if (!fShaderCode) {
        GT_LOG_ERROR("D3D11", "Failed to load pixel shader\n");
    }

    auto vRes = g_pd3dDevice->CreateVertexShader(vShaderCode, vShaderCodeSize, nullptr, &vShader);
    auto fRes = g_pd3dDevice->CreatePixelShader(fShaderCode, fShaderCodeSize, nullptr, &fShader);

    D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
        { "POSITION", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT::DXGI_FORMAT_R32G32B32A32_FLOAT, 0, sizeof(math::float4), D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    ID3D11InputLayout* inputLayout;

    auto res = g_pd3dDevice->CreateInputLayout(inputDesc, 2, vShaderCode, vShaderCodeSize, &inputLayout);
    if (res != S_OK) {
        GT_LOG_ERROR("D3D11", "failed to create input layout\n");
    }


    const size_t COMMAND_BUFFER_SIZE = sizeof(gfx::commands::DrawBatchCmd) * 10000;
    char* cmdBufferSpace = (char*)applicationArena.Allocate(COMMAND_BUFFER_SIZE * 4, 16, GT_SOURCE_INFO);
    gfx::CommandBuffer commandBuffer0(cmdBufferSpace, COMMAND_BUFFER_SIZE);
    gfx::CommandBuffer commandBuffer1(cmdBufferSpace + COMMAND_BUFFER_SIZE, COMMAND_BUFFER_SIZE);
    gfx::CommandBuffer commandBuffer2(cmdBufferSpace + COMMAND_BUFFER_SIZE * 2, COMMAND_BUFFER_SIZE);
    gfx::CommandBuffer commandBuffer3(cmdBufferSpace + COMMAND_BUFFER_SIZE * 3, COMMAND_BUFFER_SIZE);

    
    gfx::RenderPass mainRenderPass;
    mainRenderPass.beginAction = gfx::RenderTargetAction::CLEAR_TO_COLOR;
    mainRenderPass.clearColor = math::float4(bgColor[0], bgColor[1], bgColor[2], 1.0f);
    mainRenderPass.numRenderTargets = 1;
    mainRenderPass.renderTargets[0] = g_mainRenderTargetView;
    mainRenderPass.viewport = { 0 };
    mainRenderPass.viewport.TopLeftX = 0;
    mainRenderPass.viewport.TopLeftY = 0;
    mainRenderPass.viewport.Width = WINDOW_WIDTH;
    mainRenderPass.viewport.Height = WINDOW_HEIGHT;

    GT_LOG_INFO("Application", "Initialized graphics scene");

    ///
    StartCounter();

    double t = 0.0;
    double dt = 1.0 / 60.0;

    double currentTime = GetCounter();
    double accumulator = 0.0;

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    GT_LOG_INFO("Application", "Starting main loop");
    do {
        double newTime = GetCounter();
        double frameTime = newTime - currentTime;
        if (frameTime > 0.25) {
            //std::cout << "!\n";
            frameTime = 0.25;
        }
        currentTime = newTime;
        accumulator += frameTime;

        while (accumulator >= dt) {

            static char buffer[512];
            if (socket.IsOpen()) {
                fnd::sockets::Address sender;
                size_t bytesRead = socket.Receive(&sender, buffer, 512);
                if (bytesRead > 0) {
                    GT_LOG_INFO("Network", "Received msg via UDP from %d.%d.%d.%d:%d:", sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort());
                    GT_LOG_INFO("Network", "msg was: %s", buffer);
                }
            }

            toolServer.Tick();

            /* Begin sim frame*/
            while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) {
                    exitFlag = true;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                continue;
            }

            if (msg.message == WM_QUIT) { exitFlag = true; }

            ImGui_ImplDX11_NewFrame();

#ifdef GT_DEVELOPMENT
            if (ImGui::Begin("Memory usage", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

                size_t totalSize = 0;
                auto it = GetMemTrackerListHead();
                while (it != nullptr) {

                    totalSize += it->GetUsedMemorySize();
                    if (ImGui::TreeNode(it->GetName())) {
                        ImGui::Text("%lli kB allocated", it->GetUsedMemorySize() / 1024);
                        ImGui::TreePop();
                    }

                    it = it->GetNext();
                }

                ImGui::Separator();
                ImGui::Text("Total usage: %lli kb", totalSize / 1024);
            } ImGui::End();
#endif
            if (ImGui::Begin("Maths!")) {
                using namespace math;

                static float4 fl4;
                static float3 fl3;
                static float2 fl2;

                static int4 i4;
                static int3 i3;
                static int2 i2;

                ImGui::DragFloat4("fl4", static_cast<float*>(fl4));
                ImGui::DragFloat3("fl3", static_cast<float*>(fl3));
                ImGui::DragFloat2("fl2", static_cast<float*>(fl2));

                ImGui::Text("float4 arithmetics");
                {
                    static float4 a;
                    static float4 b;
                    static float4 c;

                    ImGui::PushID("float4");
                    ImGui::DragFloat4("a", static_cast<float*>(a));
                    ImGui::SameLine();
                    if (ImGui::Button("Normalize##a")) {
                        a = Normalize(a);
                    }
                    ImGui::SameLine();
                    ImGui::Text("Length is %f", Length(a));

                    ImGui::DragFloat4("b", static_cast<float*>(b));
                    ImGui::SameLine();
                    if (ImGui::Button("Normalize##b")) {
                        b = Normalize(b);
                    }
                    ImGui::SameLine();
                    ImGui::Text("Length is %f", Length(b));

                    if (ImGui::Button("a + b")) {
                        c = a + b;
                    } ImGui::SameLine();
                    if (ImGui::Button("a - b")) {
                        c = a - b;
                    } ImGui::SameLine();
                    if (ImGui::Button("a * b")) {
                        c = a * b;
                    } ImGui::SameLine();
                    if (ImGui::Button("a / b")) {
                        c = a / b;
                    }
                    ImGui::DragFloat4("c", static_cast<float*>(c));

                    float dot = Dot(a, b);
                    ImGui::SliderFloat("dot (a, b)", &dot, -1.0f, 1.0f);
                    ImGui::PopID();
                }
                ImGui::Text("float3 arithmetics");
                {
                    static float3 a;
                    static float3 b;
                    static float3 c;

                    ImGui::PushID("float3");
                    ImGui::DragFloat3("a", static_cast<float*>(a));
                    ImGui::SameLine();
                    if (ImGui::Button("Normalize##a")) {
                        a = Normalize(a);
                    }
                    ImGui::SameLine();
                    ImGui::Text("Length is %f", Length(a));

                    ImGui::DragFloat3("b", static_cast<float*>(b));
                    ImGui::SameLine();
                    if (ImGui::Button("Normalize##b")) {
                        b = Normalize(b);
                    }
                    ImGui::SameLine();
                    ImGui::Text("Length is %f", Length(b));

                    if (ImGui::Button("a + b")) {
                        c = a + b;
                    } ImGui::SameLine();
                    if (ImGui::Button("a - b")) {
                        c = a - b;
                    } ImGui::SameLine();
                    if (ImGui::Button("a * b")) {
                        c = a * b;
                    } ImGui::SameLine();
                    if (ImGui::Button("a / b")) {
                        c = a / b;
                    }
                    ImGui::DragFloat3("c", static_cast<float*>(c));

                    float dot = Dot(a, b);
                    ImGui::SliderFloat("dot (a, b)", &dot, -1.0f, 1.0f);
                    ImGui::PopID();
                }
                ImGui::Text("float2 arithmetics");
                {
                    static float2 a;
                    static float2 b;
                    static float2 c;

                    ImGui::PushID("float2");
                    ImGui::DragFloat2("a", static_cast<float*>(a));
                    ImGui::SameLine();
                    if (ImGui::Button("Normalize##a")) {
                        a = Normalize(a);
                    }
                    ImGui::SameLine();
                    ImGui::Text("Length is %f", Length(a));

                    ImGui::DragFloat2("b", static_cast<float*>(b));
                    ImGui::SameLine();
                    if (ImGui::Button("Normalize##b")) {
                        b = Normalize(b);
                    }
                    ImGui::SameLine();
                    ImGui::Text("Length is %f", Length(b));

                    if (ImGui::Button("a + b")) {
                        c = a + b;
                    } ImGui::SameLine();
                    if (ImGui::Button("a - b")) {
                        c = a - b;
                    } ImGui::SameLine();
                    if (ImGui::Button("a * b")) {
                        c = a * b;
                    } ImGui::SameLine();
                    if (ImGui::Button("a / b")) {
                        c = a / b;
                    }
                    ImGui::DragFloat2("c", static_cast<float*>(c));

                    float dot = Dot(a, b);
                    ImGui::SliderFloat("dot (a, b)", &dot, -1.0f, 1.0f);
                    ImGui::PopID();
                }


            } ImGui::End();


            if (ImGui::Begin("Networking", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                Port port = 0;
                static Port targetPort = 8080;
                ImGui::InputInt("Port", reinterpret_cast<int*>(&targetPort));
                if (!socket.IsOpen(&port)) {
                    if (ImGui::Button("Open socket")) {
                        if (!socket.Open(targetPort)) {
                            GT_LOG_ERROR("Network", "failed to open UDP socket on port %d", targetPort);
                        }
                        else {
                            GT_LOG_INFO("Network", "opened UDP socket on port %d", targetPort);
                        }
                    } 
                }
                else {
                    ImGui::Text("Listening on port %d", port);
                    if (ImGui::Button("Disconnect")) {
                        socket.Close();
                        GT_LOG_INFO("Network", "closed UDP socket");
                    } ImGui::SameLine();
                    static int a = 127, b = 0, c = 0, d = 1;
                    static Port remotePort = 8080;
                    if (ImGui::Button("Send Hello World")) {
                        fnd::sockets::Address addr(a, b, c, d, remotePort);
                        socket.Send(&addr, "Hello World", strlen("Hello World"));
                    }
                    ImGui::Text("Remote address");
                    ImGui::PushItemWidth(100.0f);
                    ImGui::InputInt("##a", &a, 1, 100);
                    ImGui::SameLine();
                    ImGui::InputInt("##b", &b);
                    ImGui::SameLine();
                    ImGui::InputInt("##c", &c);
                    ImGui::SameLine();
                    ImGui::InputInt("##d", &d);
                    ImGui::SameLine();
                    ImGui::InputInt("Remote port", reinterpret_cast<int*>(&remotePort));
                    ImGui::PopItemWidth();
                    a = a >= 0 ? (a <= 255 ? a : 255) : 0;
                    b = b >= 0 ? (b <= 255 ? b : 255) : 0;
                    c = c >= 0 ? (c <= 255 ? c : 255) : 0;
                    d = d >= 0 ? (d <= 255 ? d : 255) : 0;
                }
            } ImGui::End();


            /* Basic UI: frame statistics */
            ImGui::SetNextWindowPos(ImVec2(10.0f, ImGui::GetIO().DisplaySize.y - 50));
            ImGui::Begin("#framestatistics", (bool*)0, ImVec2(0, 0), 0.45f, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
            ImGui::Text("Simulation time average: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();

            float fCounter = static_cast<float>(GetCounter());
            triangleVertices[0].position = math::float4(0.0f, 0.5f, 0.0f, 1.0f) * math::Sin(fCounter) * math::Cos(fCounter);
            //triangleVertices[1].position = math::float4(0.45f, -0.5, 0.0f, 1.0f) * math::Cos(fCounter);

            /* End sim frame */
            ImGui::Render();
            t += dt;
            accumulator -= dt;
        }

        /* Begin render frame*/

        auto renderFrameTimerStart = GetCounter();
        auto cmdRecordingTimerStart = GetCounter();

        commandBuffer0.Flush();
        commandBuffer1.Flush();
        commandBuffer2.Flush();
        commandBuffer3.Flush();

        auto vBufferUpdate = commandBuffer0.AllocateCommand<gfx::commands::UpdateBufferDataCmd>();
        vBufferUpdate->targetBuffer = vBuffer;
        vBufferUpdate->data = triangleVertices;
        vBufferUpdate->numBytes = sizeof(triangleVertices);

        auto iBufferUpdate = commandBuffer0.AllocateCommand<gfx::commands::UpdateBufferDataCmd>();
        iBufferUpdate->targetBuffer = iBuffer;
        iBufferUpdate->data = triangleIndices;
        iBufferUpdate->numBytes = sizeof(triangleIndices);
        

        auto drawCall = commandBuffer0.AllocateCommand<gfx::commands::DrawBatchCmd>();
        drawCall->numVertexBuffers = 1;
        drawCall->vertexBuffers[0] = vBuffer;
        drawCall->indexBuffer = iBuffer;
        drawCall->numConstantBuffers = 0;

        drawCall->vertexTopology = D3D10_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        drawCall->vertexBufferOffsets[0] = 0;
        drawCall->vertexBufferStrides[0] = sizeof(Vertex);
        drawCall->indexBufferOffset = 0;
        drawCall->indexCount = 3;
        drawCall->inputLayout = inputLayout;
        drawCall->indexFormat = DXGI_FORMAT::DXGI_FORMAT_R16_UINT;
        drawCall->vertexShader = vShader;
        drawCall->pixelShader = fShader;

        /*
        struct DrawCallRecordData
        {
            ID3D11Buffer* vBuffer;
            ID3D11Buffer* iBuffer;
            ID3D11VertexShader* vShader;
            ID3D11PixelShader* fShader;
            ID3D11InputLayout* inputLayout;

            gfx::CommandBuffer* commandBuffer;

            volatile int* counter;
        };

        volatile int complCount = 4;

        DrawCallRecordData recordData0{ vBuffer, iBuffer, vShader, fShader, inputLayout, &commandBuffer0, &complCount };
        DrawCallRecordData recordData1{ vBuffer, iBuffer, vShader, fShader, inputLayout, &commandBuffer1, &complCount };
        DrawCallRecordData recordData2{ vBuffer, iBuffer, vShader, fShader, inputLayout, &commandBuffer2, &complCount };
        DrawCallRecordData recordData3{ vBuffer, iBuffer, vShader, fShader, inputLayout, &commandBuffer3, &complCount };


        auto RecordDrawCalls = [](void* data) -> void {
            auto context = static_cast<DrawCallRecordData*>(data);
            for (int i = 0; i < 2500; ++i) {
                auto drawCall = context->commandBuffer->AllocateCommand<gfx::commands::DrawBatchCmd>();
                drawCall->numVertexBuffers = 1;
                drawCall->vertexBuffers[0] = context->vBuffer;
                drawCall->indexBuffer = context->iBuffer;
                drawCall->numConstantBuffers = 0;

                drawCall->vertexTopology = D3D10_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                drawCall->vertexBufferOffsets[0] = 0;
                drawCall->vertexBufferStrides[0] = sizeof(Vertex);
                drawCall->indexBufferOffset = 0;
                drawCall->indexCount = 3;
                drawCall->inputLayout = context->inputLayout;
                drawCall->indexFormat = DXGI_FORMAT::DXGI_FORMAT_R16_UINT;
                drawCall->vertexShader = context->vShader;
                drawCall->pixelShader = context->fShader;
            }
            (*(context->counter))--;
        };

        workerThreads[0].task.data = &recordData0;
        workerThreads[1].task.data = &recordData1;
        workerThreads[2].task.data = &recordData2;
        workerThreads[3].task.data = &recordData3;

        for (int i = 0; i < 4; ++i) {
            workerThreads[i].task.Func = RecordDrawCalls;
        }

        do {} while (complCount > 0);
        */
        
        GT_LOG_INFO("Renderer", "Command recording took %f ms", 1000.0 * (GetCounter() - cmdRecordingTimerStart));


        // draw geometry

        auto commandSubmissionTimerStart = GetCounter();
        gfx::CommandBuffer* cmdBuffers[] = {
            &commandBuffer0, &commandBuffer1, &commandBuffer2, &commandBuffer3
        };
        gfx::SubmitCommandBuffers(&mainRenderPass, cmdBuffers, 4);
        
        GT_LOG_INFO("Renderer", "Command submission took %f ms", 1000.0 * (GetCounter() - commandSubmissionTimerStart));

        // draw UI
        auto uiDrawData = ImGui::GetDrawData();
        if (uiDrawData) {
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            ImGui_ImplDX11_RenderDrawLists(uiDrawData);
        }

        /* Present render frame*/
        auto presentTimerStart = GetCounter();
        g_pSwapChain->Present(0, 0);
        GT_LOG_INFO("Renderer", "Present took %f ms", 1000.0 * (GetCounter() - presentTimerStart));
        GT_LOG_INFO("Renderer", "Render frame took %f ms", 1000.0 * (GetCounter() - renderFrameTimerStart));
    } while (!exitFlag);

    ImGui_ImplDX11_Shutdown();

    fnd::sockets::ShutdownSocketLayer();

    return 0;
}

#ifndef GT_SHARED_LIB
int main(int argc, char* argv[])
{
    return win32_main(argc, argv);
}
#endif
