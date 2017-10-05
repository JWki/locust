#include "runtime.h"

#include <engine/runtime/ImGui/imgui.h>
#include <engine/runtime/ImGui/imgui_internal.h>

#include <foundation/memory/memory.h>

namespace runtime
{
    struct UIView
    {
        ImGuiID id = 0;
        ImGuiContext* context = nullptr;
        void* window = nullptr;
    };

    struct UIContext
    {
        fnd::memory::MemoryArenaBase* memoryArena = nullptr;
    
        UIContextConfig* config = nullptr;

        size_t maxNumViews = 64;
        size_t numViews = 0;
        UIView* views = nullptr;
    
        UIView* FindView(ImGuiID id)
        {
            for (size_t i = 0; i < numViews; ++i) {
                if (views[i].id == id) {
                    return &views[i];
                }
            }
            return nullptr;
        }

        UIView* CreateView(ImGuiID id)
        {
            if (numViews == maxNumViews) {
                maxNumViews = maxNumViews * 2;
                UIView* newViews = GT_NEW_ARRAY(UIView, maxNumViews, memoryArena);
                memcpy(newViews, views, sizeof(UIView) * numViews);
                GT_DELETE_ARRAY(views, memoryArena);
                views = newViews;
            }
            size_t index = numViews++;
            views[index] = UIView();
            views[index].id = id;
            views[index].context = ImGui::GetCurrentContext();

            views[index].window = config->CreateWindowCallback();
            config->SetWindowSizeCallback(views[index].window, 400, 200);

            return &views[index];
        }
    };


    bool CreateUIContext(UIContext** outCtx, fnd::memory::MemoryArenaBase* memoryArena, UIContextConfig* config)
    {
        UIContext* context = GT_NEW(UIContext, memoryArena);
        context->memoryArena = memoryArena;

        context->views = GT_NEW_ARRAY(UIView, context->maxNumViews, memoryArena);
        context->config = config;

        *outCtx = context;
        return true;
    }

    ImGuiContext* GetImGuiContextForView(UIContext* ctx, const char* name)
    {
        auto id = ImHash(name, 0);
        auto view = ctx->FindView(id);
        if (view != nullptr) {
            return view->context;
        }
        else {
            view = ctx->CreateView(id);
            return view->context;
        }
    }

    bool BeginView(UIContext* ctx, const char* name)
    {
        auto id = ImHash(name, 0);
        auto view = ctx->FindView(id);
        if (view == nullptr) {
            view = ctx->CreateView(id);
        }
        if (view == nullptr || view->context == nullptr) { return false; }

        ImGui::SetCurrentContext(view->context);

        ImGui::Begin(name);

        return true;
    }

    void EndView(UIContext* ctx)
    {
        ImGui::End();
    }


    void BeginFrame(UIContext* ctx)
    {
        for (size_t i = 0; i < ctx->numViews; ++i) {
            ImGui::SetCurrentContext(ctx->views[i].context);
            ctx->config->BeginFrameViewCallback();
        }
    }

    void EndFrame(UIContext* ctx)
    {
        for (size_t i = 0; i < ctx->numViews; ++i) {
            ImGui::SetCurrentContext(ctx->views[i].context);
            ctx->config->EndFrameViewCallback();
        }
    }

    void RenderViews(UIContext* ctx)
    {
        for (size_t i = 0; i < ctx->numViews; ++i) {
            ImGui::SetCurrentContext(ctx->views[i].context);
            auto uiDrawData = ImGui::GetDrawData();
            if (uiDrawData) {
                ctx->config->RenderViewCallback(ctx->views[i].window, uiDrawData, ctx->config->rendererUserData);
            }
        }
    }
}