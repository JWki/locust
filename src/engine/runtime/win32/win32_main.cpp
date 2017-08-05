﻿
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef near
#undef far

#include <engine/runtime/ImGui/imgui.h>
//#include <engine/runtime/win32/imgui_impl_dx11.h>

#include <tchar.h>
#include <stdio.h>
#include <malloc.h>

#include <foundation/int_types.h>
#include <foundation/memory/memory.h>
#include <foundation/memory/allocators.h>
#include <foundation/logging/logging.h>

#include <engine/runtime/gfx/gfx.h>

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
                GT_LOG_WARNING("Memory", "Leaky allocation, %llu bytes leaked, allocated from\n%s(%lli)", info.size, info.info.file, info.info.line);
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
        auto h = fnd::logging::LogChannel("RenderProfile").hash;
        return criteria.channel.hash != fnd::logging::LogChannel("RenderProfile").hash;
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
        size_t offset = snprintf(buf, bufSize, "%s(%llu): [%s]    ", criteria.scInfo.file, criteria.scInfo.line, criteria.channel.str);
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
               && criteria.channel.hash != fnd::logging::LogChannel("RenderProfile").hash;
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


#define KILOBYTES(n) (n * 1024)
#define MEGABYTES(n) (KILOBYTES(n) * 1024)
#define GIGABYTES(n) (MEGABYTES(n) * (size_t)1024)

static_assert(GIGABYTES(8) > MEGABYTES(4), "some size type is wrong");

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

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

/*
#include <comdef.h>

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
    auto res = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (res != S_OK) {
        _com_error err(res);
        LPCTSTR errMsg = err.ErrorMessage();
        printf("%ws\n", errMsg);
    }
    res = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, &render_target_view_desc, &g_mainRenderTargetView);
    //g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { 
        g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; 
    }
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
#ifdef GT_DEVELOPMENT
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 1, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return E_FAIL;

    ID3D11Debug *d3dDebug = nullptr;
    if (SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&d3dDebug)))
    {
        ID3D11InfoQueue *d3dInfoQueue = nullptr;
        if (SUCCEEDED(d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue)))
        {
#ifdef GT_DEVELOPMENT
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
#endif

            D3D11_MESSAGE_ID hide[] =
            {
                D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
                // Add more message IDs here as needed
            };

            D3D11_INFO_QUEUE_FILTER filter;
            memset(&filter, 0, sizeof(filter));
            filter.DenyList.NumIDs = _countof(hide);
            filter.DenyList.pIDList = hide;
            d3dInfoQueue->AddStorageFilterEntries(&filter);
            d3dInfoQueue->Release();
        }
        d3dDebug->Release();
    }


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
*/


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


//extern LRESULT ImGui_ImplDX11_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    /*if (ImGui_ImplDX11_WndProcHandler(hWnd, msg, wParam, lParam)) {
        //return true;
    }*/
    switch (msg)
    {
    case WM_SIZE:
        /*if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }*/
        //return 0;
        return DefWindowProc(hWnd, msg, wParam, lParam);
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

#include <engine/runtime/gfx/gfx.h>


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

HWND g_hwnd = NULL;

void AttachWindow(HWND b, HWND a)
{
    DWORD style = GetWindowLong(b, GWL_STYLE); //get the b style
    style &= ~(WS_POPUP | WS_CAPTION); //reset the "caption" and "popup" bits
    style |= WS_CHILD; //set the "child" bit
    SetWindowLong(b, GWL_STYLE, style); //set the new style of b
    SetParent(b, a); //a will be the new parent b
    RECT rc; //temporary rectangle
    GetClientRect(a, &rc); //the "inside border" rectangle for a
    MoveWindow(b, rc.left, rc.top, (rc.right - rc.left), (rc.bottom - rc.top), TRUE); //place b at (x,y,w,h) in a
    UpdateWindow(a);
}

#define GT_TOOL_SERVER_PORT 8080
#define GT_MAX_TOOL_CONNECTIONS 32

#include <cstdlib>

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
            
                if (strncmp(m_receiveBuffer, "WindowHandle = ", strlen("WindowHandle = ")) == 0) {
                    GT_LOG_DEBUG("Tools Server", "OI");
                    char* handleStr = m_receiveBuffer + strlen("WindowHandle = ");
                    char* end;
                    HWND wnd = (HWND)strtoull(handleStr, &end, 10);
                    Sleep(500);
                    AttachWindow(g_hwnd, wnd);
                    RECT rc;
                    GetClientRect(g_hwnd, &rc);
                    /*CleanupRenderTarget();
                    g_pSwapChain->ResizeBuffers(0, rc.right - rc.left, rc.bottom - rc.top, DXGI_FORMAT_UNKNOWN, 0);
                    CreateRenderTarget();*/
                }
            }
        }
    }
};


namespace util
{
    void Copy4x4FloatMatrix(float* matFrom, float* matTo)
    {
        memcpy(matTo, matFrom, sizeof(float) * 16);
    }

