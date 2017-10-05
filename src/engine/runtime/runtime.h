#pragma once

#include <foundation/int_types.h>

#define RUNTIME_API_NAME "runtime"

namespace fnd {
    namespace memory {
        class MemoryArenaBase;
    }
}

struct ImGuiContext;
struct ImDrawData;

#undef CreateWindow    // @NOTE win32 bs I don't even

namespace runtime
{
    void SetMainWindowTitle(const char* title);
    
    void SetWindowTitle(void* window, const char* title);
    void* CreateWindow();
    void DestroyWindow(void* window);
    void GetWindowSize(void* window, int* w, int* h);
    void SetWindowSize(void* window, int w, int h);
        
    struct UIContext;
    struct UIContextConfig
    {
        decltype(CreateWindow)*  CreateWindowCallback;
        decltype(DestroyWindow)* DestroyWindowCallback;
        decltype(GetWindowSize)* GetWindowSizeCallback;
        decltype(SetWindowSize)* SetWindowSizeCallback;
    
        void* rendererUserData = nullptr;
        void (*RenderViewCallback) (void* window, ImDrawData* drawData, void* userData);
        void(*BeginFrameViewCallback) ();
        void(*EndFrameViewCallback) ();
    };

    bool CreateUIContext(UIContext** outCtx, fnd::memory::MemoryArenaBase* memoryArena, UIContextConfig* config);

    ImGuiContext* GetImGuiContextForView(UIContext* ctx, const char* name);
    bool BeginView(UIContext* ctx, const char* name);
    void EndView(UIContext* ctx);

    void BeginFrame(UIContext* ctx);
    void EndFrame(UIContext* ctx);
    void RenderViews(UIContext* ctx);

    struct RuntimeInterface
    {
        decltype(SetMainWindowTitle)*       SetMainWindowTitle = nullptr;
        decltype(GetImGuiContextForView)*   GetImGuiContextForView = nullptr;
        decltype(BeginView)*                BeginView = nullptr;
        decltype(EndView)*                  EndView = nullptr;
    };
}


extern "C"
{
    __declspec(dllexport)
    bool runtime_get_interface(runtime::RuntimeInterface* outInterface);
}