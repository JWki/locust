// ImGui Win32 + DirectX11 binding
// In this binding, ImTextureID is used to store a 'ID3D11ShaderResourceView*' texture identifier. Read the FAQ about ImTextureID in imgui.cpp.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you use this binding you'll need to call 4 functions: ImGui_ImplXXXX_Init(), ImGui_ImplXXXX_NewFrame(), ImGui::Render() and ImGui_ImplXXXX_Shutdown().
// If you are new to ImGui, see examples/README.txt and documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

#include <engine/runtime/gfx/gfx.h>
#define IMGUI_API

IMGUI_API bool          ImGui_ImplDX11_Init(void* hwnd, gfx::Device* device);
IMGUI_API void          ImGui_ImplDX11_Shutdown();
IMGUI_API void          ImGui_ImplDX11_NewFrame();

// Use if you want to reset your rendering device without losing ImGui state.
IMGUI_API void          ImGui_ImplDX11_InvalidateDeviceObjects();
IMGUI_API bool          ImGui_ImplDX11_CreateDeviceObjects();

IMGUI_API void          ImGui_ImplDX11_RenderDrawLists(ImDrawData* draw_data, gfx::CommandBuffer* commandBuffer);

// Handler for Win32 messages, update mouse/keyboard data.
// You may or not need this for your implementation, but it can serve as reference for handling inputs.
// Commented out to avoid dragging dependencies on <windows.h> types. You can copy the extern declaration in your code.
/*
IMGUI_API LRESULT   ImGui_ImplDX11_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
*/