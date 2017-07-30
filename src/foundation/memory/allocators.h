#pragma once
#include "../int_types.h"
#include "tlsf.h"

namespace fnd
{
    namespace memory
    {
        class TLSFAllocator
        {
            tlsf_t m_internal;
            
        public:
            struct AllocationHeader
            {
                size_t      size = 0;
                uint32_t    adjust = 0;
            };

            TLSFAllocator(void* memory, size_t memsize);

            void*   Allocate(size_t size, size_t alignment) { return Allocate(size, alignment, 0); }
            void*   Allocate(size_t size, size_t alignment, size_t offset);
            void    Free(void* ptr);

            size_t  GetAllocationSize(void* ptr);
        };

        class LinearAllocator
        {
            char*   m_end = 0;
            char*   m_start = nullptr;
            char*   m_current = nullptr;
              
        public:
            struct AllocationHeader
            {
                size_t size = 0;
            };

            LinearAllocator(void* memory, size_t memsize);

            void*   Allocate(size_t size, size_t alignment) { return Allocate(size, alignment, 0); }
            void*   Allocate(size_t size, size_t alignment, size_t offset);
            void    Free(void* ptr);

            void    Reset();

            size_t  GetAllocationSize(void* ptr);
        };
    }
}