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


struct EntityNode
{
    EntityNode* next = nullptr;
    EntityNode* prev = nullptr;
    entity_system::Entity ent;

    bool isFree = true;
};

EntityNode* AllocateEntityNode(EntityNode* pool, size_t poolSize)
{
    for (size_t i = 0; i < poolSize; ++i) {
        if (pool[i].isFree) {
            pool[i].isFree = false;
            pool[i].ent = { entity_system::INVALID_ID };
            return &pool[i];
        }
    }
    return nullptr;
}

struct EntityNodeList
{
    EntityNode* head = nullptr;
};

void FreeEntityNode(EntityNode* node)
{
    node->isFree = true;
    node->ent = { entity_system::INVALID_ID };
    node->next = node->prev = nullptr;
}

void ClearList(EntityNodeList* list)
{
    EntityNode* it = list->head;
    while (it != nullptr) {
        EntityNode* node = it;
        it = it->next;
        FreeEntityNode(node);
    }
}

void AddToList(EntityNodeList* list, EntityNode* node)
{
    if (node == nullptr) { return; }
    EntityNode* it = list->head;
    if (it == nullptr) {
        list->head = node;
        node->prev = nullptr;
        node->next = nullptr;
        return;
    }
    while (it->next != nullptr) {
        if (it->ent.id == node->ent.id || node == it) {
            assert(false);
        }
        it = it->next;
    }
    it->next = node;
    node->prev = it;
    node->next = nullptr;
}

void RemoveFromList(EntityNodeList* list, EntityNode* node)
{
    if (node == nullptr) { return; }
    if (node->next != nullptr) {
        node->next->prev = node->prev;
    }
    if (node->prev != nullptr) {
        node->prev->next = node->next;
    }
    if (node == list->head) { list->head = node->next; }
    node->prev = node->next = nullptr;
}

bool IsEntityInList(EntityNodeList* list, entity_system::Entity ent, EntityNode** outNode = nullptr)
{
    EntityNode* it = list->head;
    while (it != nullptr) {
        if (it->ent.id == ent.id) { 
            if (outNode != nullptr) {
                *outNode = it;
            }
            return true; 
        }
        it = it->next;
    }
    return false;
}

#define ENTITY_NODE_POOL_SIZE 512
struct State {
    core::api_registry::APIRegistry* apiRegistry = nullptr;
    core::api_registry::APIRegistryInterface* apiRegistryInterface = nullptr;

    EntityNodeList entitySelection;
    entity_system::Entity lastSelected;

    EntityNode entityNodePool[ENTITY_NODE_POOL_SIZE];

