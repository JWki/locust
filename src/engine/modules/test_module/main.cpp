#include <stdio.h>
#include <foundation/logging/logging.h>
#include <malloc.h>

#include <foundation/math/math.h>
#include <foundation/memory/memory.h>
#include <engine/runtime/entities/entities.h>
#include "ImGui/imgui.h"

#include <engine/runtime/core/api_registry.h>

#include <fontawesome/IconsFontAwesome.h>

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
    ImGui::DragFloat3(" " ICON_FA_ARROWS, (float*)matrixTranslation, 0.01f);
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_UNDO "##translate")) { matrixTranslation = { 0.0f, 0.0f, 0.0f }; }
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



class SimpleFilterPolicy
{
public:
    bool Filter(fnd::logging::LogCriteria criteria)
    {
        return true;
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
class PrintfWriter
{
public:
    void Write(const char* msg)
    {
        printf("%s\n", msg);
    }
};

typedef fnd::logging::Logger<SimpleFilterPolicy, SimpleFormatPolicy, PrintfWriter> ConsoleLogger;


struct State {
    core::api_registry::APIRegistry* apiRegistry = nullptr;
    core::api_registry::APIRegistryInterface* apiRegistryInterface = nullptr;

    entity_system::Entity selectedEntity;

    char padding[1024 * 1024 - 
        ((  sizeof(core::api_registry::APIRegistry*) +
            sizeof(core::api_registry::APIRegistryInterface*) +
            sizeof(entity_system::Entity)))];
};
static_assert(sizeof(State) == 1024 * 1024, "");

extern "C" __declspec(dllexport)
void* Initialize(fnd::memory::MemoryArenaBase* memoryArena, core::api_registry::APIRegistry* apiRegistry, core::api_registry::APIRegistryInterface* apiRegistryInterface)
{
    State* state = (State*)GT_NEW(State, memoryArena);
    state->apiRegistry = apiRegistry;
    state->apiRegistryInterface = apiRegistryInterface;
    return state;
}

extern "C" __declspec(dllexport)
void Update(void* userData, ImGuiContext* guiContext, entity_system::World* world, float* camera, float* projection)
{
    using namespace fnd;
    ConsoleLogger consoleLogger;

    auto state = (State*)userData;

    auto entitySystem = (entity_system::EntitySystemInterface*) state->apiRegistryInterface->Get(state->apiRegistry, ENTITY_SYSTEM_API_NAME);
    assert(entitySystem);

    ImGui::SetCurrentContext(guiContext);
    
    ImGuizmo::BeginFrame();

    ImGuiWindowFlags windowFlags = 0;
    if (ImGui::Begin(ICON_FA_DATABASE "  Entity Explorer", nullptr, windowFlags)) {
        entity_system::Entity entityList[64];
        size_t numEntities = 0;

        /* Add / delete of entities */
        
        if (ImGui::Button(ICON_FA_USER_PLUS "  Create New")) {
            state->selectedEntity = entitySystem->CreateEntity(world);
        }
        
        entitySystem->GetAllEntities(world, entityList, &numEntities);

        if (entitySystem->IsEntityAlive(world, state->selectedEntity)) {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_USER_TIMES "  Delete")) {
                entitySystem->DestroyEntity(world, state->selectedEntity);
                int index = -1;
                for (int i = 0; i < numEntities; ++i) {
                    if (entityList[i].id == state->selectedEntity.id) {
                        index = i;
                    }
                }
                if (index > 0) {
                    state->selectedEntity = entityList[index - 1];
                }
                else {
                    if (numEntities > 1) {
                        state->selectedEntity = entityList[1];
                    }
                    else {
                        state->selectedEntity = { entity_system::INVALID_ID };
                    }
                }
            }
        }

        /* List of alive entities */

        entitySystem->GetAllEntities(world, entityList, &numEntities);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        //ImGui::Text("");
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##list", contentSize);
        for (size_t i = 0; i < numEntities; ++i) {
            entity_system::Entity entity = entityList[i];
            const char* name = entitySystem->GetEntityName(world, entity);
            ImGui::PushID(entity.id);
            if (ImGui::Selectable(name, state->selectedEntity.id == entity.id)) {
                state->selectedEntity = entity;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                //camPos = util::Get4x4FloatMatrixColumnCM(entity_system::GetEntityTransform(world, state->selectedEntity), 3).xyz;
            }
            ImGui::SameLine();
            ImGui::Text("(id = %i)", entity.id);
            ImGui::PopID();
        }
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(0)) {
            state->selectedEntity = { entity_system::INVALID_ID };
        }
        ImGui::EndChild();

    } ImGui::End();

    ImGui::Begin(ICON_FA_WRENCH "  Property Editor"); {
        if (state->selectedEntity.id != 0) {

            if (ImGui::TreeNode(ICON_FA_PENCIL "    Object")) {
                if (ImGui::InputText(" " ICON_FA_TAG " Name", entitySystem->GetEntityName(world, state->selectedEntity), ENTITY_NAME_SIZE, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    entitySystem->SetEntityName(world, state->selectedEntity, entitySystem->GetEntityName(world, state->selectedEntity));
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(ICON_FA_LOCATION_ARROW "    Transform")) {
                EditTransform(camera, projection, entitySystem->GetEntityTransform(world, state->selectedEntity));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(ICON_FA_CUBES "    Material")) {
                //ImGui::SliderFloat("Metallic", &object.metallic, 0.0f, 1.0f);
                //ImGui::SliderFloat("Roughness", &object.roughness, 0.0f, 1.0f);

                ImGui::TreePop();
            }
        }

    } ImGui::End();

    return;
}