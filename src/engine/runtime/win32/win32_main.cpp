
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef near
#undef far

#include <engine/runtime/runtime.h>

#include <engine/runtime/ImGui/imgui.h>
#include <engine/runtime/win32/imgui_impl_dx11.h>

#include <tchar.h>
#include <stdio.h>
#include <malloc.h>

#include <engine/tools/fbx_importer/fbx_importer.h>

#include <fontawesome/IconsFontAwesome.h>
#include <fontawesome/IconsMaterialDesign.h>

#include <foundation/int_types.h>
#include <foundation/memory/memory.h>
#include <foundation/memory/allocators.h>
#include <foundation/logging/logging.h>

#include <engine/runtime/gfx/gfx.h>
#include <engine/runtime/entities/entities.h>

#include <foundation/math/math.h>
#define IS_POW_OF_TWO(n) ((n & (n - 1)) == 0)

#include <engine/runtime/core/api_registry.h>
#include <engine/runtime/renderer/renderer.h>

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


#define MOUSE_LEFT 0
#define MOUSE_RIGHT 1
#define MOUSE_MIDDLE 2

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

namespace runtime
{
    void SetWindowTitle(void* window, const char* title)
    {
        SetWindowTextA((HWND)window, title);
    }

    void* CreateWindow()
    {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, _T("GTRuntimeWindowClass"), NULL };
        RegisterClassEx(&wc);

