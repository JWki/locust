#pragma once
#include <core_lib/int_types.h>
//
//  Thanks to Stefan Reinalter and his blog @ blog.molecular-matters.com

#define LC_SOURCE_INFO {__LINE__, __FILE__}
#define LC_FORCE_INLINE __forceinline

namespace lc 
{ 
    struct SourceInfo
    {
        size_t      line = 0;
        const char* file = "";
        //const char* function = "";

        SourceInfo() = default;
        SourceInfo(size_t l, const char* f) : line(l), file(f) {}
    };
    
    namespace pointerUtil
    {
        static void* AlignAddress(void* address, size_t alignment)
        {
            return reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(address) + (alignment - 1)) & ~(alignment - 1));
        }
    }

    namespace memory
    {
        /* Base interface for memory arenas */
        class MemoryArenaBase
        {
        public:
            virtual ~MemoryArenaBase() {}

            virtual void*   Allocate(size_t size, size_t alignment, SourceInfo scInfo) = 0;
            virtual void    Free(void* ptr) = 0;
            virtual size_t  GetAllocationSize(void* ptr) = 0;
        };


        /* Policy based memory arena implementation */
        template <class Allocator, class ThreadPolicy, class BoundsCheckingPolicy, class MemoryTrackingPolicy, class MemoryTaggingPolicy>
        class MemoryArena : public MemoryArenaBase
        {
        public:
            MemoryArena(Allocator* allocator) : m_allocator(allocator) {}
            virtual ~MemoryArena() {}

            virtual void*   Allocate(size_t size, size_t alignment, SourceInfo scInfo) override;
            virtual void    Free(void* ptr) override;

            virtual size_t  GetAllocationSize(void* ptr) override
            {
                // @TODO: use thread policy here?
                char* memory = static_cast<char*>(ptr) - BoundsCheckingPolicy::FRONT_PADDING;
                return m_allocator->GetAllocationSize(memory);
            }

        private:
            Allocator* m_allocator;

            ThreadPolicy            m_threadGuard;
            BoundsCheckingPolicy    m_boundsChecker;
            MemoryTrackingPolicy    m_memTracker;
            MemoryTaggingPolicy     m_memTagger;
        };


        /* Empty policies */
        
        class EmptyThreadPolicy
        {
        public:
            LC_FORCE_INLINE void Enter() {};
            LC_FORCE_INLINE void Exit() {};
        };
        
        class EmptyBoundsCheckingPolicy
        {
        public:
            static const size_t FRONT_PADDING = 0;
            static const size_t BACK_PADDING = 0;

            LC_FORCE_INLINE void WriteFrontGuard(void* memory) {}
            LC_FORCE_INLINE void WriteBackGuard(void* memory) {}

            LC_FORCE_INLINE void CheckFrontGuard(void* memory) {}
            LC_FORCE_INLINE void CheckBackGuard(void* memory) {}
        };

        class EmptyMemoryTrackingPolicy
        {
        public:
            LC_FORCE_INLINE void TrackAllocation(void* memory, size_t size, size_t alignemnt, SourceInfo scInfo) {}
            LC_FORCE_INLINE void UntrackAllocation(void* memory) {}
        };

        class EmptyMemoryTaggingPolicy
        {
        public:
            LC_FORCE_INLINE void TagAllocation(void* memory, size_t size) {}
            LC_FORCE_INLINE void TagDeallocation(void* memory, size_t size) {}
        };

        template <class Allocator>
        using SimpleMemoryArena = MemoryArena<Allocator, EmptyThreadPolicy, EmptyBoundsCheckingPolicy, EmptyMemoryTrackingPolicy, EmptyMemoryTaggingPolicy>;

        // @TODO: Put into .inl file?
        template <class Allocator, class ThreadPolicy, class BoundsCheckingPolicy, class MemoryTrackingPolicy, class MemoryTaggingPolicy>
        void* MemoryArena<Allocator, ThreadPolicy, BoundsCheckingPolicy, MemoryTrackingPolicy, MemoryTaggingPolicy>::Allocate(size_t size, size_t alignment, SourceInfo scInfo)
        {
            m_threadGuard.Enter();

            const size_t requestedSize = size;
            const size_t totalSize = requestedSize + BoundsCheckingPolicy::FRONT_PADDING + BoundsCheckingPolicy::BACK_PADDING;

            char* memory = static_cast<char*>(m_allocator->Allocate(totalSize, alignment, BoundsCheckingPolicy::FRONT_PADDING));
            if (memory == nullptr) { return nullptr; }

            m_boundsChecker.WriteFrontGuard(memory);
            m_memTagger.TagAllocation(memory + BoundsCheckingPolicy::FRONT_PADDING, requestedSize);
            m_boundsChecker.WriteBackGuard(memory + BoundsCheckingPolicy::FRONT_PADDING + requestedSize);

            m_memTracker.TrackAllocation(memory, totalSize, alignment, scInfo);

            m_threadGuard.Exit();

            return (memory + BoundsCheckingPolicy::FRONT_PADDING);
        }

        template <class Allocator, class ThreadPolicy, class BoundsCheckingPolicy, class MemoryTrackingPolicy, class MemoryTaggingPolicy>
        void MemoryArena<Allocator, ThreadPolicy, BoundsCheckingPolicy, MemoryTrackingPolicy, MemoryTaggingPolicy>::Free(void* ptr)
        {
            m_threadGuard.Enter();

            char* memory = static_cast<char*>(ptr) - BoundsCheckingPolicy::FRONT_PADDING;
            const size_t allocationSize = m_allocator->GetAllocationSize(memory);

            m_boundsChecker.CheckFrontGuard(memory);
            m_boundsChecker.CheckBackGuard(memory + allocationSize - BoundsCheckingPolicy::BACK_PADDING);

            m_memTracker.UntrackAllocation(memory);

            m_memTagger.TagDeallocation(memory, allocationSize);

            m_allocator->Free(memory);

            m_threadGuard.Exit();
        }

    }
}

namespace lc {
    namespace internal
    {
        struct PlacementNewDummy {};

        template <class T, class Arena>
        void Delete(T* object, Arena* arena)
        {
            object->~T();
            arena->Free(object);
        }
    }
}

inline void* operator new(size_t, lc::internal::PlacementNewDummy, void* ptr) { return ptr; }
inline void operator delete(void*, lc::internal::PlacementNewDummy, void*) {}

#define LC_PLACEMENT_NEW(ptr) new(lc::internal::PlacementNewDummy(), ptr)

#define LC_NEW(Type, arenaAsPtr) \
LC_PLACEMENT_NEW (arenaAsPtr->Allocate(sizeof(Type), alignof(Type), LC_SOURCE_INFO)) Type

#define LC_DELETE(objectAsPtr, arenaAsPtr) \
lc::internal::Delete(objectAsPtr, arenaAsPtr)