
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <runtime/ImGui/imgui.h>
#include <runtime/win32/imgui_impl_dx11.h>

#include <tchar.h>
#include <stdio.h>
#include <malloc.h>

#include <core_lib/int_types.h>
#include <core_lib/memory/memory.h>
#include <core_lib/memory/allocators.h>

#include <core_lib/math/math.h>

#pragma warning(push)
#pragma warning(disable : 4067)
#include <runtime/enet/enet.h>
#pragma warning(pop)

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#define IS_POW_OF_TWO(n) ((n & (n - 1)) == 0)

class SimpleMemoryTracker
{
    size_t m_usedMemory = 0;
public:
    ~SimpleMemoryTracker()
    {
        if (m_usedMemory > 0) {
            printf("NOOOO\n");
        }
    }

    LC_FORCE_INLINE void TrackAllocation(void* memory, size_t size, size_t alignemnt, lc::SourceInfo scInfo) 
    {
        m_usedMemory += size;
    }

    LC_FORCE_INLINE void UntrackAllocation(void* memory, size_t size) 
    {
        m_usedMemory -= size;
    }

    inline size_t GetUsedMemorySize()
    {
        return m_usedMemory;
    }
};

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
public:
    
    ~ExtendedMemoryTracker()
    {
        for (int i = 0; i < m_numAllocations; ++i) {
            char buf[512] = "";
            if (m_allocations[i].ptr != nullptr) {
                auto& info = m_allocations[i];
                snprintf(buf, 512, "%s(%lli): this allocation leaks\n", info.info.file, info.info.line);
                OutputDebugStringA(buf);
                printf("%lli bytes leaked @ %lli from %s(%lli)\n", info.size, (uintptr_t)info.ptr, info.info.file, info.info.line);
            }
        }
    }

    inline void SetArena(lc::memory::MemoryArenaBase* arena)
    {
        m_arena = arena;
    }
    
    LC_FORCE_INLINE void TrackAllocation(void* memory, size_t size, size_t alignment, lc::SourceInfo scInfo)
    {
        if (!m_allocations || m_numAllocations == m_capacity) {
            if (!m_arena) { return; }
            if (m_allocations) {
                LC_DELETE_ARRAY(m_allocations, m_arena);
            }
            using Type = AllocInfo;
            auto arenaAsPtr = m_arena;
            auto count = m_capacity + 1024;
            m_allocations = LC_NEW_ARRAY(AllocInfo, m_capacity + 1024, m_arena);
        }
        AllocInfo info;
        info.alignment = alignment;
        info.size = size;
        info.ptr = memory;
        info.info = scInfo;
        m_allocations[m_numAllocations++] = info;
    }

    LC_FORCE_INLINE void UntrackAllocation(void* memory, size_t size)
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


#ifdef LC_DEVELOPMENT
typedef lc::memory::SimpleTrackingArena<lc::memory::TLSFAllocator, ExtendedMemoryTracker> HeapArena;
typedef lc::memory::SimpleTrackingArena<lc::memory::LinearAllocator, ExtendedMemoryTracker> LinearArena;
#else
typedef lc::memory::SimpleMemoryArena<lc::memory::TLSFAllocator>    HeapArena;
typedef lc::memory::SimpleMemoryArena<lc::memory::LinearAllocator>  LinearArena;
#endif

#define GIGABYTES(n) (MEGABYTES(n) * (size_t)1024)
#define MEGABYTES(n) (KILOBYTES(n) * 1024)
#define KILOBYTES(n) (n * 1024)

static_assert(GIGABYTES (8) > MEGABYTES(4), "some size type is wrong");

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

static ID3D11Device*            g_pd3dDevice = NULL;
static ID3D11DeviceContext*     g_pd3dDeviceContext = NULL;
static IDXGISwapChain*          g_pSwapChain = NULL;
static ID3D11RenderTargetView*  g_mainRenderTargetView = NULL;


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
        printf("Fuck\n");
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



