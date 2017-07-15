
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <stdio.h>
#include <malloc.h>

#include <core_lib/int_types.h>
#include <core_lib/memory/memory.h>


class SimpleTracker
{
    struct TrackEntry
    {
        bool used = false;
        void* address = nullptr;
        size_t size = 0;
        size_t alignment = 0;
        lc::SourceInfo sourceInfo;
    };
    TrackEntry m_entries[1024];
    size_t m_numEntries = 0;
public:
    ~SimpleTracker()
    {
        if (m_numEntries > 0) {
            OutputDebugStringA("MEMORY LEAK! Open allocations:\n");
            char str[512] = "";
            for (auto& entry : m_entries) {
                if (entry.used) {
                    snprintf(str, 512, "%s(%lli): { address = %lli, size = %lli, alignment = %lli }\n", entry.sourceInfo.file, entry.sourceInfo.line, (uintptr_t)entry.address, entry.size, entry.alignment);
                    OutputDebugStringA(str);
                }
            }
        }
    }

    inline void TrackAllocation(void* memory, size_t size, size_t alignment, lc::SourceInfo scInfo)
    {
        TrackEntry entry;
        entry.used = true;
        entry.address = memory;
        entry.size = size;
        entry.alignment = alignment;
        entry.sourceInfo = scInfo;
        m_entries[m_numEntries++] = entry;
    }

    inline void UntrackAllocation(void* memory)
    {
        for (auto& entry : m_entries) {
            if (memory == entry.address) {
                entry.used = false;
            }
        }
        m_numEntries--;
    }

};

class SimpleBoundsChecker
{
    struct GuardData
    {
        size_t v;
    };
public:
    static const size_t FRONT_PADDING = sizeof(GuardData);
    static const size_t BACK_PADDING = sizeof(GuardData);

    inline void WriteFrontGuard(void* memory)
    {
        WriteBackGuard(memory);
    }

    inline void WriteBackGuard(void* memory)
    {
        ((GuardData*)memory)->v = 0xdeadbeef;
    }

    inline void CheckFrontGuard(void* memory)
    {
        if (((GuardData*)memory)->v != 0xdeadbeef) {
            printf("fuu\n");
            OutputDebugStringA("We stepped over our bounds!\n");
        }
    }

    inline void CheckBackGuard(void* memory) {
        CheckFrontGuard(memory);
    }
};

class SimpleTagger
{
public:
    void TagAllocation(void* memory, size_t size)
    {
        
        memset(memory, 0xc0fefe, size);
    }

    void TagDeallocation(void* memory, size_t size)
    {
        memset(memory, 0xdeadb0b, size);
    }
};

#include <core_lib/memory/allocators.h>

#define MEM_DEBUG
namespace lc {
    namespace memory {

#ifdef MEM_DEBUG
#else
        typedef MemoryArena<TLSFAllocator, EmptyThreadPolicy, EmptyBoundsCheckingPolicy, EmptyMemoryTrackingPolicy, EmptyMemoryTaggingPolicy> HeapArena;
#endif
    }
}
#define IS_POW_OF_TWO(n) ((n & (n - 1)) == 0)


typedef lc::memory::SimpleMemoryArena<lc::memory::TLSFAllocator>    HeapArena;
typedef lc::memory::SimpleMemoryArena<lc::memory::LinearAllocator>  LinearArena;

#define GIGABYTES(n) (MEGABYTES(n) * (size_t)1024)
#define MEGABYTES(n) (KILOBYTES(n) * 1024)
#define KILOBYTES(n) (n * 1024)

static_assert(GIGABYTES (8) > MEGABYTES(4), "some size type is wrong");

int main(int argc, char* argv[])
{
    using namespace lc;

    const size_t reservedMemorySize = GIGABYTES(2);
    void* reservedMemory = malloc(reservedMemorySize);

    memory::LinearAllocator applicationAllocator(reservedMemory, reservedMemorySize);
    LinearArena applicationArena(&applicationAllocator);


    static const size_t sandboxedHeapSize = MEGABYTES(500);     // 0.5 gigs of memory for free form allocations @TODO subdivide further for individual 3rd party libs etc
    void* sandboxedHeap = applicationArena.Allocate(sandboxedHeapSize, 4, LC_SOURCE_INFO);

    memory::TLSFAllocator sandboxAllocator(sandboxedHeap, sandboxedHeapSize);
    HeapArena sandboxArena(&sandboxAllocator);

    do {

    } while (true);

    return 0;
}