        RECT windowRect;
        windowRect.left = 0;
        windowRect.top = 0;
        windowRect.right = WINDOW_WIDTH;
        windowRect.bottom = WINDOW_HEIGHT;
        AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_OVERLAPPEDWINDOW);
        auto hwnd = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, _T("GTRuntimeWindowClass"), _T("GT Runtime"), WS_OVERLAPPEDWINDOW, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, NULL, NULL, wc.hInstance, NULL);
        return hwnd;
    }

    void DestroyWindow(void* window)
    {
        DestroyWindow((HWND)window);
    }

    void GetWindowSize(void* window, int* w, int* h)
    {
        RECT r;
        GetClientRect(g_hwnd, &r);  // @TODO should probably have an extra function for this and use GetWindowRect instead here
        *w = r.right - r.left;
        *h = r.bottom - r.top;
    }

    void SetWindowSize(void* window, int w, int h)
    {
        SetWindowPos((HWND)window, NULL, 0, 0, w, h, SWP_NOMOVE);
    }
}


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

    const size_t reservedMemorySize = GIGABYTES(4);
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
    
   /*
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
    */
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
    const float bgColor[] = { 0.2f, 0.2f, 0.2f };
    bool paint = false;
    // Initialize Direct3D
    /*if (CreateDeviceD3D(g_hwnd) < 0)
    {
        CleanupDeviceD3D();
        UnregisterClass(_T("void"), wc.hInstance);
        return 1;
    }*/


   

    // Show the window
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    //

    

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
    GT_LOG_INFO("Application", "Initialized gfx device");

    //
    renderer::Renderer* renderer = nullptr;
    renderer::RenderWorld* renderWorld = nullptr;

    renderer::RendererConfig rendererConfig;
    rendererConfig.gfxDevice = gfxDevice;
    rendererConfig.windowWidth = WINDOW_WIDTH;
    rendererConfig.windowHeight = WINDOW_HEIGHT;

    if (!renderer::CreateRenderer(&renderer, &applicationArena, &rendererConfig)) {
        GT_LOG_ERROR("Renderer", "Failed to create a renderer");
    }

    renderer::RenderWorldConfig renderWorldConfig;
    renderWorldConfig.renderer = renderer;
    if (!renderer::CreateRenderWorld(&renderWorld, &applicationArena, &renderWorldConfig)) {
        GT_LOG_ERROR("Renderer", "Failed to create render world");
    }

    HMODULE fbxImporter = LoadLibraryA("fbx_importer.dll");
    if (!fbxImporter) {
        GT_LOG_ERROR("Assets", "Failed to load %s", "fbx_importer.dll");
    }
    auto fbx_importer_get_interface = (bool(*)(fbx_importer::FBXImportInterface*))(GetProcAddress(fbxImporter, "fbx_importer_get_interface"));

    GT_LOG_INFO("Application", "Initialized graphics scene");

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


    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    auto RenderViewCallback = [](void* window, ImDrawData* drawData, void* userData) -> void {};

    runtime::UIContextConfig uiContextConfig = {
        &runtime::CreateWindow,
        &runtime::DestroyWindow,
        &runtime::GetWindowSize,
        &runtime::SetWindowSize,
        renderer,
        RenderViewCallback,
        ImGui_ImplDX11_NewFrame,
        []() -> void { ImGui::Render(); }
    };

    runtime::UIContext* uiContext = nullptr;
    runtime::CreateUIContext(&uiContext, &applicationArena, &uiContextConfig);

    entity_system::World* mainWorld;
    entity_system::WorldConfig worldConfig;
    if (!entity_system::CreateWorld(&mainWorld, &applicationArena, &worldConfig)) {
        GT_LOG_ERROR("Entity System", "Failed to create world");
    }

    entity_system::Entity* entityList = GT_NEW_ARRAY(entity_system::Entity, worldConfig.maxNumEntities, &applicationArena);

    core::api_registry::APIRegistry* apiRegistry = nullptr;
    core::api_registry::APIRegistryInterface apiRegistryInterface;

    api_registry_get_interface(&apiRegistryInterface);
    core::api_registry::CreateRegistry(&apiRegistry, &applicationArena);

    entity_system::EntitySystemInterface entitySystem;
    entity_system_get_interface(&entitySystem);

    renderer::RendererInterface rendererInterface;
    renderer_get_interface(&rendererInterface);

    fbx_importer::FBXImportInterface fbxImporterInterface;
    fbx_importer_get_interface(&fbxImporterInterface);
    
    runtime::RuntimeInterface runtimeInterface;
    runtime_get_interface(&runtimeInterface);

    core::api_registry::Add(apiRegistry, ENTITY_SYSTEM_API_NAME, &entitySystem);
    core::api_registry::Add(apiRegistry, RENDERER_API_NAME, &rendererInterface);
    core::api_registry::Add(apiRegistry, FBX_IMPORTER_API_NAME, &fbxImporterInterface);
    core::api_registry::Add(apiRegistry, RUNTIME_API_NAME, &runtimeInterface);

    void(*UpdateModule)(void*, ImGuiContext*, runtime::UIContext*, entity_system::World*, renderer::RenderWorld*, fnd::memory::LinearAllocator*, entity_system::Entity**, size_t*);
    void*(*InitializeModule)(memory::MemoryArenaBase*, core::api_registry::APIRegistry* apiRegistry, core::api_registry::APIRegistryInterface* apiInterface);

    char tempPath[512] = "";
    snprintf(tempPath, 512, "test_module_%s.dll", "temp");

   
    FILETIME testModuleTimestamp;
    HANDLE testModuleWatchHandle;
    testModuleWatchHandle = CreateFileA("test_module.dll", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    assert(testModuleWatchHandle);
    BOOL res = GetFileTime(testModuleWatchHandle, nullptr, nullptr, &testModuleTimestamp);
    assert(res);
    CloseHandle(testModuleWatchHandle);

    GetLastError();
    res = CopyFileA("test_module.dll", tempPath, FALSE);
    if (res == FALSE) {
        GT_LOG_ERROR("DLL Hotloader", "failed to copy %s to %s (error code: %i)", "test_module.dll", tempPath, GetLastError());
    }

    HMODULE testModule = LoadLibraryA(tempPath);
    assert(testModule);
    UpdateModule = (decltype(UpdateModule)) GetProcAddress(testModule, "Update");
    assert(UpdateModule);
    InitializeModule = (decltype(InitializeModule)) GetProcAddress(testModule, "Initialize");

    void* testModuleState = InitializeModule(&applicationArena, apiRegistry, &apiRegistryInterface);

    size_t numEntities = 0;

    static const size_t frameAllocatorSize = GIGABYTES(2);
    memory::LinearAllocator frameAllocator(applicationArena.Allocate(frameAllocatorSize, 16, GT_SOURCE_INFO), frameAllocatorSize);

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

            FILETIME currentTestModuleTimestamp;

            GetLastError();
            testModuleWatchHandle = CreateFileA("test_module.dll", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
            if (testModuleWatchHandle == INVALID_HANDLE_VALUE) {
                GT_LOG_ERROR("Runtime", "Failed to access test_module.dll with error %i", GetLastError());
            }
            GetLastError();
            BOOL res = GetFileTime(testModuleWatchHandle, nullptr, nullptr, &currentTestModuleTimestamp);
            if (res == false) {
                GT_LOG_ERROR("Runtime", "Failed to reload module with error %i", GetLastError());
            }
            //assert(res);
            CloseHandle(testModuleWatchHandle);

            auto comp = CompareFileTime(&testModuleTimestamp, &currentTestModuleTimestamp);
            testModuleTimestamp = currentTestModuleTimestamp;
            //GT_LOG_DEBUG("DLL Hotloader", "comp = %li", comp);
            if (comp < 0) {
                Sleep(500);     // because otherwise windows will still be locking this
                GT_LOG_INFO("DLL Hotloader", "Change detected, reloading %s", "test_module.dll");
                FreeLibrary(testModule);
                UpdateModule = nullptr;

                GetLastError();
                res = CopyFileA("test_module.dll", tempPath, FALSE);
                if (res == FALSE) {
                    GT_LOG_ERROR("DLL Hotloader", "failed to copy %s to %s (error code: %i)", "test_module.dll", tempPath, GetLastError());
                }
                else {
                    GT_LOG_INFO("DLL Hotloader", "reloaded %s", "test_module.dll");
                    testModule = LoadLibraryA(tempPath);
                    assert(testModule);
                    UpdateModule = (decltype(UpdateModule))GetProcAddress(testModule, "Update");
                    assert(UpdateModule);
                }

                testModuleWatchHandle = CreateFileA("test_module.dll", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
                assert(testModuleWatchHandle);
                BOOL res = GetFileTime(testModuleWatchHandle, nullptr, nullptr, &testModuleTimestamp);
                assert(res);
                CloseHandle(testModuleWatchHandle);
            }


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
            //runtime::BeginFrame(uiContext);

            entity_system::Entity* entitySelection = nullptr;
            size_t numEntitiesSelected = 0;

            if (UpdateModule) {
                UpdateModule(testModuleState, ImGui::GetCurrentContext(), uiContext, mainWorld, renderWorld, &frameAllocator, &entitySelection, &numEntitiesSelected);
            }

            entity_system::GetAllEntities(mainWorld, entityList, &numEntities);

            
#ifdef GT_DEVELOPMENT
            if (ImGui::Begin(ICON_FA_FLOPPY_O "  Memory usage", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

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
            
            static math::float3 mousePosScreenCache(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, 15.0f);
            math::float3 mousePosScreen(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, 15.0f);

            /* Basic UI: frame statistics */
            ImGui::SetNextWindowPos(ImVec2(10.0f, ImGui::GetIO().DisplaySize.y - 50));
            ImGui::Begin("#framestatistics", (bool*)0, ImVec2(0, 0), 0.45f, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
            ImGui::Text("Window dimensions = %ix%i", WINDOW_WIDTH, WINDOW_HEIGHT);
            ImGui::Text("Mouse Screen Pos: %f, %f", mousePosScreen.x, mousePosScreen.y);
            ImGui::Text("Simulation time average: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();

            /*static float angle = 0.0f;
            angle += 0.01f;
            entity_system::GetAllEntities(mainWorld, entityList, &numEntities);
            for (size_t i = 0; i < numEntities; ++i) {
                float rotmat[16];
                float tempmap[16];
                util::Make4x4FloatRotationMatrixCMLH(rotmat, math::float3(0.0f, 1.0f, 0.0f), angle);
                util::MultiplyMatricesCM(entity_system::GetEntityTransform(mainWorld, entityList[i]), rotmat, tempmap);
                util::Copy4x4FloatMatrixCM(tempmap, entity_system::GetEntityTransform(mainWorld, entityList[i]));
            }*/

            /* End sim frame */
            //runtime::EndFrame(uiContext);
            ImGui::Render();
            t += dt;
            accumulator -= dt;
        }
        if (didUpdate) {
                
            renderer::WorldSnapshot worldSnapshot;
            
            entity_system::GetAllEntities(mainWorld, entityList, &numEntities);
            worldSnapshot.numTransforms = (uint32_t)numEntities;
            worldSnapshot.transforms = (renderer::Transform*)frameAllocator.Allocate(sizeof(renderer::Transform) * numEntities, alignof(renderer::Transform));
            for (size_t i = 0; i < numEntities; ++i) {
                worldSnapshot.transforms[i].entityID = entityList[i].id;
                util::Copy4x4FloatMatrixCM(entity_system::GetEntityTransform(mainWorld, entityList[i]), worldSnapshot.transforms[i].transform);
            }
            renderer::UpdateWorldState(renderWorld, &worldSnapshot);

            frameAllocator.Reset();
        }

    
        /* Begin render frame*/

        auto renderFrameTimerStart = GetCounter();

        //GT_LOG_INFO("RenderProfile", "Command recording took %f ms", 1000.0 * (GetCounter() - cmdRecordingTimerStart));
        
        // draw geometry
        auto commandSubmissionTimerStart = GetCounter();
      
        // draw UI
        //runtime::RenderViews(uiContext);
        auto uiDrawData = ImGui::GetDrawData();
        if (uiDrawData) {
            renderer::RenderUI(renderer, uiDrawData, swapChain);
        }

        // draw world
        renderer::Render(renderWorld, swapChain);

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

namespace runtime
{
    void SetMainWindowTitle(const char* title)
    {
        if (g_hwnd == nullptr) { return; }
        SetWindowTextA(g_hwnd, title);
    }
}

bool runtime_get_interface(runtime::RuntimeInterface* outInterface)
{
    outInterface->SetMainWindowTitle = &runtime::SetMainWindowTitle;
    outInterface->GetImGuiContextForView = &runtime::GetImGuiContextForView;
    outInterface->BeginView = &runtime::BeginView;
    outInterface->EndView = &runtime::EndView;

    return true;
}
