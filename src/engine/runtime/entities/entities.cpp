#include "entities.h"
#include <foundation/memory/memory.h>
#include <cassert>

#define HANDLE_INDEX(handle)        (uint16_t)(handle)
#define HANDLE_GENERATION(handle)   (uint16_t)(handle >> 16)

#define HANDLE_GENERATION_START 1

#define MAKE_HANDLE(index, generation) (uint32_t)(((uint32_t)generation) << 16 | index); 


namespace entity_system
{
    template <class TResource>
    struct ResourcePool
    {
        uint32_t    size = 0;
        uint32_t    numElements = 0;
        TResource*  buffer = nullptr;
        uint16_t*   indexList = nullptr;
        uint32_t    indexListHead = 0;
        uint32_t    indexListTail = 0;

        void Initialize(uint32_t bufferSize, fnd::memory::MemoryArenaBase* memoryArena)
        {
            numElements = 0;
            size = bufferSize;
            buffer = GT_NEW_ARRAY(TResource, size, memoryArena);
            indexList = GT_NEW_ARRAY(uint16_t, size, memoryArena);
            indexListHead = indexListTail = 0;
            for (uint32_t i = 0; i < size; ++i) {
                indexList[i] = i;
            }
            indexListTail = size - 1;
        }

        bool GetNextIndex(uint16_t* outIndex)
        {
            if (indexListHead == indexListTail) { return false; }
            *outIndex = indexList[indexListHead];
            indexListHead = (indexListHead + 1) % size;
            return true;
        }

        void ReleaseIndex(uint16_t index)
        {
            indexListTail = (indexListTail + 1) % size;
            indexList[indexListTail] = index;
        }

        bool Allocate(TResource** resource, uint32_t* id)
        {
            uint16_t index = 0;
            if (!GetNextIndex(&index)) {
                return false;
            }
            TResource* res = &buffer[index];
            //res->resState = _ResourceState::STATE_ALLOC;
            *resource = res;
            *id = MAKE_HANDLE(index, res->generation);
            numElements++;
            return true;
        }

        void Free(uint32_t id)
        {
            uint16_t index = HANDLE_INDEX(id);
            TResource* res = &buffer[index];
            assert(res->generation == HANDLE_GENERATION(id));
            //D3D11ReleaseResource(res);

            res->generation++;
            //res->resState = _ResourceState::STATE_EMPTY;
            numElements--;
            ReleaseIndex(index);
        }

        TResource* Get(uint32_t id)
        {
            uint16_t index = HANDLE_INDEX(id);
            TResource* res = &buffer[index];
            assert(res->generation == HANDLE_GENERATION(id));
            return res;
        }
    };


    struct EntityData
    {
        uint16_t generation = HANDLE_GENERATION_START;
        const char* name = "Entity";
    };

    struct World
    {
        fnd::memory::MemoryArenaBase* memoryArena = nullptr;
        ResourcePool<EntityData> entities;
        
    };

    bool CreateWorld(World** outWorld, fnd::memory::MemoryArenaBase* memoryArena, WorldConfig* config)
    {
        World* world = GT_NEW(World, memoryArena);
        world->memoryArena = memoryArena;
        world->entities.Initialize(config->maxNumEntities, memoryArena);
        *outWorld = world;
        return true;
    }

    void DestroyWorld(World* world)
    {

        GT_DELETE(world, world->memoryArena);
    }

    void SetEntityName(World* world, Entity entity, const char* name)
    {
        assert(entity.id != 0);
        EntityData* data = world->entities.Get(entity.id);
        data->name = name;
    }

    const char* GetEntityName(World* world, Entity entity)
    {
        assert(entity.id != 0);
        EntityData* data = world->entities.Get(entity.id);
        return data->name;
    }

    Entity CreateEntity(World* world)
    {
        EntityData* entityData;
        Entity entity;
        if (!world->entities.Allocate(&entityData, &entity.id)) {
            return { INVALID_ID };
        }
        return entity;
    }

    void DestroyEntity(World* world, Entity entity)
    {
        world->entities.Free(entity.id);
    }
}