#pragma once
#include "../int_types.h"
//
//  Thanks to Stefan Reinalter and his blog @ blog.molecular-matters.com


#ifdef _MSC_VER
#define GT_FORCE_INLINE __forceinline
#else
#define GT_FORCE_INLINE inline
#endif

namespace fnd 
{ 
   
    
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
        template <class TAllocator, class TThreadPolicy, class TBoundsCheckingPolicy, class TMemoryTrackingPolicy, class TMemoryTaggingPolicy>
        class MemoryArena : public MemoryArenaBase
        {
        public:
            using Allocator = TAllocator;
            using ThreadPolicy = TThreadPolicy;
            using BoundsCheckingPolicy = TBoundsCheckingPolicy;
            using MemoryTrackingPolicy = TMemoryTrackingPolicy;
            using MemoryTaggingPolicy = TMemoryTaggingPolicy;

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

        protected:
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
            GT_FORCE_INLINE void Enter() {};
            GT_FORCE_INLINE void Exit() {};
        };
        
        class EmptyBoundsCheckingPolicy
        {
        public:
            static const size_t FRONT_PADDING = 0;
            static const size_t BACK_PADDING = 0;

            GT_FORCE_INLINE void WriteFrontGuard(void* memory) {}
            GT_FORCE_INLINE void WriteBackGuard(void* memory) {}

            GT_FORCE_INLINE void CheckFrontGuard(void* memory) {}
            GT_FORCE_INLINE void CheckBackGuard(void* memory) {}
        };

        class EmptyMemoryTrackingPolicy
        {
        public:
            GT_FORCE_INLINE void TrackAllocation(void* memory, size_t size, size_t alignment, SourceInfo scInfo) {}
            GT_FORCE_INLINE void UntrackAllocation(void* memory, size_t size) {}
        };

        class EmptyMemoryTaggingPolicy
        {
        public:
            GT_FORCE_INLINE void TagAllocation(void* memory, size_t size) {}
            GT_FORCE_INLINE void TagDeallocation(void* memory, size_t size) {}
        };

        template <class TAllocator>
        using SimpleMemoryArena = MemoryArena<TAllocator, EmptyThreadPolicy, EmptyBoundsCheckingPolicy, EmptyMemoryTrackingPolicy, EmptyMemoryTaggingPolicy>;

        template <class TAllocator, class TTrackingPolicy>
        class SimpleTrackingArena : public MemoryArena<TAllocator, EmptyThreadPolicy, EmptyBoundsCheckingPolicy, TTrackingPolicy, EmptyMemoryTaggingPolicy>
        {
        public:
            SimpleTrackingArena(typename MemoryArena::Allocator* allocator) : MemoryArena(allocator) {}
            virtual ~SimpleTrackingArena() = default;

            inline TTrackingPolicy* GetTrackingPolicy()
            {
                return &m_memTracker;
            }
            
        };


        // @TODO: Put into .inl file?
        template <class TAllocator, class TThreadPolicy, class TBoundsCheckingPolicy, class TMemoryTrackingPolicy, class TMemoryTaggingPolicy>
        void* MemoryArena<TAllocator, TThreadPolicy, TBoundsCheckingPolicy, TMemoryTrackingPolicy, TMemoryTaggingPolicy>::Allocate(size_t size, size_t alignment, SourceInfo scInfo)
        {
            m_threadGuard.Enter();

            const size_t requestedSize = size;
            const size_t totalSize = requestedSize + TBoundsCheckingPolicy::FRONT_PADDING + TBoundsCheckingPolicy::BACK_PADDING;

            char* memory = static_cast<char*>(m_allocator->Allocate(totalSize, alignment, TBoundsCheckingPolicy::FRONT_PADDING));
            if (memory == nullptr) { return nullptr; }

            m_boundsChecker.WriteFrontGuard(memory);
            m_memTagger.TagAllocation(memory + TBoundsCheckingPolicy::FRONT_PADDING, requestedSize);
            m_boundsChecker.WriteBackGuard(memory + TBoundsCheckingPolicy::FRONT_PADDING + requestedSize);

            m_memTracker.TrackAllocation(memory, totalSize, alignment, scInfo);

            m_threadGuard.Exit();

            return (memory + TBoundsCheckingPolicy::FRONT_PADDING);
        }

        template <class TAllocator, class TThreadPolicy, class TBoundsCheckingPolicy, class TMemoryTrackingPolicy, class TMemoryTaggingPolicy>
        void MemoryArena<TAllocator, TThreadPolicy, TBoundsCheckingPolicy, TMemoryTrackingPolicy, TMemoryTaggingPolicy>::Free(void* ptr)
        {
            m_threadGuard.Enter();
            
            if (ptr == nullptr) { return; }

            char* memory = static_cast<char*>(ptr) - TBoundsCheckingPolicy::FRONT_PADDING;
            const size_t allocationSize = m_allocator->GetAllocationSize(memory);

            m_boundsChecker.CheckFrontGuard(memory);
            m_boundsChecker.CheckBackGuard(memory + allocationSize - TBoundsCheckingPolicy::BACK_PADDING);

            m_memTracker.UntrackAllocation(memory, allocationSize);

            m_memTagger.TagDeallocation(memory, allocationSize);

            m_allocator->Free(memory);

            m_threadGuard.Exit();
        }

    }
}