    float Get4x4FloatMatrixValue(float* mat, int column, int row)
    {
        int index = 4 * column + row;
        assert(index < 16);
        return mat[index];
    }

    void Set4x4FloatMatrixValue(float* mat, int column, int row, float value)
    {
        int index = 4 * column + row;
        assert(index < 16);
        mat[index] = value;
    }

    void Make4x4FloatMatrixIdentity(float* mat)
    {
        memset(mat, 0x0, sizeof(float) * 16);
        for (int i = 0; i < 4; ++i) { mat[4 * i + i] = 1.0f; }
    }


    void Make4x4FloatProjectionMatrixLH(float* mat, float fovInRadians, float aspect, float near, float far)
    {
        Make4x4FloatMatrixIdentity(mat);

        float tanHalfFovy = tanf(fovInRadians / 2.0f);

        Set4x4FloatMatrixValue(mat, 0, 0, 1.0f / (aspect * tanHalfFovy));
        Set4x4FloatMatrixValue(mat, 1, 1, 1.0f / tanHalfFovy);
        Set4x4FloatMatrixValue(mat, 2, 3, 1.0f);
        
        Set4x4FloatMatrixValue(mat, 2, 2, (far * near) / (far - near));
        Set4x4FloatMatrixValue(mat, 3, 2, -(2.0f * far * near) / (far - near));
    }

    void Make4x4FloatMatrixTranspose(float* mat, float* result)
    {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                Set4x4FloatMatrixValue(result, j, i, Get4x4FloatMatrixValue(mat, i, j));
            }
        }
    }

    fnd::math::float4 Get4x4FloatMatrixColumn(float* mat, int column) 
    {
        return {
            mat[4 * column + 0],
            mat[4 * column + 1],
            mat[4 * column + 2],
            mat[4 * column + 3]
        };
    }
   
    void Make4x4FloatRotationMatrix(float* mat, fnd::math::float3 axisIn, float rad)
    {
        float rotate[16];
        float base[16];
        Make4x4FloatMatrixIdentity(base);
        Make4x4FloatMatrixIdentity(rotate);
        Make4x4FloatMatrixIdentity(mat);

        float a = rad;
        float c = fnd::math::Cos(a);
        float s = fnd::math::Sin(a);

        auto axis = fnd::math::Normalize(axisIn);
        auto temp = axis * (1.0f - c);

        Set4x4FloatMatrixValue(rotate, 0, 0, c + temp[0] * axis[0]);
        Set4x4FloatMatrixValue(rotate, 0, 1, temp[0] * axis[1] + s * axis[2]);
        Set4x4FloatMatrixValue(rotate, 0, 2, temp[0] * axis[2] - s * axis[1]);
        
        Set4x4FloatMatrixValue(rotate, 1, 0, temp[1] * axis[0] - s * axis[2]);
        Set4x4FloatMatrixValue(rotate, 1, 1, c + temp[1] * axis[1]);
        Set4x4FloatMatrixValue(rotate, 1, 2, temp[1] * axis[2] + s * axis[0]);

        Set4x4FloatMatrixValue(rotate, 2, 0, temp[2] * axis[0] + s * axis[1]);
        Set4x4FloatMatrixValue(rotate, 2, 1, temp[2] * axis[1] - s * axis[0]);
        Set4x4FloatMatrixValue(rotate, 2, 2, c + temp[2] * axis[2]);

        fnd::math::float4 m0 = Get4x4FloatMatrixColumn(base, 0);
        fnd::math::float4 m1 = Get4x4FloatMatrixColumn(base, 1);
        fnd::math::float4 m2 = Get4x4FloatMatrixColumn(base, 2);
        fnd::math::float4 m3 = Get4x4FloatMatrixColumn(base, 3);

        float r00 = Get4x4FloatMatrixValue(rotate, 0, 0);
        float r11 = Get4x4FloatMatrixValue(rotate, 1, 1);
        float r12 = Get4x4FloatMatrixValue(rotate, 1, 2);
        float r01 = Get4x4FloatMatrixValue(rotate, 0, 1);
        float r02 = Get4x4FloatMatrixValue(rotate, 0, 2);

        float r10 = Get4x4FloatMatrixValue(rotate, 1, 0);
        float r20 = Get4x4FloatMatrixValue(rotate, 2, 0);
        float r21 = Get4x4FloatMatrixValue(rotate, 2, 1);
        float r22 = Get4x4FloatMatrixValue(rotate, 2, 2);

        for (int i = 0; i < 4; ++i) {
            Set4x4FloatMatrixValue(mat, i, 0, m0[i] * r00 + m1[i] * r01 + m2[i] * r02);
            Set4x4FloatMatrixValue(mat, i, 1, m0[i] * r10 + m1[i] * r11 + m2[i] * r12);
            Set4x4FloatMatrixValue(mat, i, 2, m0[i] * r20 + m1[i] * r21 + m2[i] * r22);
            Set4x4FloatMatrixValue(mat, i, 3, m3[i]);
        }
    }

    void Make4x4FloatTranslationMatrix(float* mat, fnd::math::float3 t)
    {
        Make4x4FloatMatrixIdentity(mat);
        for (int i = 0; i < 3; ++i) {
            Set4x4FloatMatrixValue(mat, 3, i, t[i]);
        }
    }

    // result = matA * matB
    void MultiplyMatrices(float* matA, float* matB, float* result)
    {
        Make4x4FloatMatrixIdentity(result);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float acc = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    acc += Get4x4FloatMatrixValue(matA, i, k) * Get4x4FloatMatrixValue(matB, k, j);
                }
                Set4x4FloatMatrixValue(result, i, j, acc);
            } 
        }
    }
}


