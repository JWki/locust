#pragma once

#include <stdint.h>

// @TODO remove dependency on this?
namespace fnd {
    namespace memory {
        class MemoryArenaBase;
    }
}

namespace gfx
{

    /* Resource handle types */
    
    typedef struct { uint32_t id; } Buffer;     // A buffer for vertex/index/constant data
    typedef struct { uint32_t id; } Image;      // A texture or render target

    typedef struct { uint32_t id; } PipelineState;  // Wraps render states, vertex input layout, shader bindings...
    typedef struct { uint32_t id; } Shader;         // a compiled shader object @NOTE: pack vertex/pixel/etc shader into one object? PipelineObject?
    typedef struct { uint32_t id; } RenderPass;     // defines a complete render pass with all render targets, clear/resolve actions, etc

    typedef struct { uint32_t id; } CommandBuffer;  // wraps (or emulates) a command buffer for multithreaded command recording

    typedef struct { uint32_t id; } SwapChain;      // wraps a swap chain (for multi window stuff)

    /* some constants */
    enum { INVALID_ID = 0 };

    /* */

    enum class ResourceUsage : uint8_t
    {
        _DEFAULT = 0,
        USAGE_IMMUTABLE,      // data is never updated
        USAGE_DYNAMIC,        // data is updated infrequently
        USAGE_STREAM,         // data is updated at least once or multiple times per frame
        USAGE_STAGING         // data can be read back by the CPU
    };

    enum class BufferType : uint8_t
    {
        _DEFAULT = 0,
        BUFFER_TYPE_VERTEX,
        BUFFER_TYPE_INDEX,
        BUFFER_TYPE_CONSTANT
    };

    enum class IndexFormat : uint8_t
    {
        _DEFAULT = 0,
        INDEX_FORMAT_NONE,   // no indexing used
        INDEX_FORMAT_UINT16, // 16 bit unsigned
        INDEX_FORMAT_UINT32  // 32 bit unsigned
    };

    enum class ImageType : uint8_t
    {
        _DEFAULT = 0,
        IMAGE_TYPE_2D,
        IMAGE_TYPE_CUBE,
        IMAGE_TYPE_3D,
        IMAGE_TYPE_ARRAY
    };

    enum class ShaderType : uint8_t
    {
        _DEFAULT = 0,
        SHADER_TYPE_VS,     // vertex shader
        SHADER_TYPE_PS,     // pixel shader
        SHADER_TYPE_GS,     // geometry shader
        SHADER_TYPE_DS,     // domain shader
        SHADER_TYPE_HS      // hull shader
    };

    enum class PrimitiveType : uint8_t
    {
        _DEFAULT = 0,
        PRIMITIVE_TYPE_POINTS,
        PRIMITIVE_TYPE_LINES,
        PRIMITIVE_TYPE_TRIANGLES,
        PRIMITIVE_TYPE_TRIANGLE_STRIP
    };

    enum class FilterMode : uint8_t
    {
        _DEFAULT = 0,
        FILTER_NEAREST,
        FILTER_LINEAR,
        FILTER_NEAREST_MIPMAP_NEAREST,
        FILTER_NEAREST_MIPMAP_LINEAR,
        FILTER_LINEAR_MIPMAP_NEAREST,
        FILTER_LINEAR_MIPMAP_LINEAR
    };

    enum class WrapMode : uint8_t
    {
        _DEFAULT = 0,
        WRAP_REPEAT,
        WRAP_CLAMP_TO_EDGE,
        WRAP_MIRRORED_REPEAT
    };

    // @TODO: Pixel formats


    /* interface contexts */

