#include "api_registry.h"
#include <foundation/memory/memory.h>
#include <string.h>

namespace core
{
    namespace api_registry
    {
        static const size_t MAX_NODES = 64;
        static const size_t MAX_NAME_SIZE = 512;

        struct APINode
        {
            char name[MAX_NAME_SIZE] = "";
            void* interface = nullptr;
            bool isFree = true;
        };

        struct APIRegistry
        {
            APINode nodes[MAX_NODES];
        };

        bool CreateRegistry(APIRegistry** outRegistry, fnd::memory::MemoryArenaBase* memoryArena)
        {
            APIRegistry* registry = GT_NEW(APIRegistry, memoryArena);
            *outRegistry = registry;
            return true;
        }

        void* Get(APIRegistry* registry, const char* name)
        {
            for (size_t i = 0; i < MAX_NODES; ++i) {
                APINode* node = &registry->nodes[i];
                if (!node->isFree) {
                    if (!strcmp(name, node->name)) {
                        return node->interface;
                    }
                }
            }
            return nullptr;
        }

        bool Add(APIRegistry* registry, const char* name, void* interface)
        {
            for (size_t i = 0; i < MAX_NODES; ++i) {
                APINode* node = &registry->nodes[i];
                if (node->isFree) {
                    size_t len = strlen(name) > MAX_NAME_SIZE ? MAX_NAME_SIZE : strlen(name);
                    memset(node->name + len, 0x0, MAX_NAME_SIZE - len);
                    memcpy(node->name, name, len);
                    node->interface = interface;
                    node->isFree = false;
                }
            }
            return false;
        }

        void Remove(APIRegistry* registry, void* interface)
        {
            for (size_t i = 0; i < MAX_NODES; ++i) {
                APINode* node = &registry->nodes[i];
                if (!node->isFree) {
                    if (node->interface == interface) {
                        node->isFree = true;
                    }
                }
            }
        }
    }
}

void api_registry_get_interface(core::api_registry::APIRegistryInterface* outInterface)
{
    outInterface->Add = &core::api_registry::Add;
    outInterface->Get = &core::api_registry::Get;
    outInterface->Remove = &core::api_registry::Remove;
}