#pragma warning(push, 0)    // lots of warnings in here  
#define PAR_SHAPES_IMPLEMENTATION
#include <engine/runtime/par_shapes-h.h>
#pragma warning(pop)

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
    GT_LOG_INFO("Application", "Created %llu worker threads", NUM_WORKER_THREADS);

    //
    
    ToolServer toolServer;
    if (!toolServer.Start(&applicationArena, GT_TOOL_SERVER_PORT, GT_MAX_TOOL_CONNECTIONS)) {
        GT_LOG_ERROR("Application", "Failed to initialize tools server");
    }
    
    //
    bool exitFlag = false;
    bool restartFlag = false;

    ImGui::GetIO().UserData = &sandboxArena;
    ImGui::GetIO().MemAllocFn = [](size_t size) -> void* {
        auto arena = static_cast<HeapArena*>(ImGui::GetIO().UserData);
        return arena->Allocate(size, 4, GT_SOURCE_INFO);
    };
    ImGui::GetIO().MemFreeFn = [](void* ptr) -> void {
        auto arena = static_cast<HeapArena*>(ImGui::GetIO().UserData);
        arena->Free(ptr);
    };

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, _T("GTRuntimeWindowClass"), NULL };
    RegisterClassEx(&wc);
    g_hwnd = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, _T("GTRuntimeWindowClass"), _T("GT Runtime"), WS_OVERLAPPEDWINDOW, 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, wc.hInstance, NULL);

    HWND secondaryWindow = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, _T("GTRuntimeWindowClass"), _T("GT Runtime"), WS_OVERLAPPEDWINDOW, 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, wc.hInstance, NULL);

    if (!g_hwnd) {
        GT_LOG_ERROR("Application", "failed to create a window\n");
        return 1;
    }

    GT_LOG_INFO("Application", "Created application window");

    const float bgColor[] = { 100.0f / 255.0f, 149.0f / 255.0f, 237.0f / 255.0f };

    // Initialize Direct3D
    /*if (CreateDeviceD3D(g_hwnd) < 0)
    {
        CleanupDeviceD3D();
        UnregisterClass(_T("void"), wc.hInstance);
        return 1;
    }*/

    GT_LOG_INFO("Application", "Initialized gfx device");

    //ImGui_ImplDX11_Init(g_hwnd, g_pd3dDevice, g_pd3dDeviceContext);

    ImGui_Style_SetDark(0.8f);

    GT_LOG_INFO("Application", "Initialized UI");

    // Show the window
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    ShowWindow(secondaryWindow, SW_SHOWDEFAULT);
    UpdateWindow(secondaryWindow);

    //
    
    struct Vertex
    {
        math::float4 position;
        math::float4 color;
    };

    struct VertexWeight
    {
        float value = 1.0f;
    };

    Vertex triangleVertices[] = {
        { { 0.0f, 0.5f, 0.0f, 1.0f },{ 1.0f, 0.0f, 0.0f, 1.0f } },
        { { 0.45f, -0.5, 0.0f, 1.0f },{ 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.45f, -0.5f, 0.0f, 1.0f },{ 0.0f, 0.0f, 1.0f, 1.0f } }
    };

    VertexWeight triangleWeights[] = { 1.0f, 1.0f, 1.0f };

    uint16_t triangleIndices[] = {
        0, 1, 2, 2, 1, 0
    };

    size_t vShaderCodeSize = 0;
    char* vShaderCode = static_cast<char*>(LoadFileContents("VertexShader.cso", &applicationArena, &vShaderCodeSize));
    if (!vShaderCode) {
        GT_LOG_ERROR("D3D11", "Failed to load vertex shader\n");
    }

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

    //
 
    gfx::Interface* gfxInterface = nullptr;
    gfx::Device* gfxDevice = nullptr;

    gfx::InterfaceDesc interfaceDesc;
    if (!gfx::CreateInterface(&gfxInterface, &interfaceDesc, &applicationArena)) {
        GT_LOG_ERROR("Renderer", "Failed to initialize graphics interface\n");
    }

    gfx::DeviceInfo deviceInfo[GFX_DEFAULT_MAX_NUM_DEVICES];
    uint32_t numDevices = 0;
    gfx::EnumerateDevices(gfxInterface, deviceInfo, &numDevices);
    for (uint32_t i = 0; i < numDevices; ++i) {
        GT_LOG_INFO("Renderer", "Detected graphics device: %s", deviceInfo[i].friendlyName);
    }
    if (numDevices == 0) {
        GT_LOG_ERROR("Renderer", "No graphics device detected!");
    }
    else {
        gfxDevice = gfx::GetDevice(gfxInterface, deviceInfo[0].index);
        if (gfxDevice != nullptr) {
            GT_LOG_INFO("Renderer", "Selected graphics device: %s", deviceInfo[0].friendlyName);
        }
        else {
            GT_LOG_ERROR("Renderer", "Failed to get default graphics device");
        }
    }

    gfx::SwapChainDesc swapChainDesc;   
    swapChainDesc.width = WINDOW_WIDTH;
    swapChainDesc.height = WINDOW_HEIGHT;
    swapChainDesc.window = g_hwnd;
    gfx::SwapChain swapChain = gfx::CreateSwapChain(gfxDevice, &swapChainDesc);
    if (!GFX_CHECK_RESOURCE(swapChain)) {
        GT_LOG_ERROR("Renderer", "Failed to create swap chain");
    }

    gfx::SwapChain secondSwapChain;
    {
        gfx::SwapChainDesc swapChainDesc; 
        swapChainDesc.width = WINDOW_WIDTH;
        swapChainDesc.height = WINDOW_HEIGHT;
        swapChainDesc.window = secondaryWindow;
        secondSwapChain = gfx::CreateSwapChain(gfxDevice, &swapChainDesc);
        if (!GFX_CHECK_RESOURCE(secondSwapChain)) {
            GT_LOG_ERROR("Renderer", "Failed to create swap chain");
        }
    }

    struct ConstantData {
        float transform[16];
    };

    ConstantData transform;
    memset(&transform, 0x0, sizeof(ConstantData));
    for (int i = 0; i < 4; ++i) {
        transform.transform[4 * i + i] = 1.0f;
    }
    //transform.transform[4 * 3 + 3] = 1.0f;
    auto cubeMesh = par_shapes_create_cube();
    par_shapes_translate(cubeMesh, -0.5f, -0.5f, -0.5f);

    float* cubeVertices = cubeMesh->points;
    PAR_SHAPES_T* cubeIndices = cubeMesh->triangles;
    int numCubeVertices = cubeMesh->npoints;
    int numCubeIndices = cubeMesh->ntriangles * 3;
    for (int i = 0; i < cubeMesh->ntriangles; ++i) {
        int j = i + 1; 
        int k = j + 1;
        PAR_SHAPES_T swap = cubeIndices[i];
        //cubeIndices[i] = cubeIndices[k];
        //cubeIndices[k] = swap;
    }


    gfx::BufferDesc cubeVertexBufferDesc;
    cubeVertexBufferDesc.type = gfx::BufferType::BUFFER_TYPE_VERTEX;
    cubeVertexBufferDesc.byteWidth = sizeof(float) * numCubeVertices * 3;
    cubeVertexBufferDesc.initialData = cubeVertices;
    cubeVertexBufferDesc.initialDataSize = cubeVertexBufferDesc.byteWidth;
    gfx::Buffer cubeVertexBuffer = gfx::CreateBuffer(gfxDevice, &cubeVertexBufferDesc);
    if (!GFX_CHECK_RESOURCE(cubeVertexBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create cube vertex buffer");
    }

    gfx::BufferDesc cubeIndexBufferDesc;
    cubeIndexBufferDesc.type = gfx::BufferType::BUFFER_TYPE_INDEX;
    cubeIndexBufferDesc.byteWidth = sizeof(PAR_SHAPES_T) * numCubeIndices;
    cubeIndexBufferDesc.initialData = cubeIndices;
    cubeIndexBufferDesc.initialDataSize = cubeIndexBufferDesc.byteWidth;
    gfx::Buffer cubeIndexBuffer = gfx::CreateBuffer(gfxDevice, &cubeIndexBufferDesc);
    if (!GFX_CHECK_RESOURCE(cubeIndexBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create cube index buffer");
    }


    gfx::BufferDesc vBufferDesc;
    vBufferDesc.type = gfx::BufferType::BUFFER_TYPE_VERTEX;
    vBufferDesc.byteWidth = sizeof(triangleVertices);
    vBufferDesc.initialData = triangleVertices;
    vBufferDesc.initialDataSize = sizeof(triangleVertices);
    gfx::Buffer vBuffer = gfx::CreateBuffer(gfxDevice, &vBufferDesc);
    if (!GFX_CHECK_RESOURCE(vBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create vertex buffer");
    }

    gfx::BufferDesc vWeightBufferDesc;
    vWeightBufferDesc.type = gfx::BufferType::BUFFER_TYPE_VERTEX;
    vWeightBufferDesc.byteWidth = sizeof(triangleWeights);
    vWeightBufferDesc.initialData = triangleWeights;
    vWeightBufferDesc.initialDataSize = sizeof(triangleWeights);
    vWeightBufferDesc.usage = gfx::ResourceUsage::USAGE_DYNAMIC;
    gfx::Buffer vWeightBuffer = gfx::CreateBuffer(gfxDevice, &vWeightBufferDesc);
    if (!GFX_CHECK_RESOURCE(vWeightBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create vertex weight buffer");
    }

    gfx::BufferDesc iBufferDesc;
    iBufferDesc.type = gfx::BufferType::BUFFER_TYPE_INDEX;
    iBufferDesc.byteWidth = sizeof(triangleIndices);
    iBufferDesc.initialData = triangleIndices;
    iBufferDesc.initialDataSize = sizeof(triangleIndices);
    gfx::Buffer iBuffer = gfx::CreateBuffer(gfxDevice, &iBufferDesc);
    if (!GFX_CHECK_RESOURCE(iBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create index buffer");
    }

    gfx::BufferDesc cBufferDesc;
    cBufferDesc.type = gfx::BufferType::BUFFER_TYPE_CONSTANT;
    cBufferDesc.byteWidth = sizeof(ConstantData);
    cBufferDesc.initialData = &transform;
    cBufferDesc.initialDataSize = sizeof(transform);
    cBufferDesc.usage = gfx::ResourceUsage::USAGE_STREAM;
    gfx::Buffer cBuffer = gfx::CreateBuffer(gfxDevice, &cBufferDesc);
    if (!GFX_CHECK_RESOURCE(cBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create constant buffer");
    }

    gfx::ShaderDesc vShaderDesc;
    vShaderDesc.type = gfx::ShaderType::SHADER_TYPE_VS;
    vShaderDesc.code = vShaderCode;
    vShaderDesc.codeSize = vShaderCodeSize;

    gfx::ShaderDesc vCubeShaderDesc;
    vCubeShaderDesc.type = gfx::ShaderType::SHADER_TYPE_VS;
    vCubeShaderDesc.code = vCubeShaderCode;
    vCubeShaderDesc.codeSize = vCubeShaderCodeSize;
    
    gfx::ShaderDesc pShaderDesc;
    pShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
    pShaderDesc.code = pShaderCode;
    pShaderDesc.codeSize = pShaderCodeSize;

    gfx::Shader vShader = gfx::CreateShader(gfxDevice, &vShaderDesc);
    if (!GFX_CHECK_RESOURCE(vShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create vertex shader");
    }

    gfx::Shader vCubeShader = gfx::CreateShader(gfxDevice, &vCubeShaderDesc);
    if (!GFX_CHECK_RESOURCE(vCubeShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create vertex shader");
    }

    gfx::Shader pShader = gfx::CreateShader(gfxDevice, &pShaderDesc);
    if (!GFX_CHECK_RESOURCE(pShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
    }

    gfx::PipelineStateDesc pipelineStateDesc;
    pipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_UINT16;
    pipelineStateDesc.vertexShader = vShader;
    pipelineStateDesc.pixelShader = pShader;
    pipelineStateDesc.vertexLayout.attribs[0] = { "POSITION", 0, 0, 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT4 };
    pipelineStateDesc.vertexLayout.attribs[1] = { "COLOR", 0, sizeof(math::float4), 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT4 };
    pipelineStateDesc.vertexLayout.attribs[2] = { "TEXCOORD", 0, 0, 1, gfx::VertexFormat::VERTEX_FORMAT_FLOAT };
    gfx::PipelineState pipeline = gfx::CreatePipelineState(gfxDevice, &pipelineStateDesc);
    if (!GFX_CHECK_RESOURCE(pipeline)) {
        GT_LOG_ERROR("Renderer", "Failed to create pipeline state");
    }

    gfx::PipelineStateDesc cubePipelineStateDesc;
    cubePipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_UINT16;
    cubePipelineStateDesc.vertexShader = vCubeShader;
    cubePipelineStateDesc.pixelShader = pShader;
    cubePipelineStateDesc.vertexLayout.attribs[0] = { "POSITION", 0, 0, 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT3 };
    gfx::PipelineState cubePipeline = gfx::CreatePipelineState(gfxDevice, &cubePipelineStateDesc);
    if (!GFX_CHECK_RESOURCE(cubePipeline)) {
        GT_LOG_ERROR("Renderer", "Failed to create pipeline state for cube");
    }

    gfx::DrawCall triangleDrawCall;
    triangleDrawCall.vertexBuffers[0] = vBuffer;
    triangleDrawCall.vertexOffsets[0] = 0;
    triangleDrawCall.vertexStrides[0] = sizeof(Vertex);
    triangleDrawCall.vertexBuffers[1] = vWeightBuffer;
    triangleDrawCall.vertexOffsets[1] = 0;
    triangleDrawCall.vertexStrides[1] = sizeof(VertexWeight);
    triangleDrawCall.indexBuffer = iBuffer;
    triangleDrawCall.numElements = ARRAYSIZE(triangleIndices);
    triangleDrawCall.pipelineState = pipeline;
    triangleDrawCall.vsConstantInputs[0] = cBuffer;

    gfx::DrawCall cubeDrawCall;
    cubeDrawCall.vertexBuffers[0] = cubeVertexBuffer;
    cubeDrawCall.vertexOffsets[0] = 0;
    cubeDrawCall.vertexStrides[0] = sizeof(float) * 3;
    cubeDrawCall.indexBuffer = cubeIndexBuffer;
    cubeDrawCall.numElements = numCubeIndices;
    cubeDrawCall.pipelineState = cubePipeline;
    cubeDrawCall.vsConstantInputs[0] = cBuffer;

    GT_LOG_INFO("Application", "Initialized graphics scene");

    gfx::CommandBuffer cmdBuffer = gfx::GetImmediateCommandBuffer(gfxDevice);

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

        bool didUpdate = false;

        while (accumulator >= dt) {
            didUpdate = true;

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

            //ImGui_ImplDX11_NewFrame();

//
//#ifdef GT_DEVELOPMENT
//            if (ImGui::Begin("Memory usage", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
//
//                size_t totalSize = 0;
//                auto it = GetMemTrackerListHead();
//                while (it != nullptr) {
//
//                    totalSize += it->GetUsedMemorySize();
//                    if (ImGui::TreeNode(it->GetName())) {
//                        ImGui::Text("%llu kB allocated", it->GetUsedMemorySize() / 1024);
//                        ImGui::TreePop();
//                    }
//
//                    it = it->GetNext();
//                }
//
//                ImGui::Separator();
//                ImGui::Text("Total usage: %llu kb", totalSize / 1024);
//            } ImGui::End();
//#endif
//            if (ImGui::Begin("Maths!")) {
//                using namespace math;
//
//                static float4 fl4;
//                static float3 fl3;
//                static float2 fl2;
//
//                static int4 i4;
//                static int3 i3;
//                static int2 i2;
//
//                ImGui::DragFloat4("fl4", static_cast<float*>(fl4));
//                ImGui::DragFloat3("fl3", static_cast<float*>(fl3));
//                ImGui::DragFloat2("fl2", static_cast<float*>(fl2));
//
//                ImGui::Text("float4 arithmetics");
//                {
//                    static float4 a;
//                    static float4 b;
//                    static float4 c;
//
//                    ImGui::PushID("float4");
//                    ImGui::DragFloat4("a", static_cast<float*>(a));
//                    ImGui::SameLine();
//                    if (ImGui::Button("Normalize##a")) {
//                        a = Normalize(a);
//                    }
//                    ImGui::SameLine();
//                    ImGui::Text("Length is %f", Length(a));
//
//                    ImGui::DragFloat4("b", static_cast<float*>(b));
//                    ImGui::SameLine();
//                    if (ImGui::Button("Normalize##b")) {
//                        b = Normalize(b);
//                    }
//                    ImGui::SameLine();
//                    ImGui::Text("Length is %f", Length(b));
//
//                    if (ImGui::Button("a + b")) {
//                        c = a + b;
//                    } ImGui::SameLine();
//                    if (ImGui::Button("a - b")) {
//                        c = a - b;
//                    } ImGui::SameLine();
//                    if (ImGui::Button("a * b")) {
//                        c = a * b;
//                    } ImGui::SameLine();
//                    if (ImGui::Button("a / b")) {
//                        c = a / b;
//                    }
//                    ImGui::DragFloat4("c", static_cast<float*>(c));
//
//                    float dot = Dot(a, b);
//                    ImGui::SliderFloat("dot (a, b)", &dot, -1.0f, 1.0f);
//                    ImGui::PopID();
//                }
//                ImGui::Text("float3 arithmetics");
//                {
//                    static float3 a;
//                    static float3 b;
//                    static float3 c;
//
//                    ImGui::PushID("float3");
//                    ImGui::DragFloat3("a", static_cast<float*>(a));
//                    ImGui::SameLine();
//                    if (ImGui::Button("Normalize##a")) {
//                        a = Normalize(a);
//                    }
//                    ImGui::SameLine();
//                    ImGui::Text("Length is %f", Length(a));
//
//                    ImGui::DragFloat3("b", static_cast<float*>(b));
//                    ImGui::SameLine();
//                    if (ImGui::Button("Normalize##b")) {
//                        b = Normalize(b);
//                    }
//                    ImGui::SameLine();
//                    ImGui::Text("Length is %f", Length(b));
//
//                    if (ImGui::Button("a + b")) {
//                        c = a + b;
//                    } ImGui::SameLine();
//                    if (ImGui::Button("a - b")) {
//                        c = a - b;
//                    } ImGui::SameLine();
//                    if (ImGui::Button("a * b")) {
//                        c = a * b;
//                    } ImGui::SameLine();
//                    if (ImGui::Button("a / b")) {
//                        c = a / b;
//                    }
//                    ImGui::DragFloat3("c", static_cast<float*>(c));
//
//                    float dot = Dot(a, b);
//                    ImGui::SliderFloat("dot (a, b)", &dot, -1.0f, 1.0f);
//                    ImGui::PopID();
//                }
//                ImGui::Text("float2 arithmetics");
//                {
//                    static float2 a;
//                    static float2 b;
//                    static float2 c;
//
//                    ImGui::PushID("float2");
//                    ImGui::DragFloat2("a", static_cast<float*>(a));
//                    ImGui::SameLine();
//                    if (ImGui::Button("Normalize##a")) {
//                        a = Normalize(a);
//                    }
//                    ImGui::SameLine();
//                    ImGui::Text("Length is %f", Length(a));
//
//                    ImGui::DragFloat2("b", static_cast<float*>(b));
//                    ImGui::SameLine();
//                    if (ImGui::Button("Normalize##b")) {
//                        b = Normalize(b);
//                    }
//                    ImGui::SameLine();
//                    ImGui::Text("Length is %f", Length(b));
//
//                    if (ImGui::Button("a + b")) {
//                        c = a + b;
//                    } ImGui::SameLine();
//                    if (ImGui::Button("a - b")) {
//                        c = a - b;
//                    } ImGui::SameLine();
//                    if (ImGui::Button("a * b")) {
//                        c = a * b;
//                    } ImGui::SameLine();
//                    if (ImGui::Button("a / b")) {
//                        c = a / b;
//                    }
//                    ImGui::DragFloat2("c", static_cast<float*>(c));
//
//                    float dot = Dot(a, b);
//                    ImGui::SliderFloat("dot (a, b)", &dot, -1.0f, 1.0f);
//                    ImGui::PopID();
//                }
//
//
//            } ImGui::End();
//
//
//            if (ImGui::Begin("Networking", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
//                Port port = 0;
//                static Port targetPort = 8080;
//                ImGui::InputInt("Port", reinterpret_cast<int*>(&targetPort));
//                if (!socket.IsOpen(&port)) {
//                    if (ImGui::Button("Open socket")) {
//                        if (!socket.Open(targetPort)) {
//                            GT_LOG_ERROR("Network", "failed to open UDP socket on port %d", targetPort);
//                        }
//                        else {
//                            GT_LOG_INFO("Network", "opened UDP socket on port %d", targetPort);
//                        }
//                    } 
//                }
//                else {
//                    ImGui::Text("Listening on port %d", port);
//                    if (ImGui::Button("Disconnect")) {
//                        socket.Close();
//                        GT_LOG_INFO("Network", "closed UDP socket");
//                    } ImGui::SameLine();
//                    static int a = 127, b = 0, c = 0, d = 1;
//                    static Port remotePort = 8080;
//                    if (ImGui::Button("Send Hello World")) {
//                        fnd::sockets::Address addr(a, b, c, d, remotePort);
//                        socket.Send(&addr, "Hello World", strlen("Hello World"));
//                    }
//                    ImGui::Text("Remote address");
//                    ImGui::PushItemWidth(100.0f);
//                    ImGui::InputInt("##a", &a, 1, 100);
//                    ImGui::SameLine();
//                    ImGui::InputInt("##b", &b);
//                    ImGui::SameLine();
//                    ImGui::InputInt("##c", &c);
//                    ImGui::SameLine();
//                    ImGui::InputInt("##d", &d);
//                    ImGui::SameLine();
//                    ImGui::InputInt("Remote port", reinterpret_cast<int*>(&remotePort));
//                    ImGui::PopItemWidth();
//                    a = a >= 0 ? (a <= 255 ? a : 255) : 0;
//                    b = b >= 0 ? (b <= 255 ? b : 255) : 0;
//                    c = c >= 0 ? (c <= 255 ? c : 255) : 0;
//                    d = d >= 0 ? (d <= 255 ? d : 255) : 0;
//                }
//            } ImGui::End();
//
//
//            /* Basic UI: frame statistics */
//            ImGui::SetNextWindowPos(ImVec2(10.0f, ImGui::GetIO().DisplaySize.y - 50));
//            ImGui::Begin("#framestatistics", (bool*)0, ImVec2(0, 0), 0.45f, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
//            ImGui::Text("Simulation time average: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
//            ImGui::End();

            float fCounter = static_cast<float>(GetCounter());
           
            math::float3 translation;
            translation.x = 5.0f;//math::Sin(fCounter) * 1.0f;
            translation.y = 0.0;//-math::Cos(fCounter) * 1.0f;

            float proj[16];
            float camera[16];
            float model[16];

            float translate[16];
            float rotate[16];
            static float rotation = 0.0f;
            rotation += 0.03f;

            util::Make4x4FloatTranslationMatrix(translate, translation);
            util::Make4x4FloatRotationMatrix(rotate, { 0.0f, 1.0f, 0.0f }, rotation);
        
            util::Make4x4FloatProjectionMatrixLH(proj, 1.0f, (float)WINDOW_WIDTH / WINDOW_HEIGHT, 0.1f, 1000.0f);
            util::Make4x4FloatTranslationMatrix(camera, { 0.0f, -1.0f, 5.0f });
            
            float modelView[16];

            util::MultiplyMatrices(translate, rotate, model);
            util::Copy4x4FloatMatrix(rotate, model);
            util::MultiplyMatrices(model, camera, modelView);
            util::MultiplyMatrices(modelView, proj, transform.transform);
            //util::Copy4x4FloatMatrix(model, transform.transform);

            for (int i = 0; i < 3; ++i) {
                //triangleWeights[i].value -= 0.001f;
                triangleWeights[i].value = triangleWeights[i].value < 0.0f ? triangleWeights[i].value : (triangleWeights[i].value > 1.0f ? 1.0f : triangleWeights[i].value);
            }
            /* End sim frame */
            //ImGui::Render();
            t += dt;
            accumulator -= dt;
        }
        if (didUpdate) {
            // upload transformation to constant buffer only once per update -> render transition
            void* cBufferMem = gfx::MapBuffer(gfxDevice, cBuffer, gfx::MapType::MAP_WRITE_DISCARD);
            if (cBufferMem != nullptr) {
                memcpy(cBufferMem, &transform, sizeof(transform));
                gfx::UnmapBuffer(gfxDevice, cBuffer);
            }
            

            // upload new weights to weight vertex buffer
            void* vWeightBufferMem = gfx::MapBuffer(gfxDevice, vWeightBuffer, gfx::MapType::MAP_WRITE_DISCARD);
            if (vWeightBufferMem != nullptr) {
                memcpy(vWeightBufferMem, triangleWeights, sizeof(triangleWeights));
                gfx::UnmapBuffer(gfxDevice, vWeightBuffer);
            }
        }

        /* Begin render frame*/

        auto renderFrameTimerStart = GetCounter();
        auto cmdRecordingTimerStart = GetCounter();

        GT_LOG_INFO("RenderProfile", "Command recording took %f ms", 1000.0 * (GetCounter() - cmdRecordingTimerStart));
        
        // draw geometry
        auto commandSubmissionTimerStart = GetCounter();
       
        gfx::RenderPassAction clearAllAction;
        clearAllAction.colors[0].action = gfx::Action::ACTION_CLEAR;
        float blue[] = { bgColor[0], bgColor[1], bgColor[2], 1.0f };
        memcpy(clearAllAction.colors[0].color, blue, sizeof(float) * 4);
        
        gfx::BeginDefaultRenderPass(gfxDevice, cmdBuffer, swapChain, &clearAllAction);
        gfx::SetViewport(gfxDevice, cmdBuffer, { (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT });
        gfx::SubmitDrawCall(gfxDevice, cmdBuffer, &triangleDrawCall);
        gfx::EndRenderPass(gfxDevice, cmdBuffer);

        gfx::BeginDefaultRenderPass(gfxDevice, cmdBuffer, secondSwapChain, &clearAllAction);
        gfx::SetViewport(gfxDevice, cmdBuffer, { (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT });
        gfx::SubmitDrawCall(gfxDevice, cmdBuffer, &cubeDrawCall);
        gfx::EndRenderPass(gfxDevice, cmdBuffer);

        GT_LOG_INFO("RenderProfile", "Command submission took %f ms", 1000.0 * (GetCounter() - commandSubmissionTimerStart));
       
        // draw UI
        auto uiDrawData = ImGui::GetDrawData();
        if (uiDrawData) {
            //g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            float black[] = { 0.0f, 0.0f, 0.0f, 1.0f };
            
            //g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, bgColor);
            //ImGui_ImplDX11_RenderDrawLists(uiDrawData);
        }

        /* Present render frame*/
        auto presentTimerStart = GetCounter();
        gfx::PresentSwapChain(gfxDevice, swapChain);
        gfx::PresentSwapChain(gfxDevice, secondSwapChain);
        //g_pSwapChain->Present(0, 0);
        GT_LOG_INFO("RenderProfile", "Present took %f ms", 1000.0 * (GetCounter() - presentTimerStart));
        //GT_LOG_INFO("RenderProfile", "Render frame took %f ms", 1000.0 * (GetCounter() - renderFrameTimerStart));
    } while (!exitFlag);

    //ImGui_ImplDX11_Shutdown();

    fnd::sockets::ShutdownSocketLayer();

    return 0;
}

#ifndef GT_SHARED_LIB
int main(int argc, char* argv[])
{
    return win32_main(argc, argv);
}
#endif
