#pragma once

#include <foundation/int_types.h>
namespace fnd
{
    namespace memory {
        class MemoryArenaBase;
    }
}

#define ENTITY_NAME_SIZE 128
namespace entity_system
{
    static const uint32_t MAX_NUM_ENTITIES = 4096;

    struct World;

    struct WorldConfig
    {
        uint32_t maxNumEntities = MAX_NUM_ENTITIES;
    };

    bool CreateWorld(World** outWorld, fnd::memory::MemoryArenaBase* memoryArena, WorldConfig* config);
    void DestroyWorld(World* world);

    enum { INVALID_ID = 0 };
    typedef struct { uint32_t id = INVALID_ID; } Entity;
    
    Entity CreateEntity(World* world);
    void DestroyEntity(World* world, Entity entity);

    void SetEntityName(World* world, Entity entity, const char* name);
    char* GetEntityNameBuf(World* world, Entity entity);

    float* GetEntityTransform(World* world, Entity entity);
}