    char padding[1024 * 1024 - 
        ((  sizeof(core::api_registry::APIRegistry*) +
            sizeof(core::api_registry::APIRegistryInterface*) +
            sizeof(EntityNodeList) +
            sizeof(entity_system::Entity) * 2 +     // @NOTE because padding between member fields (members are padded to 64 bit in this struct)
            sizeof(EntityNode) * ENTITY_NODE_POOL_SIZE))];
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
            entity_system::Entity ent = entitySystem->CreateEntity(world);
            if (!ImGui::GetIO().KeyCtrl) {
                ClearList(&state->entitySelection);
            }
            EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
            selectionNode->ent = ent;
            AddToList(&state->entitySelection, selectionNode);
        }
        
        entitySystem->GetAllEntities(world, entityList, &numEntities);

        if (state->entitySelection.head != nullptr && entitySystem->IsEntityAlive(world, state->entitySelection.head->ent)) {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_USER_TIMES "  Delete")) {
                EntityNode* it = state->entitySelection.head;
                while (it) {
                    entitySystem->DestroyEntity(world, it->ent);
                    it = it->next;
                }
                ClearList(&state->entitySelection);
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
            
            auto GetIndex = [](entity_system::Entity entity, entity_system::Entity* entities, size_t numEntities) -> int {
                int index = -1;
                for (size_t i = 0; i < numEntities; ++i) {
                    if (entity.id == entities[i].id) {
                        index = (int)i;
                        break;
                    }
                }
                return index;
            };

            int lastSelectedIndex = GetIndex(state->lastSelected, entityList, numEntities);
            ImGui::PushStyleColor(ImGuiCol_Header, lastSelectedIndex == (int)i ? ImVec4(0.2f, 0.4f, 1.0f, 1.0f) : ImGui::GetStyle().Colors[ImGuiCol_Header]);
            if (ImGui::Selectable(name, IsEntityInList(&state->entitySelection, entity))) {
                ImGui::PopStyleColor();

                if (ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl) {  // multiselection
                    if(ImGui::GetIO().KeyShift) {
                        
                        int clickedIndex = (int)i;
                        if (clickedIndex > lastSelectedIndex) {
                            for (int i = lastSelectedIndex; i <= clickedIndex; ++i) {
                                if (!IsEntityInList(&state->entitySelection, entityList[i])) {
                                    EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
                                    selectionNode->ent = entityList[i];
                                    AddToList(&state->entitySelection, selectionNode);
                                }
                            }
                        }
                        else {
                            if (clickedIndex < lastSelectedIndex) {
                                for (int i = lastSelectedIndex; i >= clickedIndex; --i) {
                                    if (!IsEntityInList(&state->entitySelection, entityList[i])) {
                                        EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
                                        selectionNode->ent = entityList[i];
                                        AddToList(&state->entitySelection, selectionNode);
                                    }
                                }
                            }
                            else {
                                // @TODO what to do in this case?
                                GT_LOG_WARNING("Editor", "Meh.");
                            }
                        }
                    }
                    else {
                        EntityNode* node = nullptr;
                        if (!IsEntityInList(&state->entitySelection, entity, &node)) {  // ADD
                            EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
                            selectionNode->ent = entity;
                            AddToList(&state->entitySelection, selectionNode);
                        }
                        else {  // REMOVE
                            RemoveFromList(&state->entitySelection, node);
                            FreeEntityNode(node);
                        }
                    }
                }
                else {  // SET selection
                    ClearList(&state->entitySelection);
                    EntityNode* selectionNode = AllocateEntityNode(state->entityNodePool, ENTITY_NODE_POOL_SIZE);
                    selectionNode->ent = entity;
                    AddToList(&state->entitySelection, selectionNode);
                }

                if (!ImGui::GetIO().KeyCtrl) {
                    state->lastSelected = entity;
                }
            }
            else {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                //camPos = util::Get4x4FloatMatrixColumnCM(entity_system::GetEntityTransform(world, state->selectedEntity), 3).xyz;
            }
            ImGui::SameLine();
            ImGui::Text("(id = %i)", entity.id);
            ImGui::PopID();
        }
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(0)) {
            ClearList(&state->entitySelection);
        }
        ImGui::EndChild();

    } ImGui::End();

    ImGui::Begin(ICON_FA_WRENCH "  Property Editor"); {
        static entity_system::Entity selectedEntity = { entity_system::INVALID_ID };
        if (!IsEntityInList(&state->entitySelection, selectedEntity)) {
            selectedEntity = { entity_system::INVALID_ID };
        }
        EntityNode* it = state->entitySelection.head;
        if (selectedEntity.id == entity_system::INVALID_ID && it != nullptr) {
            selectedEntity = it->ent;
        }
        while (it != nullptr && it->ent.id != 0) {

            ImGuiWindowFlags flags = ImGuiWindowFlags_HorizontalScrollbar;

            ImVec2 contentRegion = ImGui::GetContentRegionAvail();

            ImGui::BeginChild("##tabs", ImVec2(contentRegion.x, 50), false, flags);
            ImGui::PushID(it->ent.id);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, selectedEntity.id != it->ent.id ? ImGui::GetStyle().Alpha * 0.4f : ImGui::GetStyle().Alpha);
            if (ImGui::Button(entitySystem->GetEntityName(world, it->ent))) {
                selectedEntity = it->ent;
            }
            ImGui::PopStyleVar();
            ImGui::SameLine();
            ImGui::PopID();
            ImGui::EndChild();

            it = it->next;
        }

        ImVec2 contentRegion = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##properties", contentRegion);
        if (selectedEntity.id != entity_system::INVALID_ID) {
            if (ImGui::TreeNode(ICON_FA_PENCIL "    Object")) {
                if (ImGui::InputText(" " ICON_FA_TAG " Name", entitySystem->GetEntityName(world, selectedEntity), ENTITY_NAME_SIZE, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    //entitySystem->SetEntityName(world, selectedEntity, entitySystem->GetEntityName(world, selectedEntity));
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(ICON_FA_LOCATION_ARROW "    Transform")) {
                EditTransform(camera, projection, entitySystem->GetEntityTransform(world, selectedEntity));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode(ICON_FA_CUBES "    Material")) {
                //ImGui::SliderFloat("Metallic", &object.metallic, 0.0f, 1.0f);
                //ImGui::SliderFloat("Roughness", &object.roughness, 0.0f, 1.0f);

                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
    } ImGui::End();

    return;
}