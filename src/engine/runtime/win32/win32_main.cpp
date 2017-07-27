
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

    GT_FORCE_INLINE void TrackAllocation(void* memory, size_t size, size_t alignemnt, lc::SourceInfo scInfo)
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
        lc::SourceInfo info;
    };
    AllocInfo* m_allocations = nullptr;
    size_t m_numAllocations = 0;
    size_t m_capacity = 0;
    lc::memory::MemoryArenaBase* m_arena = nullptr;

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

    inline void SetArena(lc::memory::MemoryArenaBase* arena)
    {
        m_arena = arena;
    }

    inline void SetName(const char* name) { m_name = name; }
    inline const char* GetName() { return m_name; }

    inline ExtendedMemoryTracker* GetNext() { return m_next; }

    GT_FORCE_INLINE void TrackAllocation(void* memory, size_t size, size_t alignment, lc::SourceInfo scInfo)
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
typedef lc::memory::SimpleTrackingArena<lc::memory::TLSFAllocator, ExtendedMemoryTracker> HeapArena;
typedef lc::memory::SimpleTrackingArena<lc::memory::LinearAllocator, ExtendedMemoryTracker> LinearArena;
#else
typedef lc::memory::SimpleMemoryArena<lc::memory::TLSFAllocator>    HeapArena;
typedef lc::memory::SimpleMemoryArena<lc::memory::LinearAllocator>  LinearArena;
#endif

class SimpleFilterPolicy
{
public:
    bool Filter(lc::logging::LogCriteria criteria)
    {
        return true;
    }
};

class SimpleFormatPolicy
{
public:
    void Format(char* buf, size_t bufSize, lc::logging::LogCriteria criteria, const char* format, va_list args)
    {
        size_t offset = snprintf(buf, bufSize, "[%s]    ", criteria.channel.str);
        vsnprintf(buf + offset, bufSize - offset, format, args);
    }
};


class IDEConsoleFormatter
{
public:
    void Format(char* buf, size_t bufSize, lc::logging::LogCriteria criteria, const char* format, va_list args)
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

class PrintfWriter
{
public:
    void Write(const char* msg)
    {
        printf("%s\n", msg);
    }
};

typedef lc::logging::Logger<SimpleFilterPolicy, SimpleFormatPolicy, PrintfWriter> SimpleLogger;
typedef lc::logging::Logger<SimpleFilterPolicy, IDEConsoleFormatter, IDEConsoleWriter> IDEConsoleLogger;

#define GIGABYTES(n) (MEGABYTES(n) * (size_t)1024)
#define MEGABYTES(n) (KILOBYTES(n) * 1024)
#define KILOBYTES(n) (n * 1024)

static_assert(GIGABYTES(8) > MEGABYTES(4), "some size type is wrong");

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

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
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
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
void PrintVector(const lc::math::Vector<TElement, ELEMENT_COUNT>& vec)
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


void* LoadFileContents(const char* path, lc::memory::MemoryArenaBase* memoryArena, size_t* fileSize = nullptr)
{
    HANDLE handle = CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (!handle) {
        printf("Failed to load %s\n", path);
        return nullptr;
    }
    DWORD size = GetFileSize(handle, NULL);
    void* buffer = memoryArena->Allocate(size, 16, GT_SOURCE_INFO);
    DWORD bytesRead = 0;
    auto res = ReadFile(handle, buffer, size, &bytesRead, NULL);
    if (res == FALSE || bytesRead != size) {
        printf("Failed to read %s\n", path);
        memoryArena->Free(buffer);
        return nullptr;
    }
    if (fileSize) { *fileSize = bytesRead; }
    CloseHandle(handle);
    return buffer;
}

#include <foundation/sockets/sockets.h>

extern "C" __declspec(dllexport) int win32_main(int argc, char* argv[])
{
  
    fnd::sockets::InitializeSocketLayer();

    fnd::sockets::Address addr(127, 0, 0, 1, 8080);

    fnd::sockets::UDPSocket socket;

   
    using namespace lc;

    SimpleLogger logger;
    IDEConsoleLogger ideLogger;

    GT_LOG_INFO("Generic", "Some message %f %d %s", 3.141f, 42, "Hello World");

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

    bool exitFlag = false;
    bool restartFlag = false;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, _T("void"), NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, _T("void"), _T("void"), WS_OVERLAPPEDWINDOW, 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, wc.hInstance, NULL);

    if (!hwnd) {
        fprintf(stderr, "failed to create a window\n");
        return 1;
    }

    const float bgColor[] = { 100.0f / 255.0f, 149.0f / 255.0f, 237.0f / 255.0f };

    // Initialize Direct3D
    if (CreateDeviceD3D(hwnd) < 0)
    {
        CleanupDeviceD3D();
        UnregisterClass(_T("void"), wc.hInstance);
        return 1;
    }

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