    struct Interface;       // setup & memory management interface, used to access list of devices
    struct Device;          // main interface for command submission and resource creation

#define GFX_DEVICE_INFO_NAME_LEN 128
    struct DeviceInfo
    {
        uint32_t    index = 0;                                      // index within internal device list
        char        friendlyName[GFX_DEVICE_INFO_NAME_LEN] = "";   // human readable name
        // @TODO Add more things here
    };

#define GFX_DEFAULT_BUFFER_POOL_SIZE        1024
#define GFX_DEFAULT_IMAGE_POOL_SIZE         1024
#define GFX_DEFAULT_PIPELINE_POOL_SIZE      1024
#define GFX_DEFAULT_SHADER_POOL_SIZE        1024
#define GFX_DEFAULT_RENDER_PASS_POOL_SIZE   1024
#define GFX_DEFAULT_CMD_BUFFER_POOL_SIZE    1024
#define GFX_DEFAULT_MAX_NUM_SWAPCHAINS      64
#define GFX_DEFAULT_MAX_NUM_DEVICES         4

    struct InterfaceDesc
    {
        uint32_t    bufferPoolSize      = GFX_DEFAULT_BUFFER_POOL_SIZE;
        uint32_t    imagePoolSize       = GFX_DEFAULT_IMAGE_POOL_SIZE;
        uint32_t    pipelinePoolSize    = GFX_DEFAULT_PIPELINE_POOL_SIZE;
        uint32_t    shaderPoolSize      = GFX_DEFAULT_SHADER_POOL_SIZE;
        uint32_t    renderPassPoolSize  = GFX_DEFAULT_RENDER_PASS_POOL_SIZE;
        uint32_t    cmdBufferPoolSize   = GFX_DEFAULT_CMD_BUFFER_POOL_SIZE;
        uint32_t    maxNumSwapChains    = GFX_DEFAULT_MAX_NUM_SWAPCHAINS;
        uint32_t    maxNumDevices       = GFX_DEFAULT_MAX_NUM_DEVICES;
    };

    enum class _ResourceState : uint8_t
    {
        STATE_EMPTY = 0,
        STATE_ALLOC,
        STATE_VALID
    };

    /* Creates a device */
    bool CreateInterface(Interface** outInterface, InterfaceDesc* desc, fnd::memory::MemoryArenaBase* memoryArena);
    /* Enumerates all potential devices present in the system (GPU id, readable name, etc) */
    void EnumerateDevices(Interface* interf, DeviceInfo* outInfo, uint32_t* numDevices);
    /* Returns a device with given index */
    Device* GetDevice(Interface* interf, uint32_t index);

    /* Resource creation */

    struct BufferDesc
    {
        uint32_t        byteWidth       = 0;
        BufferType      type            = BufferType::_DEFAULT;
        ResourceUsage   usage           = ResourceUsage::_DEFAULT;

        void*           initialData     = nullptr;
        uint32_t        initialDataSize = 0;
    };

    struct ImageDesc
    {
        // @TODO
    };

    struct PipelineStateDesc
    {
        // @TODO
    };
    
    struct ShaderDesc
    {
        // @TODO
    };

    struct RenderPassDesc
    {
        // @TODO
    };

    struct CommandBufferDesc
    {
        // @TODO
    };

    struct SwapChainDesc
    {
        // @TODO
    };

    Buffer CreateBuffer(Device* device, BufferDesc* desc);
    bool CreateImage(Device* device, Image* outImage, ImageDesc* desc);
    bool CreatePipelineState(Device* device, PipelineState* outPipeline, PipelineStateDesc* desc);
    bool CreateShader(Device* device, Shader* outShader, ShaderDesc* desc);
    bool CreateRenderPass(Device* device, RenderPass* outRenderPass, RenderPassDesc* desc);
    bool CreateCommandBuffer(Device* device, CommandBuffer* outCmdBuffer, CommandBufferDesc* desc);
    bool CreateSwapChain(Device* device, SwapChain* outSwapChain, SwapChainDesc* desc);

    bool GetBufferDesc(Device* device, Buffer* buffer);
    bool GetImageDesc(Device* device, Image* image);
    bool GetPipelineStateDesc(Device* device, PipelineState* pipelineState);
    bool GetShaderDesc(Device* device, Shader* shader);
    bool GetRenderPassDesc(Device* device, RenderPass* pass);
    
    bool GetSwapChainDesc(Device* device, SwapChain* swapChain);

}