namespace fnd {
    namespace internal
    {
        struct PlacementNewDummy {};

    }
}

inline void* operator new(size_t, fnd::internal::PlacementNewDummy, void* ptr) { return ptr; }
inline void operator delete(void*, fnd::internal::PlacementNewDummy, void*) {}

#define GT_PLACEMENT_NEW(ptr) new(fnd::internal::PlacementNewDummy(), ptr)

namespace fnd {
    namespace internal {

        template <class T>
        struct RemovePointer
        {
            typedef T Type;
        };

        template <class T>
        struct RemovePointer<T*>
        {
            typedef T Type;
        };

        template <size_t sizeA, size_t sizeB>
        void CheckSize()
        {
            static_assert(sizeA == sizeB, "sizes are off");
        }

        template <class T, class Arena>
        void Delete(T* object, Arena* arena)
        {
            object->~T();
            arena->Free(object);
        }

        struct ArrayHeader
        {
            size_t count;
            uint32_t adjustment;
        };

        template <class T, class Arena>
        T* NewArray(size_t count, Arena* arena, SourceInfo scInfo)
        {
            const size_t padding = sizeof(size_t) <= sizeof(T) ? sizeof(T) : sizeof(T) * (1 + sizeof(size_t) / sizeof(T));
            //CheckSize<padding % sizeof(T), 0>();
            static_assert(padding % sizeof(T) == 0, "Our maths is off");
            const size_t totalSize = sizeof(T) * count + padding;

            union {
                char* as_char;
                size_t* as_size_t;
                T* as_t;
            };

            as_char = static_cast<char*>(arena->Allocate(totalSize, alignof(T), scInfo));
            *(as_size_t) = count;
            as_char += padding;
            for (size_t i = 0; i < count; ++i) {
                GT_PLACEMENT_NEW(as_t + i) T();
            }
            return reinterpret_cast<T*>(as_t);
        }

        template <class T, class Arena, class InitializerFunc>
        T* NewArrayWithInitializer(size_t count, Arena* arena, InitializerFunc initializerFunc, SourceInfo scInfo)
        {
            const size_t padding = sizeof(size_t) <= sizeof(T) ? sizeof(T) : sizeof(T) * (1 + sizeof(size_t) / sizeof(T));
            //CheckSize<padding % sizeof(T), 0>();
            static_assert(padding % sizeof(T) == 0, "Our maths is off");
            const size_t totalSize = sizeof(T) * count + padding;

            union {
                char* as_char;
                size_t* as_size_t;
                T* as_t;
            };

            as_char = static_cast<char*>(arena->Allocate(totalSize, alignof(T), scInfo));
            *(as_size_t) = count;
            as_char += padding;
            for (size_t i = 0; i < count; ++i) {
                initializerFunc(&as_t[i]);
            }
            return reinterpret_cast<T*>(as_t);
        }

        template <class T, class Arena>
        void DeleteArray(T* array, Arena* arena)
        {
            const size_t padding = sizeof(size_t) <= sizeof(T) ? sizeof(T) : sizeof(T) * (1 + sizeof(size_t) / sizeof(T));
            //CheckSize<padding % sizeof(T), 0>();
            static_assert(padding % sizeof(T) == 0, "Our maths is off");
            
            union {
                char* as_char;
                size_t* as_size_t;
                T* as_t;
            };

            as_t = array;
            as_char -= padding;
            const size_t count = *as_size_t;
            as_t = array;
            for (size_t i = count - 1; i > 0; --i) {
                auto object = as_t + i;
                object->~T();
            }
            arena->Free(as_char - padding);
        }
    }
}



#define GT_NEW(Type, arenaAsPtr) \
GT_PLACEMENT_NEW (arenaAsPtr->Allocate(sizeof(Type), alignof(Type), GT_SOURCE_INFO)) Type

#define GT_NEW_ARRAY(ArrayType, count, arenaAsPtr) \
fnd::internal::NewArray<ArrayType, fnd::internal::RemovePointer<decltype(arenaAsPtr)>::Type>(count, arenaAsPtr, GT_SOURCE_INFO)

#define GT_NEW_ARRAY_WITH_INITIALIZER(ArrayType, count, arenaAsPtr, initializerFunc) \
fnd::internal::NewArrayWithInitializer<ArrayType, fnd::internal::RemovePointer<decltype(arenaAsPtr)>::Type>(count, arenaAsPtr, initializerFunc, GT_SOURCE_INFO)

#define GT_DELETE(objectAsPtr, arenaAsPtr) \
fnd::internal::Delete(objectAsPtr, arenaAsPtr)

#define GT_DELETE_ARRAY(array, arenaAsPtr) \
fnd::internal::DeleteArray(array, arenaAsPtr) 