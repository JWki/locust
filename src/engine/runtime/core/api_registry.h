#pragma once

namespace fnd
{
    namespace memory
    {
        class MemoryArenaBase;
    }
}

namespace core
{
    namespace api_registry
    {
        struct APIRegistry;

        bool CreateRegistry(APIRegistry** outRegistry, fnd::memory::MemoryArenaBase* memoryArena);


        void* Get(APIRegistry* registry, const char* name);
        bool Add(APIRegistry* registry, const char* name, void* interface);
        void Remove(APIRegistry* registry, void* interface);

        struct APIRegistryInterface
        {
            void* (*Get)(APIRegistry* registry, const char* name)                   = nullptr;

            bool(*Add)(APIRegistry* registry, const char* name, void* interface)    = nullptr;
            void(*Remove)(APIRegistry* registry, void* interface)                   = nullptr;
        };
    }
}

extern "C" __declspec(dllexport)
void api_registry_get_interface(core::api_registry::APIRegistryInterface* outInterface);

#define SIM_UPDATE_API_NAME "sim_update"
#define RENDER_UPDATE_API_NAME "render_update"

