#include <stdio.h>
#include <foundation/logging/logging.h>
#include <malloc.h>

#include <foundation/memory/memory.h>
#include <engine/runtime/entities/entities.h>
#include "ImGui/imgui.h"

#include <engine/runtime/core/api_registry.h>

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
void Update(void* userData, ImGuiContext* guiContext, entity_system::World* world)
{
    using namespace fnd;
    ConsoleLogger consoleLogger;

    auto state = (State*)userData;

    auto entitySystem = (entity_system::EntitySystemInterface*) state->apiRegistryInterface->Get(state->apiRegistry, ENTITY_SYSTEM_API_NAME);
    assert(entitySystem);

    ImGui::SetCurrentContext(guiContext);
    
    ImGuiWindowFlags windowFlags = 0;
    if (ImGui::Begin("test_module", nullptr, windowFlags)) {
        entity_system::Entity entityList[64];
        size_t numEntities = 0;

        if (ImGui::Button("Create New")) {
            entitySystem->CreateEntity(world);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            entitySystem->DestroyEntity(world, state->selectedEntity);
            for (int i = 0; i < numEntities; ++i) {
                if (state->selectedEntity.id == entityList[i].id) {
                    if (i > 0) {
                        state->selectedEntity = entityList[i - 1];
                    }
                    else {
                        state->selectedEntity = { entity_system::INVALID_ID };
                    }
                }
            }
        }
        entitySystem->GetAllEntities(world, entityList, &numEntities);

        for (size_t i = 0; i < numEntities; ++i) {
            entity_system::Entity entity = entityList[i];
            const char* name = entitySystem->GetEntityName(world, entity);
            ImGui::PushID(entity.id);
            if (ImGui::Selectable(name, state->selectedEntity.id == entity.id)) {
                state->selectedEntity = entity;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                //camPos = util::Get4x4FloatMatrixColumnCM(entity_system::GetEntityTransform(mainWorld, selectedEntity), 3).xyz;
            }
            ImGui::SameLine();
            ImGui::Text("(id = %i)", entity.id);
            ImGui::PopID();
        }


    } ImGui::End();
  
    return;
}