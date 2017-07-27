#pragma once

#include <core_lib/int_types.h>
#include <core_lib/memory/memory.h>

namespace lc
{
    namespace gfx
    {
        enum class ResourceUsage : uint8_t
        {
            USAGE_DEFAULT,      // read/write by GPU
            USAGE_IMMUTABLE,    // read only by GPU, no CPU access
            USAGE_DYNAMIC,      // read only by GPU, write only by CPU
            USAGE_STAGING       // GPU -> CPU data transfer
        };

        struct BufferDesc
        {
            ResourceUsage   usage       = ResourceUsage::USAGE_DEFAULT;
            size_t          byteWidth   = 0;
        };

        struct BufferImpl;
        struct TextureImpl;
        struct ShaderImpl;
        struct PipelineStateImpl;

        typedef BufferImpl*         Buffer;
        typedef TextureImpl*        Texture;
        typedef ShaderImpl*         Shader;
        typedef PipelineStateImpl*  PipelineState;

        static const size_t MAX_GPUS = 8;
        typedef uint8_t GPUAffinityMask;

        struct GPUDescriptor
        { 
            const char*     readableName = nullptr;
            GPUAffinityMask affinityMask = 0;
        };
        
        struct GFXInterfaceInfo
        {
            GPUDescriptor gpuDescriptors[MAX_GPUS];
        };

        class GFXInterface
        {
        public:
            virtual ~GFXInterface() = default;
            
            virtual bool Initialize(memory::MemoryArenaBase* memoryArena, GFXInterfaceInfo* info) = 0;
        
            virtual Buffer CreateBuffer(BufferDesc, GPUAffinityMask) = 0;
        };
    }
}
