
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef near
#undef far

#include <engine/runtime/ImGui/imgui.h>
#include <engine/runtime/win32/imgui_impl_dx11.h>

#include <tchar.h>
#include <stdio.h>
#include <malloc.h>

#include <fontawesome/IconsFontAwesome.h>
#include <fontawesome/IconsMaterialDesign.h>

#include <foundation/int_types.h>
#include <foundation/memory/memory.h>
#include <foundation/memory/allocators.h>
#include <foundation/logging/logging.h>

#include <engine/runtime/gfx/gfx.h>

#include <foundation/math/math.h>
#define IS_POW_OF_TWO(n) ((n & (n - 1)) == 0)

int WINDOW_WIDTH = 1920;
int WINDOW_HEIGHT = 1080;

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

bool g_renderUIOffscreen = true;
bool g_enableUIBlur = true;

extern LRESULT ImGui_ImplDX11_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplDX11_WndProcHandler(hWnd, msg, wParam, lParam)) {
        //return true;
    }
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


    /*void Make4x4FloatProjectionMatrixLH(float* mat, float fovInRadians, float aspect, float near, float far)
    {
        Make4x4FloatMatrixIdentity(mat);

        float tanHalfFovy = tanf(fovInRadians / 2.0f);

        Set4x4FloatMatrixValue(mat, 0, 0, 1.0f / (aspect * tanHalfFovy));
        Set4x4FloatMatrixValue(mat, 1, 1, 1.0f / tanHalfFovy);
        Set4x4FloatMatrixValue(mat, 2, 3, 1.0f);
        
        Set4x4FloatMatrixValue(mat, 2, 2, (far * near) / (far - near));
        Set4x4FloatMatrixValue(mat, 3, 2, -(2.0f * far * near) / (far - near));
    }*/
    void Make4x4FloatProjectionMatrixLH(float* mat, float fovInRadians, float width, float height, float near, float far)
    {
        Make4x4FloatMatrixIdentity(mat);
        
        float yScale = 1 / tanf(fovInRadians / 2.0f);
        float xScale = yScale / (width / height);

        Set4x4FloatMatrixValue(mat, 0, 0, xScale);
        Set4x4FloatMatrixValue(mat, 1, 1, yScale);
        Set4x4FloatMatrixValue(mat, 2, 2, far / (far - near));
        Set4x4FloatMatrixValue(mat, 3, 2, -near * far / (far - near));
        Set4x4FloatMatrixValue(mat, 2, 3, 1.0f);
        Set4x4FloatMatrixValue(mat, 3, 3, 0.0f);
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

#include <engine/runtime/ImGuizmo/ImGuizmo.h>
void EditTransform(float camera[16], float projection[16], float matrix[16])
{
    static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
    static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);
    if (ImGui::IsKeyPressed(90)) 
        mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
    if (ImGui::IsKeyPressed(69))
        mCurrentGizmoOperation = ImGuizmo::ROTATE;
    if (ImGui::IsKeyPressed(82)) // r Key
        mCurrentGizmoOperation = ImGuizmo::SCALE;
    if (ImGui::RadioButton(" " ICON_FA_ARROWS "  Translation", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
        mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGui::SameLine();
    if (ImGui::RadioButton(" " ICON_FA_REFRESH "  Rotation", mCurrentGizmoOperation == ImGuizmo::ROTATE))
        mCurrentGizmoOperation = ImGuizmo::ROTATE;
    ImGui::SameLine();
    if (ImGui::RadioButton(" " ICON_FA_EXPAND "  Scaling", mCurrentGizmoOperation == ImGuizmo::SCALE))
        mCurrentGizmoOperation = ImGuizmo::SCALE;
    fnd::math::float3 matrixTranslation, matrixRotation, matrixScale;
    ImGuizmo::DecomposeMatrixToComponents(matrix, (float*)matrixTranslation, (float*)matrixRotation, (float*)matrixScale);
    ImGui::DragFloat3(" " ICON_FA_ARROWS, (float*)matrixTranslation, 0.1f);
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_UNDO "##translate")) { matrixTranslation = {0.0f, 0.0f, 0.0f}; }
    ImGui::DragFloat3(" " ICON_FA_REFRESH, (float*)matrixRotation, 0.1f);
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_UNDO "##rotation")) { matrixRotation = { 0.0f, 0.0f, 0.0f }; }
    ImGui::DragFloat3(" " ICON_FA_EXPAND, (float*)matrixScale, 0.1f);
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_UNDO "##scale")) { matrixScale = { 1.0f, 1.0f, 1.0f }; }
    ImGuizmo::RecomposeMatrixFromComponents((float*)matrixTranslation, (float*)matrixRotation, (float*)matrixScale, matrix);

    if (mCurrentGizmoOperation != ImGuizmo::SCALE)
    {
        if (ImGui::RadioButton(" " ICON_FA_CUBE "  Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
            mCurrentGizmoMode = ImGuizmo::LOCAL;
        ImGui::SameLine();
        if (ImGui::RadioButton(" " ICON_FA_GLOBE "  World", mCurrentGizmoMode == ImGuizmo::WORLD))
            mCurrentGizmoMode = ImGuizmo::WORLD;
    }
    static bool useSnap(false);
    if (ImGui::IsKeyPressed(83))
        useSnap = !useSnap;
    // lol
    ImGui::Checkbox("##snap", &useSnap);
    ImGui::SameLine();
    static fnd::math::float3 snap = { 0.1f, 0.1f, 0.1f };
    switch (mCurrentGizmoOperation)
    {
    case ImGuizmo::TRANSLATE:
        //snap = fnd::math::float3(0.1f);
        ImGui::InputFloat3(" " ICON_FA_TH "  Snap", &snap.x);
        break;
    case ImGuizmo::ROTATE:
        //snap = fnd::math::float3(0.1f);
        ImGui::InputFloat(" " ICON_FA_TH "  Snap", &snap.x);
        break;
    case ImGuizmo::SCALE:
        //snap = fnd::math::float3(0.1f);
        ImGui::InputFloat(" " ICON_FA_TH "  Snap", &snap.x);
        break;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    ImGuizmo::Manipulate(camera, projection, mCurrentGizmoOperation, mCurrentGizmoMode, matrix, NULL, useSnap ? &snap.x : NULL);
}

#pragma warning(push, 0)    // lots of warnings in here  
#define PAR_SHAPES_IMPLEMENTATION
#include <engine/runtime/par_shapes-h.h>
#pragma warning(pop)

#define STB_IMAGE_IMPLEMENTATION
//#define STBI_NO_STDIO
#include <stb/stb_image.h>

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

    RECT windowRect;
    windowRect.left = 0;
    windowRect.top = 0;
    windowRect.right = WINDOW_WIDTH;
    windowRect.bottom = WINDOW_HEIGHT;
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_OVERLAPPEDWINDOW);
    g_hwnd = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, _T("GTRuntimeWindowClass"), _T("GT Runtime"), WS_OVERLAPPEDWINDOW, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, NULL, NULL, wc.hInstance, NULL);
   
    RECT r;
    GetClientRect(g_hwnd, &r);
    WINDOW_WIDTH = r.right - r.left;
    WINDOW_HEIGHT = r.bottom - r.top;

    if (!g_hwnd) {
        GT_LOG_ERROR("Application", "failed to create a window\n");
        return 1;
    }

    GT_LOG_INFO("Application", "Created application window");

    //const float bgColor[] = { 100.0f / 255.0f, 149.0f / 255.0f, 237.0f / 255.0f };
    const float bgColor[] = { 0.0f, 0.0f, 0.0f };
    bool paint = false;
    // Initialize Direct3D
    /*if (CreateDeviceD3D(g_hwnd) < 0)
    {
        CleanupDeviceD3D();
        UnregisterClass(_T("void"), wc.hInstance);
        return 1;
    }*/

    GT_LOG_INFO("Application", "Initialized gfx device");

   

    // Show the window
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    //

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

    struct ConstantData {
        float transform[16];
        math::float4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        math::float4 lightDir = { 1.0f, -1.0f, 1.0f, 0.0f };
    };

    float proj[16];
    float camera[16];
    util::Make4x4FloatProjectionMatrixLH(proj, 1.0f, (float)WINDOW_WIDTH,  (float)WINDOW_HEIGHT, 0.1f, 1000.0f);
    util::Make4x4FloatTranslationMatrix(camera, { 0.0f, -0.4f, 2.75f });

    ConstantData object;
    util::Make4x4FloatMatrixIdentity(object.transform);


    //object.transform[4 * 3 + 3] = 1.0f;
    auto cubeMesh = par_shapes_create_klein_bottle(35, 35);
    //par_shapes_translate(cubeMesh, 0.5f, 0.5f, 0.5f);
    //par_shapes_compute_normals(cubeMesh);
    
    float* cubeVertices = cubeMesh->points;
    float* cubeNormals = cubeMesh->normals;
    float* cubeTexcoords = cubeMesh->tcoords;
    PAR_SHAPES_T* cubeIndices = cubeMesh->triangles;
    int numCubeVertices = cubeMesh->npoints;
    int numCubeIndices = cubeMesh->ntriangles * 3;
    
    gfx::Image cubeTexture;
    {
        int width, height, numComponents;
        auto image = stbi_load("../../texture.png", &width, &height, &numComponents, 4);
        //image = stbi_load_from_memory(buf, buf_len, &width, &height, &numComponents, 4);
        if (image == NULL) {
            GT_LOG_ERROR("Assets", "Failed to load image %s:\n%s\n", "../../texture.png", stbi_failure_reason());
        }
        //assert(numComponents == 4);

        gfx::ImageDesc cubeTextureDesc;
        //cubeTextureDesc.usage = gfx::ResourceUsage::USAGE_DYNAMIC;
        cubeTextureDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
        cubeTextureDesc.width = width;
        cubeTextureDesc.height = height;
        cubeTextureDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R8G8B8A8_UNORM;
        cubeTextureDesc.numDataItems = 1;
        void* data[] = { image };
        size_t size = sizeof(stbi_uc) * width * height * 4;
        cubeTextureDesc.initialData = data;
        cubeTextureDesc.initialDataSizes = &size;
        cubeTexture = gfx::CreateImage(gfxDevice, &cubeTextureDesc);
        if (!GFX_CHECK_RESOURCE(cubeTexture)) {
            GT_LOG_ERROR("Renderer", "Failed to create texture");
        }
        stbi_image_free(image);
    }

    const size_t NUM_PAINT_TEXTURES = 4;
    gfx::Image paintTexture[NUM_PAINT_TEXTURES];
    char fileNameBuf[512] = "";
    for(size_t i = 0; i < NUM_PAINT_TEXTURES; ++i) {
        int width, height, numComponents;
        snprintf(fileNameBuf, 512, "../../texture%llu.png", i);
        auto image = stbi_load(fileNameBuf, &width, &height, &numComponents, 4);
        //image = stbi_load_from_memory(buf, buf_len, &width, &height, &numComponents, 4);
        if (image == NULL) {
            GT_LOG_ERROR("Assets", "Failed to load image %s:\n%s\n", fileNameBuf, stbi_failure_reason());
        }
        //assert(numComponents == 4);

        gfx::ImageDesc paintTextureDesc;
        //paintTextureDesc.usage = gfx::ResourceUsage::USAGE_DYNAMIC;
        paintTextureDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
        paintTextureDesc.width = width;
        paintTextureDesc.height = height;
        paintTextureDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R8G8B8A8_UNORM;
        paintTextureDesc.numDataItems = 1;
        void* data[] = { image };
        size_t size = sizeof(stbi_uc) * width * height * 4;
        paintTextureDesc.initialData = data;
        paintTextureDesc.initialDataSizes = &size;
        paintTexture[i] = gfx::CreateImage(gfxDevice, &paintTextureDesc);
        if (!GFX_CHECK_RESOURCE(paintTexture[i])) {
            GT_LOG_ERROR("Renderer", "Failed to create texture");
        }
        stbi_image_free(image);
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

    gfx::BufferDesc cubeNormalBufferDesc;
    cubeNormalBufferDesc.type = gfx::BufferType::BUFFER_TYPE_VERTEX;
    cubeNormalBufferDesc.byteWidth = sizeof(float) * numCubeVertices * 3;
    cubeNormalBufferDesc.initialData = cubeNormals;
    cubeNormalBufferDesc.initialDataSize = cubeNormalBufferDesc.byteWidth;
    gfx::Buffer cubeNormalBuffer = gfx::CreateBuffer(gfxDevice, &cubeNormalBufferDesc);
    if (!GFX_CHECK_RESOURCE(cubeNormalBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create cube normal buffer");
    }

    gfx::BufferDesc cubeUVBufferDesc;
    cubeUVBufferDesc.type = gfx::BufferType::BUFFER_TYPE_VERTEX;
    cubeUVBufferDesc.byteWidth = sizeof(float) * numCubeVertices * 2;
    cubeUVBufferDesc.initialData = cubeTexcoords;
    cubeUVBufferDesc.initialDataSize = cubeUVBufferDesc.byteWidth;
    gfx::Buffer cubeUVBuffer = gfx::CreateBuffer(gfxDevice, &cubeUVBufferDesc);
    if (!GFX_CHECK_RESOURCE(cubeUVBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create cube uv buffer");
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

    struct PaintConstantData
    {
        float modelToViewMatrix[16];
        float modelToProjMatrix[16];
        math::float2 cursorPos;
        math::float2 _padding;
        math::float4 color;
        float brushSize;
        float _padding2[3];
    } paintConstantData;

    gfx::BufferDesc cPaintBufferDesc;
    cPaintBufferDesc.type = gfx::BufferType::BUFFER_TYPE_CONSTANT;
    cPaintBufferDesc.byteWidth = sizeof(PaintConstantData);
    cPaintBufferDesc.initialData = &paintConstantData;
    cPaintBufferDesc.initialDataSize = sizeof(paintConstantData);
    cPaintBufferDesc.usage = gfx::ResourceUsage::USAGE_STREAM;
    gfx::Buffer cPaintBuffer = gfx::CreateBuffer(gfxDevice, &cPaintBufferDesc);
    if (!GFX_CHECK_RESOURCE(cPaintBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create constant buffer");
    }

    gfx::BufferDesc cBufferDesc;
    cBufferDesc.type = gfx::BufferType::BUFFER_TYPE_CONSTANT;
    cBufferDesc.byteWidth = sizeof(ConstantData);
    cBufferDesc.initialData = &object;
    cBufferDesc.initialDataSize = sizeof(object);
    cBufferDesc.usage = gfx::ResourceUsage::USAGE_STREAM;
    gfx::Buffer cBuffer = gfx::CreateBuffer(gfxDevice, &cBufferDesc);
    if (!GFX_CHECK_RESOURCE(cBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create constant buffer");
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

    gfx::ShaderDesc pBlurShaderDesc;
    pBlurShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
    pBlurShaderDesc.code = pBlurShaderCode;
    pBlurShaderDesc.codeSize = pBlurShaderCodeSize;

    gfx::ShaderDesc vPaintShaderDesc;
    vPaintShaderDesc.type = gfx::ShaderType::SHADER_TYPE_VS;
    vPaintShaderDesc.code = vPaintShaderCode;
    vPaintShaderDesc.codeSize = vPaintShaderCodeSize;
    gfx::ShaderDesc pPaintShaderDesc;
    pPaintShaderDesc.type = gfx::ShaderType::SHADER_TYPE_PS;
    pPaintShaderDesc.code = pPaintShaderCode;
    pPaintShaderDesc.codeSize = pPaintShaderCodeSize;

    gfx::Shader vCubeShader = gfx::CreateShader(gfxDevice, &vCubeShaderDesc);
    if (!GFX_CHECK_RESOURCE(vCubeShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create vertex shader");
    }

    gfx::Shader pShader = gfx::CreateShader(gfxDevice, &pShaderDesc);
    if (!GFX_CHECK_RESOURCE(pShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
    }

    gfx::Shader vBlitShader = gfx::CreateShader(gfxDevice, &vBlitShaderDesc);
    if (!GFX_CHECK_RESOURCE(vBlitShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create vertex shader");
    }

    gfx::Shader pBlitShader = gfx::CreateShader(gfxDevice, &pBlitShaderDesc);
    if (!GFX_CHECK_RESOURCE(pBlitShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
    }

    gfx::Shader pBlurShader = gfx::CreateShader(gfxDevice, &pBlurShaderDesc);
    if (!GFX_CHECK_RESOURCE(pBlurShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
    }

    gfx::Shader vPaintShader = gfx::CreateShader(gfxDevice, &vPaintShaderDesc);
    if (!GFX_CHECK_RESOURCE(vPaintShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create vertex shader");
    }

    gfx::Shader pPaintShader = gfx::CreateShader(gfxDevice, &pPaintShaderDesc);
    if (!GFX_CHECK_RESOURCE(pPaintShader)) {
        GT_LOG_ERROR("Renderer", "Failed to create pixel shader");
    }


    gfx::PipelineStateDesc cubePipelineStateDesc;
    cubePipelineStateDesc.blendState.enableBlend = true;
    cubePipelineStateDesc.blendState.srcBlend = gfx::BlendFactor::BLEND_SRC_ALPHA;
    cubePipelineStateDesc.blendState.dstBlend = gfx::BlendFactor::BLEND_INV_SRC_ALPHA;
    cubePipelineStateDesc.blendState.writeMask = gfx::COLOR_WRITE_MASK_COLOR;

    //cubePipelineStateDesc.rasterState.cullMode = gfx::CullMode::CULL_NONE;
    cubePipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_UINT16;
    cubePipelineStateDesc.vertexShader = vCubeShader;
    cubePipelineStateDesc.pixelShader = pShader;
    cubePipelineStateDesc.vertexLayout.attribs[0] = { "POSITION", 0, 0, 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT3 };
    cubePipelineStateDesc.vertexLayout.attribs[1] = { "NORMAL", 0, 0, 1, gfx::VertexFormat::VERTEX_FORMAT_FLOAT3 };
    cubePipelineStateDesc.vertexLayout.attribs[2] = { "TEXCOORD", 0, 0, 2, gfx::VertexFormat::VERTEX_FORMAT_FLOAT2 };

    gfx::PipelineState cubePipeline = gfx::CreatePipelineState(gfxDevice, &cubePipelineStateDesc);
    if (!GFX_CHECK_RESOURCE(cubePipeline)) {
        GT_LOG_ERROR("Renderer", "Failed to create pipeline state for cube");
    }

    gfx::PipelineStateDesc blitPipelineStateDesc;
    blitPipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_NONE;
    blitPipelineStateDesc.vertexShader = vBlitShader;
    blitPipelineStateDesc.pixelShader = pBlitShader;
    blitPipelineStateDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLE_STRIP;
    
    blitPipelineStateDesc.depthStencilState.enableDepth = false;
    blitPipelineStateDesc.blendState.enableBlend = true;
    blitPipelineStateDesc.blendState.srcBlend = gfx::BlendFactor::BLEND_ONE;
    blitPipelineStateDesc.blendState.dstBlend = gfx::BlendFactor::BLEND_SRC_ALPHA;
    blitPipelineStateDesc.blendState.blendOp = gfx::BlendOp::BLEND_OP_ADD;
    blitPipelineStateDesc.blendState.writeMask = gfx::COLOR_WRITE_MASK_COLOR;
    gfx::PipelineState blitPipeline = gfx::CreatePipelineState(gfxDevice, &blitPipelineStateDesc);
    if (!GFX_CHECK_RESOURCE(blitPipeline)) {
        GT_LOG_ERROR("Renderer", "Failed to create pipeline state for blit");
    }

    gfx::PipelineStateDesc blurPipelineStateDesc;
    blurPipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_NONE;
    blurPipelineStateDesc.vertexShader = vBlitShader;
    blurPipelineStateDesc.pixelShader = pBlurShader;
    blurPipelineStateDesc.primitiveType = gfx::PrimitiveType::PRIMITIVE_TYPE_TRIANGLE_STRIP;

    blurPipelineStateDesc.depthStencilState.enableDepth = false;
    blurPipelineStateDesc.blendState.enableBlend = false;
    blurPipelineStateDesc.blendState.srcBlend = gfx::BlendFactor::BLEND_SRC_ALPHA;
    blurPipelineStateDesc.blendState.dstBlend = gfx::BlendFactor::BLEND_INV_SRC_ALPHA;
    blurPipelineStateDesc.blendState.blendOp = gfx::BlendOp::BLEND_OP_ADD;
    blurPipelineStateDesc.blendState.writeMask = gfx::COLOR_WRITE_MASK_COLOR;
    gfx::PipelineState blurPipeline = gfx::CreatePipelineState(gfxDevice, &blurPipelineStateDesc);
    if (!GFX_CHECK_RESOURCE(blurPipeline)) {
        GT_LOG_ERROR("Renderer", "Failed to create pipeline state for blit");
    }

   
    gfx::ImageDesc uiRenderTargetDesc;
    uiRenderTargetDesc.isRenderTarget = true;
    uiRenderTargetDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
    uiRenderTargetDesc.width = WINDOW_WIDTH;
    uiRenderTargetDesc.height = WINDOW_HEIGHT;
    gfx::Image uiRenderTarget = gfx::CreateImage(gfxDevice, &uiRenderTargetDesc);
    if (!GFX_CHECK_RESOURCE(uiRenderTarget)) {
        GT_LOG_ERROR("Renderer", "Failed to create render target for UI");
    }

    gfx::ImageDesc paintRTDesc;
    paintRTDesc.isRenderTarget = true;
    paintRTDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
    paintRTDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R16G16B16A16_FLOAT;
    paintRTDesc.width = WINDOW_WIDTH;
    paintRTDesc.height = WINDOW_HEIGHT;
    gfx::Image paintRT = gfx::CreateImage(gfxDevice, &paintRTDesc);
    if (!GFX_CHECK_RESOURCE(paintRT)) {
        GT_LOG_ERROR("Renderer", "Failed to create render target for paintshop");
    }

    gfx::ImageDesc mainRTDesc;
    mainRTDesc.isRenderTarget = true;
    mainRTDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
    mainRTDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_R16G16B16A16_FLOAT;
    mainRTDesc.width = WINDOW_WIDTH;
    mainRTDesc.height = WINDOW_HEIGHT;
    gfx::Image mainRT = gfx::CreateImage(gfxDevice, &mainRTDesc);
    if (!GFX_CHECK_RESOURCE(mainRT)) {
        GT_LOG_ERROR("Renderer", "Failed to create main render target");
    }

    gfx::ImageDesc mainDepthBufferDesc;
    //mainDepthBufferDesc.isRenderTarget = true;
    mainDepthBufferDesc.isDepthStencilTarget = true;
    mainDepthBufferDesc.type = gfx::ImageType::IMAGE_TYPE_2D;
    mainDepthBufferDesc.pixelFormat = gfx::PixelFormat::PIXEL_FORMAT_D32_FLOAT_S8X24_UINT;
    mainDepthBufferDesc.width = WINDOW_WIDTH;
    mainDepthBufferDesc.height = WINDOW_HEIGHT;
    gfx::Image mainDepthBuffer = gfx::CreateImage(gfxDevice, &mainDepthBufferDesc);
    if (!GFX_CHECK_RESOURCE(mainDepthBuffer)) {
        GT_LOG_ERROR("Renderer", "Failed to create main depth buffer target");
    }

    gfx::PipelineStateDesc paintObjPipelineStateDesc;
    paintObjPipelineStateDesc.blendState.enableBlend = true;
    paintObjPipelineStateDesc.blendState.srcBlend = gfx::BlendFactor::BLEND_SRC_ALPHA;
    paintObjPipelineStateDesc.blendState.dstBlend = gfx::BlendFactor::BLEND_INV_SRC_ALPHA;
    paintObjPipelineStateDesc.blendState.blendOp = gfx::BlendOp::BLEND_OP_ADD;
    paintObjPipelineStateDesc.blendState.srcBlendAlpha = gfx::BlendFactor::BLEND_ZERO;
    paintObjPipelineStateDesc.blendState.dstBlendAlpha = gfx::BlendFactor::BLEND_INV_SRC_ALPHA;
    paintObjPipelineStateDesc.blendState.blendOpAlpha = gfx::BlendOp::BLEND_OP_ADD;

    paintObjPipelineStateDesc.blendState.writeMask = gfx::COLOR_WRITE_MASK_ALL;

    paintObjPipelineStateDesc.depthStencilState.enableDepth = false;
    paintObjPipelineStateDesc.rasterState.cullMode = gfx::CullMode::CULL_NONE;
    paintObjPipelineStateDesc.indexFormat = gfx::IndexFormat::INDEX_FORMAT_UINT16;
    paintObjPipelineStateDesc.vertexShader = vPaintShader;
    paintObjPipelineStateDesc.pixelShader = pPaintShader;
    paintObjPipelineStateDesc.vertexLayout.attribs[0] = { "POSITION", 0, 0, 0, gfx::VertexFormat::VERTEX_FORMAT_FLOAT3 };
    paintObjPipelineStateDesc.vertexLayout.attribs[1] = { "NORMAL", 0, 0, 1, gfx::VertexFormat::VERTEX_FORMAT_FLOAT3 };
    paintObjPipelineStateDesc.vertexLayout.attribs[2] = { "TEXCOORD", 0, 0, 2, gfx::VertexFormat::VERTEX_FORMAT_FLOAT2 };

    gfx::PipelineState paintObjPipelineState = gfx::CreatePipelineState(gfxDevice, &paintObjPipelineStateDesc);
    if (!GFX_CHECK_RESOURCE(paintObjPipelineState)) {
        GT_LOG_ERROR("Renderer", "Failed to create pipeline state for painting");
    }

    gfx::DrawCall cubeDrawCall;
    cubeDrawCall.vertexBuffers[0] = cubeVertexBuffer;
    cubeDrawCall.vertexOffsets[0] = 0;
    cubeDrawCall.vertexStrides[0] = sizeof(float) * 3;
    cubeDrawCall.vertexBuffers[1] = cubeNormalBuffer;
    cubeDrawCall.vertexOffsets[1] = 0;
    cubeDrawCall.vertexStrides[1] = sizeof(float) * 3;
    cubeDrawCall.vertexBuffers[2] = cubeUVBuffer;
    cubeDrawCall.vertexOffsets[2] = 0;
    cubeDrawCall.vertexStrides[2] = sizeof(float) * 2;
    cubeDrawCall.indexBuffer = cubeIndexBuffer;
    cubeDrawCall.numElements = numCubeIndices;
    cubeDrawCall.pipelineState = cubePipeline;
    cubeDrawCall.vsConstantInputs[0] = cBuffer;
    cubeDrawCall.psConstantInputs[0] = cBuffer;
    cubeDrawCall.psImageInputs[0] = cubeTexture;
    cubeDrawCall.psImageInputs[1] = paintRT;

    gfx::DrawCall cubePaintDrawCall;
    cubePaintDrawCall.vertexBuffers[0] = cubeVertexBuffer;
    cubePaintDrawCall.vertexOffsets[0] = 0;
    cubePaintDrawCall.vertexStrides[0] = sizeof(float) * 3;
    cubePaintDrawCall.vertexBuffers[1] = cubeNormalBuffer;
    cubePaintDrawCall.vertexOffsets[1] = 0;
    cubePaintDrawCall.vertexStrides[1] = sizeof(float) * 3;
    cubePaintDrawCall.vertexBuffers[2] = cubeUVBuffer;
    cubePaintDrawCall.vertexOffsets[2] = 0;
    cubePaintDrawCall.vertexStrides[2] = sizeof(float) * 2;
    cubePaintDrawCall.indexBuffer = cubeIndexBuffer;
    cubePaintDrawCall.numElements = numCubeIndices;
    cubePaintDrawCall.pipelineState = paintObjPipelineState;
    cubePaintDrawCall.vsConstantInputs[0] = cPaintBuffer;
    cubePaintDrawCall.psConstantInputs[0] = cPaintBuffer;
    cubePaintDrawCall.psImageInputs[0] = paintTexture[0];

    

    // @TODO depth attachment
    gfx::RenderPassDesc mainRenderPassDesc;
    mainRenderPassDesc.colorAttachments[0].image = mainRT;
    mainRenderPassDesc.depthStencilAttachment.image = mainDepthBuffer;
    gfx::RenderPass mainPass = gfx::CreateRenderPass(gfxDevice, &mainRenderPassDesc);
    if (!GFX_CHECK_RESOURCE(mainPass)) {
        GT_LOG_ERROR("Renderer", "Failed to create main render pass");
    }

    gfx::RenderPassDesc uiPassDesc;
    uiPassDesc.colorAttachments[0].image = uiRenderTarget;
    gfx::RenderPass uiPass = gfx::CreateRenderPass(gfxDevice, &uiPassDesc);
    if (!GFX_CHECK_RESOURCE(uiPass)) {
        GT_LOG_ERROR("Renderer", "Failed to create render pass for UI");
    }

    gfx::RenderPassDesc paintPassDesc;
    paintPassDesc.colorAttachments[0].image = paintRT;
    gfx::RenderPass paintPass = gfx::CreateRenderPass(gfxDevice, &paintPassDesc);
    if (!GFX_CHECK_RESOURCE(paintPass)) {
        GT_LOG_ERROR("Renderer", "Failed to create render pass for painting");
    }

    GT_LOG_INFO("Application", "Initialized graphics scene");

    gfx::CommandBuffer cmdBuffer = gfx::GetImmediateCommandBuffer(gfxDevice);


    ImGui_ImplDX11_Init(g_hwnd, gfxDevice);

    ImGuiIO& io = ImGui::GetIO();   // load a nice font
    io.Fonts->AddFontFromFileTTF("../../extra_fonts/Roboto-Medium.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesDefault());
    ImFontConfig icons_config; icons_config.MergeMode = true; icons_config.PixelSnapH = true;
    {   // merge in icons from Font Awesome
        static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        io.Fonts->AddFontFromFileTTF("../../extra_fonts/fontawesome-webfont.ttf", 16.0f, &icons_config, icons_ranges);
    }



    ImGui_Style_SetDark(0.8f);

    GT_LOG_INFO("Application", "Initialized UI");

    ///
    StartCounter();

    double t = 0.0;
    double dt = 1.0 / 60.0;

    double currentTime = GetCounter();
    double accumulator = 0.0;
    float model[16];
    util::Make4x4FloatMatrixIdentity(model);

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    gfx::RenderPassAction clearAllAction;
    clearAllAction.colors[0].action = gfx::Action::ACTION_CLEAR;
    clearAllAction.depth.action = gfx::Action::ACTION_CLEAR;
    float blue[] = { bgColor[0], bgColor[1], bgColor[2], 1.0f };
    memcpy(clearAllAction.colors[0].color, blue, sizeof(float) * 4);

    gfx::RenderPassAction clearMaybeAction;
    clearMaybeAction.colors[0].color[0] = 0.0f;
    clearMaybeAction.colors[0].color[1] = 0.0f;
    clearMaybeAction.colors[0].color[2] = 0.0f;
    clearMaybeAction.colors[0].color[3] = 1.0f;
    clearMaybeAction.colors[0].action = gfx::Action::ACTION_LOAD;
   
    clearMaybeAction.colors[0].action = gfx::Action::ACTION_CLEAR;
    gfx::BeginRenderPass(gfxDevice, cmdBuffer, paintPass, &clearMaybeAction);
    gfx::EndRenderPass(gfxDevice, cmdBuffer);
    clearMaybeAction.colors[0].action = gfx::Action::ACTION_LOAD;
   
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

            ImGui_ImplDX11_NewFrame();
            ImGuizmo::BeginFrame();

#ifdef GT_DEVELOPMENT
            if (ImGui::Begin("Memory usage", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

                size_t totalSize = 0;
                auto it = GetMemTrackerListHead();
                while (it != nullptr) {

                    totalSize += it->GetUsedMemorySize();
                    if (ImGui::TreeNode(it->GetName())) {
                        ImGui::Text("%llu kB allocated", it->GetUsedMemorySize() / 1024);
                        ImGui::TreePop();
                    }

                    it = it->GetNext();
                }

                ImGui::Separator();
                ImGui::Text("Total usage: %llu kb", totalSize / 1024);
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


            if (ImGui::Button("Clear")) {
                clearMaybeAction.colors[0].action = gfx::Action::ACTION_CLEAR;
                gfx::BeginRenderPass(gfxDevice, cmdBuffer, paintPass, &clearMaybeAction);
                gfx::EndRenderPass(gfxDevice, cmdBuffer);
                clearMaybeAction.colors[0].action = gfx::Action::ACTION_LOAD;
            }
            ImGui::Image((ImTextureID)(uintptr_t)paintRT.id, ImVec2(512, 512));

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
                    if (ImGui::Button(ICON_FA_WINDOW_CLOSE " Disconnect")) {
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

            
            ImGui::Begin("Foo"); {
                ImGui::Checkbox("Render UI to offscreen buffer", &g_renderUIOffscreen);
                ImGui::Checkbox("Enable UI Blur Effect", &g_enableUIBlur);
            } ImGui::End();

            static math::float3 mousePosScreenCache(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, 15.0f);
            math::float3 mousePosScreen(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, 15.0f);

            static float brushSizeSetting = 100.0f;
            float brushSize = brushSizeSetting;
            static bool modulateSizeWithRate = false;
            static float maxRate = 50.0;
            float rate = math::Length(mousePosScreen - mousePosScreenCache);
            //rate = rate <= 1.0f ? rate : 1.0f;
            if (modulateSizeWithRate) {
                if (maxRate >= 0.0f) {
                    brushSize = brushSizeSetting * ((maxRate - rate) / maxRate);
                }
                else {
                    brushSize = brushSizeSetting * (1.0f + (rate / -maxRate));
                }
                brushSize = brushSize > brushSizeSetting * 0.1f ? brushSize : brushSizeSetting * 0.1f;
            }
            float stepSize = brushSize * 0.25f / rate;
             
            
            ImGui::Begin("Brush Stats"); {
                ImGui::Text("Rate : %f", rate);
                ImGui::Text("Step Size : %f", stepSize);
                ImGui::Checkbox("Modulate Size", &modulateSizeWithRate);
                ImGui::DragFloat("Modulation Rate", &maxRate);
            } ImGui::End();

            /* Basic UI: frame statistics */
            ImGui::SetNextWindowPos(ImVec2(10.0f, ImGui::GetIO().DisplaySize.y - 50));
            ImGui::Begin("#framestatistics", (bool*)0, ImVec2(0, 0), 0.45f, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
            ImGui::Text("Window dimensions = %ix%i", WINDOW_WIDTH, WINDOW_HEIGHT);
            ImGui::Text("Mouse Screen Pos: %f, %f", mousePosScreen.x, mousePosScreen.y);
            ImGui::Text("Simulation time average: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();

            ImGui::Begin(ICON_FA_WRENCH "  Property Editor"); {
                ImGui::SliderFloat3("Sun Direction", (float*)object.lightDir, -1.0f, 1.0f);
                if (ImGui::TreeNode(ICON_FA_PENCIL "    Object")) {
                    static char namebuf[512] = "Generic Object";
                    if (ImGui::InputText(" " ICON_FA_TAG " Name", namebuf, 512, ImGuiInputTextFlags_EnterReturnsTrue)) {

                    }
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode(ICON_FA_LOCATION_ARROW "    Transform")) {
                    EditTransform(camera, proj, model);
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode(ICON_FA_PAINT_BRUSH "    Material")) {
                    ImGui::SliderFloat("Brush Size", &brushSizeSetting, 10.0f, 300.0f);
                    ImGui::SameLine();
         
                    auto drawList = ImGui::GetWindowDrawList();
                    ImVec2 circleCenter = ImGui::GetCursorScreenPos();
                    circleCenter.x += 25.0f;
                    circleCenter.y += 25.0f * 0.5f;
                    drawList->AddCircle(circleCenter, 25.0f * (brushSize / 300.0f), ImColor(1.0f, 1.0f, 1.0f, 1.0f), 64, 2.5f);
                    
                    ImGui::Dummy(ImVec2(100.0f, 50.0f));
                    static int pickerMode = 0;
                    const ImGuiColorEditFlags modes[] = { ImGuiColorEditFlags_PickerHueBar, ImGuiColorEditFlags_PickerHueWheel };
                    const char* labels[] = { "Hue Bar", "Hue Wheel" };
                    ImGui::Combo("Color Picker Mode", &pickerMode, labels, ARRAYSIZE(labels));
                    ImGuiColorEditFlags ceditFlags = 0;
                    ceditFlags |= modes[pickerMode];
                    ImGui::Spacing();
                    ImGui::ColorPicker4("Albedo", (float*)object.color, ceditFlags);

                    ImGui::Spacing();

                    static size_t selectionIndex = 0;

                    //paintTexture[3] = uiRenderTarget;   // hehe
                    for (size_t i = 0; i < NUM_PAINT_TEXTURES; ++i) {
                        if (selectionIndex == i) {
                            ImGui::Text(ICON_FA_LINK " texture%llu", i);
                        }
                        else {
                            ImGui::Text(ICON_FA_CHAIN_BROKEN " texture%llu", i);
                        }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                            selectionIndex = i;
                        }

                        ImGui::Image((ImTextureID)(uintptr_t)paintTexture[i].id, ImVec2(256, 256));
                        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                            selectionIndex = i;
                        }
                    }
                    cubePaintDrawCall.psImageInputs[0] = paintTexture[selectionIndex];

                    ImGui::TreePop();
                }
            } ImGui::End();

           
            float modelView[16];
            util::MultiplyMatrices(model, camera, modelView);
            util::MultiplyMatrices(modelView, proj, object.transform);

            //
            paint = ImGui::IsMouseDown(1);
            float steps = 0.0f;
            while (steps < 1.0f) {
                if (paint) {
                    void* cBufferMem = gfx::MapBuffer(gfxDevice, cPaintBuffer, gfx::MapType::MAP_WRITE_DISCARD);
                    if (cBufferMem != nullptr) {
                        PaintConstantData* data = (PaintConstantData*)cBufferMem;
                        data->cursorPos = mousePosScreen.xy * steps + mousePosScreenCache.xy * (1.0f - steps);
                        //memcpy(data->modelToViewMatrix, object.transform, sizeof(float) * 16);
                        util::Copy4x4FloatMatrix(modelView, data->modelToViewMatrix);
                        util::Copy4x4FloatMatrix(object.transform, data->modelToProjMatrix);
                        data->color = object.color;
                        data->brushSize = brushSize;
                        gfx::UnmapBuffer(gfxDevice, cPaintBuffer);
                    }

                    gfx::BeginRenderPass(gfxDevice, cmdBuffer, paintPass, &clearMaybeAction);
                    gfx::SubmitDrawCall(gfxDevice, cmdBuffer, &cubePaintDrawCall);
                    gfx::EndRenderPass(gfxDevice, cmdBuffer);
                }
                steps += stepSize;
            }
            mousePosScreenCache = mousePosScreen;
            if(paint)
            GT_LOG_INFO("Paintshop", "Did %f steps", steps / stepSize);

            /* End sim frame */
            ImGui::Render();
            t += dt;
            accumulator -= dt;
        }
        if (didUpdate) {
            // upload objectation to constant buffer only once per update -> render transition
            void* cBufferMem = gfx::MapBuffer(gfxDevice, cBuffer, gfx::MapType::MAP_WRITE_DISCARD);
            if (cBufferMem != nullptr) {
                memcpy(cBufferMem, &object, sizeof(object));
                gfx::UnmapBuffer(gfxDevice, cBuffer);
            }
           
            
        }

        
        /* Begin render frame*/

        auto renderFrameTimerStart = GetCounter();

        //GT_LOG_INFO("RenderProfile", "Command recording took %f ms", 1000.0 * (GetCounter() - cmdRecordingTimerStart));
        
        // draw geometry
        auto commandSubmissionTimerStart = GetCounter();
      
        gfx::BeginRenderPass(gfxDevice, cmdBuffer, mainPass, &clearAllAction);
        gfx::SubmitDrawCall(gfxDevice, cmdBuffer, &cubeDrawCall);
        gfx::EndRenderPass(gfxDevice, cmdBuffer);

        GT_LOG_INFO("RenderProfile", "Command submission took %f ms", 1000.0 * (GetCounter() - commandSubmissionTimerStart));
       
        // draw UI
        auto uiDrawData = ImGui::GetDrawData();
        if (uiDrawData) {
            gfx::RenderPassAction uiPassAction;
            
            
            if (g_renderUIOffscreen) {
                uiPassAction.colors[0].color[0] = 0.0f;
                uiPassAction.colors[0].color[1] = 0.0f;
                uiPassAction.colors[0].color[2] = 0.0f;
                uiPassAction.colors[0].color[3] = 1.0f;
                uiPassAction.colors[0].action = gfx::Action::ACTION_CLEAR;
                gfx::BeginRenderPass(gfxDevice, cmdBuffer, uiPass, &uiPassAction);
            }
            else {
                uiPassAction.colors[0].action = gfx::Action::ACTION_LOAD;
                gfx::BeginDefaultRenderPass(gfxDevice, cmdBuffer, swapChain, &uiPassAction);
            }
            ImGui_ImplDX11_RenderDrawLists(uiDrawData, &cmdBuffer);
            gfx::EndRenderPass(gfxDevice, cmdBuffer);
        }

        gfx::RenderPassAction blitAction;
        blitAction.colors[0].action = gfx::Action::ACTION_CLEAR;
        
        gfx::DrawCall blitDrawCall;
        blitDrawCall.pipelineState = blitPipeline;
        blitDrawCall.numElements = 4;
        blitDrawCall.psImageInputs[0] = mainRT;

        gfx::BeginDefaultRenderPass(gfxDevice, cmdBuffer, swapChain, &blitAction);
        gfx::SubmitDrawCall(gfxDevice, cmdBuffer, &blitDrawCall);
        gfx::EndRenderPass(gfxDevice, cmdBuffer);


        blitAction.colors[0].action = gfx::Action::ACTION_LOAD;

        if (g_enableUIBlur) {
            gfx::DrawCall blurDrawCall;
            blurDrawCall.pipelineState = blurPipeline;
            blurDrawCall.numElements = 4;
            blurDrawCall.psImageInputs[0] = mainRT;
            blurDrawCall.psImageInputs[1] = uiRenderTarget;

            gfx::BeginDefaultRenderPass(gfxDevice, cmdBuffer, swapChain, &blitAction);
            gfx::SubmitDrawCall(gfxDevice, cmdBuffer, &blurDrawCall);
            gfx::EndRenderPass(gfxDevice, cmdBuffer);
        }
        if (g_renderUIOffscreen) {
            blitDrawCall.psImageInputs[0] = uiRenderTarget;
            gfx::BeginDefaultRenderPass(gfxDevice, cmdBuffer, swapChain, &blitAction);
            gfx::SubmitDrawCall(gfxDevice, cmdBuffer, &blitDrawCall);
            gfx::EndRenderPass(gfxDevice, cmdBuffer);
        }

        /* Present render frame*/
        auto presentTimerStart = GetCounter();
        gfx::PresentSwapChain(gfxDevice, swapChain);
 
        GT_LOG_INFO("RenderProfile", "Present took %f ms", 1000.0 * (GetCounter() - presentTimerStart));
        GT_LOG_INFO("RenderProfile", "Render frame took %f ms", 1000.0 * (GetCounter() - renderFrameTimerStart));
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