int main(int argc, char* argv[])
{
    using namespace lc;
   
#ifdef LC_DEVELOPMENT
    const size_t debugHeapSize = MEGABYTES(500);
    memory::TLSFAllocator debugAllocator(malloc(debugHeapSize), debugHeapSize);
    HeapArena debugArena(&debugAllocator);
#endif

    const size_t reservedMemorySize = GIGABYTES(2);
    void* reservedMemory = malloc(reservedMemorySize);

    memory::LinearAllocator applicationAllocator(reservedMemory, reservedMemorySize);
    LinearArena applicationArena(&applicationAllocator);
#ifdef LC_DEVELOPMENT
    applicationArena.GetTrackingPolicy()->SetArena(&debugArena);
#endif

    static const size_t sandboxedHeapSize = MEGABYTES(500);     // 0.5 gigs of memory for free form allocations @TODO subdivide further for individual 3rd party libs etc
    void* sandboxedHeap = applicationArena.Allocate(sandboxedHeapSize, 4, LC_SOURCE_INFO);

    memory::TLSFAllocator sandboxAllocator(sandboxedHeap, sandboxedHeapSize);
    HeapArena sandboxArena(&sandboxAllocator);
#ifdef LC_DEVELOPMENT
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
        return arena->Allocate(size, 4, LC_SOURCE_INFO);
    };
    ImGui::GetIO().MemFreeFn = [](void* ptr) -> void {
        auto arena = static_cast<HeapArena*>(ImGui::GetIO().UserData);
        arena->Free(ptr);
    };

    ImGui_ImplDX11_Init(hwnd, g_pd3dDevice, g_pd3dDeviceContext);

    // Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // networking!

    ENetCallbacks inits;
    memset(&inits, 0x0, sizeof(ENetCallbacks));
    inits.malloc = [](size_t size) -> void* {   
        // @TODO: this probably shouldn't access the HeapArena via ImGui::GetIO()
        auto arena = static_cast<HeapArena*>(ImGui::GetIO().UserData);
        return arena->Allocate(size, 4, LC_SOURCE_INFO);
    };
    inits.free = [](void* ptr) -> void {
        // @TODO: this probably shouldn't access the HeapArena via ImGui::GetIO()
        auto arena = static_cast<HeapArena*>(ImGui::GetIO().UserData);
        arena->Free(ptr);
    };
    enet_initialize_with_callbacks(ENET_VERSION, &inits);


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

#ifdef LC_DEVELOPMENT
            if (ImGui::Begin("Memory usage")) {

                ImDrawList* drawList = ImGui::GetWindowDrawList();

                float totalWidth = ImGui::GetContentRegionAvailWidth();
                
                float totalMemory = debugHeapSize + reservedMemorySize;

                auto blue = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 1, 1));
                ImVec2 memRectA = ImGui::GetWindowContentRegionMin();
                ImVec2 memRectB(memRectA.x + totalWidth, memRectA.y + 400);

                float appPortion = reservedMemorySize / (float)totalMemory;

                ImVec2 appRectA = memRectA;
                float appWidth = appPortion * totalWidth;
                ImVec2 appRectB(appRectA.x + appWidth, memRectB.y);

                auto appCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 0, 0, 1));

                float sandboxPortion = sandboxedHeapSize / (float)reservedMemorySize;
                ImVec2 sbRectA = appRectA;
                float sbWidth = sandboxPortion * appWidth;
                ImVec2 sbRectB(sbRectA.x + sbWidth, appRectB.y);
                auto sbCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 0, 1));

                drawList->AddRectFilled(memRectA, memRectB, blue);
                
                drawList->AddRectFilled(appRectA, appRectB, appCol);
                drawList->AddRectFilled(sbRectA, sbRectB, sbCol);

                if (ImGui::TreeNode("Application arena")) {
                    ImGui::Text("%lli kB allocated", applicationArena.GetTrackingPolicy()->GetUsedMemorySize() / 1024);
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("Sandbox arena")) {
                    ImGui::Text("%lli kB allocated", sandboxArena.GetTrackingPolicy()->GetUsedMemorySize() / 1024);
                    ImGui::TreePop();
                }

                


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

                {
                    static float4 a;
                    static float4 b;
                    static float4 c;
                    ImGui::DragFloat4("a", static_cast<float*>(a));
                    ImGui::DragFloat4("b", static_cast<float*>(b));
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

        // draw UI
        auto uiDrawData = ImGui::GetDrawData();
        if (uiDrawData) {
            ImGui_ImplDX11_RenderDrawLists(uiDrawData);
        }

        /* Present render frame*/
        g_pSwapChain->Present(0, 0);

    } while (!exitFlag);

    ImGui_ImplDX11_Shutdown();
    enet_deinitialize();

    return 0;
}