        bufferDesc.Usage = D3D11_USAGE::D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_VERTEX_BUFFER;
        bufferDesc.ByteWidth = sizeof(Vertex) * 3;
        //bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA triangleVertexData = { triangleVertices , 0, 0 };

        auto result = g_pd3dDevice->CreateBuffer(&bufferDesc, &triangleVertexData, &vBuffer);
        if (result != S_OK) {
            printf("failed to create vertex buffer\n");
        }
    }

    ID3D11Buffer* iBuffer = nullptr;
    {
        D3D11_BUFFER_DESC bufferDesc = {};
        ZeroMemory(&bufferDesc, sizeof(bufferDesc));

        bufferDesc.Usage = D3D11_USAGE::D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_INDEX_BUFFER;
        bufferDesc.ByteWidth = sizeof(uint16_t) * 3;
        //bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA triangleIndexData = { triangleIndices , 0, 0 };

        auto result = g_pd3dDevice->CreateBuffer(&bufferDesc, &triangleIndexData, &iBuffer);
        if (result != S_OK) {
            printf("failed to create index buffer\n");
        }
    }

    ID3D11VertexShader* vShader;
    ID3D11PixelShader* fShader;

    size_t vShaderCodeSize = 0;
    char* vShaderCode = static_cast<char*>(LoadFileContents("VertexShader.cso", &applicationArena, &vShaderCodeSize));
    if (!vShaderCode) {
        printf("Failed to load vertex shader\n");
    }

    size_t fShaderCodeSize = 0;
    char* fShaderCode = static_cast<char*>(LoadFileContents("PixelShader.cso", &applicationArena, &fShaderCodeSize));
    if (!fShaderCode) {
        printf("Failed to load pixel shader\n");
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
        printf("failed to create input layout\n");
    }

    ///
    StartCounter();

    double t = 0.0;
    double dt = 1.0 / 60.0;

    double currentTime = GetCounter();
    double accumulator = 0.0;

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

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
                    printf("Received msg from %d.%d.%d.%d:%d:\n", sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort());
                    printf("%s\n", buffer);
                }
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
                if (!socket.IsOpen(&port)) {
                    if (ImGui::Button("Start server")) {
                        if (!socket.Open(8080)) {
                            printf("failed to open socket as server\n");
                        }
                    } ImGui::SameLine();
                    if (ImGui::Button("Try to connect to server")) {
                        if (!socket.Open(0)) {
                            printf("failed to open socket as client\n");
                        }
                    }
                }
                else {
                    bool isServer = port == 8080;
                    ImGui::Text("Listening as %s", isServer ? "Server" : "Client");
                    if (ImGui::Button("Disconnect")) {
                        socket.Close();
                    } ImGui::SameLine();
                    if (ImGui::Button("Send Hello World")) {
                        socket.Send(&addr, "Hello World", strlen("Hello World"));
                    }
                }
            } ImGui::End();


            /* Basic UI: frame statistics */
            ImGui::SetNextWindowPos(ImVec2(10.0f, ImGui::GetIO().DisplaySize.y - 50));
            ImGui::Begin("#framestatistics", (bool*)0, ImVec2(0, 0), 0.45f, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
            ImGui::Text("Simulation time average: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();

            /* End sim frame */
            ImGui::Render();
            t += dt;
            accumulator -= dt;
        }

        /* Begin render frame*/
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, bgColor);

        // draw geometry

        D3D11_VIEWPORT viewport = { 0 };

        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = WINDOW_WIDTH;
        viewport.Height = WINDOW_HEIGHT;

        g_pd3dDeviceContext->RSSetViewports(1, &viewport);

        g_pd3dDeviceContext->IASetInputLayout(inputLayout);
        g_pd3dDeviceContext->VSSetShader(vShader, nullptr, 0);
        g_pd3dDeviceContext->PSSetShader(fShader, nullptr, 0);

        ID3D11Buffer* vBuffers[] = { vBuffer };
        UINT strides[] = { sizeof(Vertex) };
        UINT offsets[] = { 0 };


        g_pd3dDeviceContext->IASetVertexBuffers(0, 1, vBuffers, strides, offsets);
        g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_pd3dDeviceContext->IASetIndexBuffer(iBuffer, DXGI_FORMAT::DXGI_FORMAT_R16_UINT, 0);
        g_pd3dDeviceContext->DrawIndexed(3, 0, 0);

        // draw UI
        auto uiDrawData = ImGui::GetDrawData();
        if (uiDrawData) {
            ImGui_ImplDX11_RenderDrawLists(uiDrawData);
        }

        /* Present render frame*/
        g_pSwapChain->Present(0, 0);

    } while (!exitFlag);

    ImGui_ImplDX11_Shutdown();

    fnd::sockets::ShutdownSocketLayer();


    return 0;
}