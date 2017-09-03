#include <stdio.h>
#include <foundation/logging/logging.h>
#include <malloc.h>

#include <foundation/memory/memory.h>
#include <engine/runtime/entities/entities.h>

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
    
    entity_system::Entity myEntity;
    int nameIndex = 1;
    static const int numNames = 4;
    const char* names[numNames] = { "Samuel", "Dean", "Robert", "Castiel" };
};

extern "C" __declspec(dllexport)
void* Initialize(fnd::memory::MemoryArenaBase* memoryArena)
{
    State* state = (State*)GT_NEW(State, memoryArena);

    return state;
}

extern "C" __declspec(dllexport)
void Execute(void* userData, entity_system::World* world, entity_system::EntitySystemInterface* entitySystem)
{
    using namespace fnd;
    ConsoleLogger consoleLogger;

    auto state = (State*)userData;

    /*
    if (entitySystem->IsEntityAlive(world, state->myEntity)) {
        if (state->nameIndex == 0) {
            entitySystem->DestroyEntity(world, state->myEntity);
            state->myEntity.id = 0;
        }
        else {
            entitySystem->SetEntityName(world, state->myEntity, state->names[state->nameIndex - 1]);
        }
        state->nameIndex = (state->nameIndex + 1) % (state->numNames + 1);
    }
    else {
        state->myEntity = entitySystem->CreateEntity(world);
        entitySystem->SetEntityName(world, state->myEntity, state->names[state->nameIndex - 1]);
        state->nameIndex = (state->nameIndex + 1) % (state->numNames + 1);
    }*/
    
    entity_system::Entity entityList[64];
    size_t numEntities = 0;
    entitySystem->GetAllEntities(world, entityList, &numEntities);
    for (size_t i = 0; i < numEntities; ++i) {
        entitySystem->DestroyEntity(world, entityList[i]);
    }
  
    return;
}