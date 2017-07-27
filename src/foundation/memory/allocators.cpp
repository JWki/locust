#include "allocators.h"
#include "memory.h"
#include "../int_types.h"

namespace lc 
{
    namespace memory
    {
        TLSFAllocator::TLSFAllocator(void* memory, size_t memsize)
            :   m_internal(tlsf_create_with_pool(memory, memsize))
        {}  

        void* TLSFAllocator::Allocate(size_t size, size_t alignment, size_t offset)
        {
            size_t totalSize = size + alignment + sizeof(AllocationHeader);
            char* memory = static_cast<char*>(tlsf_malloc(m_internal, totalSize));
            if (memory == nullptr) { return nullptr; }
            char* alignedMemory = static_cast<char*>(lc::pointerUtil::AlignAddress(memory + offset + sizeof(AllocationHeader), alignment)) - offset;
            AllocationHeader* info = reinterpret_cast<AllocationHeader*>(alignedMemory - sizeof(AllocationHeader));
            info->size = size;
            info->adjust = uint32_t(reinterpret_cast<uintptr_t>(info) - reinterpret_cast<uintptr_t>(memory));
            return alignedMemory;
        }

        void TLSFAllocator::Free(void* ptr)
        {
            AllocationHeader* allocInfo = static_cast<AllocationHeader*>(ptr) - 1;
            char* originalMemory = reinterpret_cast<char*>(allocInfo) - allocInfo->adjust;
            tlsf_free(m_internal, originalMemory);
        }

        size_t TLSFAllocator::GetAllocationSize(void* ptr)
        {
            AllocationHeader* allocInfo = static_cast<AllocationHeader*>(ptr) - 1;
            return allocInfo->size;
        }


        LinearAllocator::LinearAllocator(void* memory, size_t memsize)
            :   m_start(static_cast<char*>(memory)), m_end(static_cast<char*>(memory) + memsize), m_current(static_cast<char*>(memory))
        {}

        void* LinearAllocator::Allocate(size_t size, size_t alignment, size_t offset)
        {
            char* current = m_current;
            m_current = static_cast<char*>(pointerUtil::AlignAddress(m_current + offset + sizeof(AllocationHeader), alignment)) - offset;
            void* memory = m_current;
            AllocationHeader* header = reinterpret_cast<AllocationHeader*>(memory) - 1;
            header->size = size;
            m_current += size + sizeof(AllocationHeader);
            if (m_current >= m_end) { m_current = current; return nullptr; }
            return memory;
        }

        size_t LinearAllocator::GetAllocationSize(void* ptr)
        {
            AllocationHeader* header = reinterpret_cast<AllocationHeader*>(ptr) - 1;
            return header->size;
        }

        void LinearAllocator::Reset()
        {
            m_current = m_start;
        }

        void LinearAllocator::Free(void* ptr)
        {
            //  @NOTE no-op, maybe issue warning here?
        }
    }
}