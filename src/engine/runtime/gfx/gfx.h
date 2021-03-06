#pragma once

#include <stdint.h>

// @TODO remove dependency on this?
namespace fnd {
    namespace memory {
        class MemoryArenaBase;
    }
}

#define GFX_DEVICE_INFO_NAME_LEN 128

#define GFX_DEFAULT_BUFFER_POOL_SIZE        1024
#define GFX_DEFAULT_IMAGE_POOL_SIZE         1024
#define GFX_DEFAULT_PIPELINE_POOL_SIZE      1024
#define GFX_DEFAULT_SHADER_POOL_SIZE        1024
#define GFX_DEFAULT_RENDER_PASS_POOL_SIZE   1024
#define GFX_DEFAULT_CMD_BUFFER_POOL_SIZE    1024
#define GFX_DEFAULT_MAX_NUM_SWAPCHAINS      64
#define GFX_DEFAULT_MAX_NUM_DEVICES         4

#ifndef GFX_MAX_VERTEX_ATTRIBS
#define GFX_MAX_VERTEX_ATTRIBS 8
#endif

#ifndef GFX_MAX_COLOR_ATTACHMENTS
#define GFX_MAX_COLOR_ATTACHMENTS 8
#endif

#ifndef GFX_MAX_VERTEX_STREAMS
#define GFX_MAX_VERTEX_STREAMS 8
#endif

#ifndef GFX_MAX_IMAGE_INPUTS_PER_STAGE
#define GFX_MAX_IMAGE_INPUTS_PER_STAGE 12
#endif

#ifndef GFX_MAX_CONSTANT_INPUTS_PER_STAGE
#define GFX_MAX_CONSTANT_INPUTS_PER_STAGE 4
#endif

#define GFX_CHECK_RESOURCE(handle) (handle.id != gfx::INVALID_ID)


namespace gfx
{

    /* some constants */
    enum { INVALID_ID = 0 };

    /* Resource handle types */
    
    typedef struct { uint32_t id = INVALID_ID; } Buffer;    // A buffer for vertex/index/constant data
    typedef struct { uint32_t id = INVALID_ID; } Image;     // A texture or render target
    typedef struct { uint32_t id = INVALID_ID; } Sampler;   // texture sampler state 

    typedef struct { uint32_t id = INVALID_ID; } PipelineState;  // Wraps render states, vertex input layout, shader bindings...
    typedef struct { uint32_t id = INVALID_ID; } Shader;         // a compiled shader object @NOTE: pack vertex/pixel/etc shader into one object? PipelineObject?
    typedef struct { uint32_t id = INVALID_ID; } RenderPass;     // defines a complete render pass with all render targets, clear/resolve actions, etc

    typedef struct { uint32_t id = INVALID_ID; } CommandBuffer;  // wraps (or emulates) a command buffer for multithreaded command recording

    typedef struct { uint32_t id = INVALID_ID; } SwapChain;      // wraps a swap chain (for multi window stuff)

    /* */

    enum class ResourceUsage : uint8_t
    {
        _DEFAULT = 0,
        USAGE_IMMUTABLE,      // data is never updated
        USAGE_DYNAMIC,        // data is updated infrequently
        USAGE_STREAM,         // data is updated at least once or multiple times per frame
        USAGE_STAGING         // data can be read back by the CPU
    };

    enum class MapType : uint8_t
    {
        _DEFAULT = 0,
        MAP_READ,
        MAP_WRITE, 
        MAP_READ_WRITE,
        MAP_WRITE_DISCARD,
        MAP_WRITE_NO_OVERWRITE
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
        IMAGE_TYPE_2DARRAY
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
        FILTER_LINEAR,
        FILTER_NEAREST,
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

    // @TODO: MORE Pixel formats
    enum class PixelFormat : uint8_t
    {
        _DEFAULT = 0,
        PIXEL_FORMAT_NONE,
        PIXEL_FORMAT_R8G8B8A8_UNORM,
        PIXEL_FORMAT_R16G16B16A16_FLOAT,
        PIXEL_FORMAT_R32G32B32A32_FLOAT,
        PIXEL_FORMAT_R8G8B8A8_UINT,
        PIXEL_FORMAT_R16G16B16A16_UINT,
        PIXEL_FORMAT_D32_FLOAT_S8X24_UINT   // for depth stencil targets
    };

    /* interface contexts */

    struct Interface;       // setup & memory management interface, used to access list of devices
    struct Device;          // main interface for command submission and resource creation

    struct DeviceInfo
    {
        uint32_t    index = 0;                                      // index within internal device list
        char        friendlyName[GFX_DEVICE_INFO_NAME_LEN] = "";   // human readable name
        // @TODO Add more things here
    };



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
        size_t          byteWidth       = 0;
        BufferType      type            = BufferType::_DEFAULT;
        ResourceUsage   usage           = ResourceUsage::_DEFAULT;

        void*           initialData     = nullptr;
        size_t          initialDataSize = 0;
    };

    struct SamplerDesc
    {
        FilterMode      minFilter = FilterMode::FILTER_LINEAR;
        FilterMode      magFilter = FilterMode::FILTER_LINEAR;

        WrapMode        wrapU = WrapMode::WRAP_REPEAT;
        WrapMode        wrapV = WrapMode::WRAP_REPEAT;
        WrapMode        wrapW = WrapMode::WRAP_REPEAT;
    };

    struct ImageDesc
    {
        ImageType   type            = ImageType::_DEFAULT;
        bool        isRenderTarget  = false;
        bool        isDepthStencilTarget = false;

        uint16_t    width           = 0;
        uint16_t    height          = 0;
        uint16_t    numMipmaps      = 1;

        //union {
            //uint16_t    depth;
            uint16_t    numSlices = 1;
        //};

        ResourceUsage   usage       = ResourceUsage::_DEFAULT;
        
        PixelFormat     pixelFormat = PixelFormat::_DEFAULT;
        SamplerDesc*    samplerDesc = nullptr;

        
        size_t          numDataItems        = 0;
        void**          initialData         = nullptr;
        size_t*         initialDataSizes    = nullptr;
        // @TODO
    };

    enum class VertexFormat : uint8_t
    {
        VERTEX_FORMAT_INVALID,
        VERTEX_FORMAT_FLOAT,
        VERTEX_FORMAT_FLOAT2,
        VERTEX_FORMAT_FLOAT3,
        VERTEX_FORMAT_FLOAT4,
        VERTEX_FORMAT_R8G8B8A8_UNNORM
    };

    struct VertexAttribDesc
    {
        const char* name    = "";
        uint32_t    index   = 0;
        uint32_t    offset  = 0;
        uint32_t    slot    = 0;
        VertexFormat format = VertexFormat::VERTEX_FORMAT_INVALID;
    };


    struct VertexLayoutDesc
    {
        VertexAttribDesc    attribs[GFX_MAX_VERTEX_ATTRIBS];
    };

    // @NOTE maybe expose those as resource objects - maybe just let implementation cache

    struct DepthStateDesc
    {

    };

    enum class BlendFactor : uint8_t 
    {
        BLEND_ZERO,
        BLEND_ONE,
        BLEND_SRC_COLOR,
        BLEND_INV_SRC_COLOR,
        BLEND_SRC_ALPHA,
        BLEND_INV_SRC_ALPHA,
        BLEND_DEST_ALPHA,
        BLEND_INV_DEST_ALPHA,
        BLEND_DEST_COLOR,
        BLEND_INV_DEST_COLOR,
        BLEND_SRC_ALPHA_SAT,
        BLEND_BLEND_FACTOR,
        BLEND_INV_BLEND_FACTOR,
        BLEND_SRC1_COLOR,
        BLEND_INV_SRC1_COLOR,
        BLEND_SRC1_ALPHA,
        BLEND_INV_SRC1_ALPHA
    };

    enum class BlendOp : uint8_t
    {
        BLEND_OP_ADD,
        BLEND_OP_SUBTRACT,
        BLEND_OP_REV_SUBTRACT,
        BLEND_OP_MIN,
        BLEND_OP_MAX
    };

    enum COLOR_WRITE_MASK : uint8_t {
        COLOR_WRITE_MASK_RED = 1,
        COLOR_WRITE_MASK_GREEN = 2,
        COLOR_WRITE_MASK_BLUE = 4,
        COLOR_WRITE_MASK_ALPHA = 8,
        COLOR_WRITE_MASK_COLOR = ((COLOR_WRITE_MASK_RED | COLOR_WRITE_MASK_GREEN) | COLOR_WRITE_MASK_BLUE),
        COLOR_WRITE_MASK_ALL = (((COLOR_WRITE_MASK_RED | COLOR_WRITE_MASK_GREEN) | COLOR_WRITE_MASK_BLUE) | COLOR_WRITE_MASK_ALPHA)
    };

    // @NOTE revisit defaults
    struct BlendStateDesc
    {
        // @TODO independent blend
        bool            alphaToCoverage = false;    // @NOTE no-op atm
        bool            enableBlend     = false;
        BlendFactor     srcBlend        = BlendFactor::BLEND_ONE; 
        BlendFactor     dstBlend        = BlendFactor::BLEND_ZERO;
        BlendOp         blendOp         = BlendOp::BLEND_OP_ADD;
        BlendFactor     srcBlendAlpha   = BlendFactor::BLEND_ONE;
        BlendFactor     dstBlendAlpha   = BlendFactor::BLEND_ZERO;
        BlendOp         blendOpAlpha    = BlendOp::BLEND_OP_ADD;
        uint8_t         writeMask       = COLOR_WRITE_MASK_ALL;
        
        float           color[4]        = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    enum class FillMode : uint8_t
    {
        FILL_WIREFRAME,
        FILL_SOLID
    };

    enum class CullMode : uint8_t
    {
        CULL_NONE,
        CULL_FRONT,
        CULL_BACK
    };

    enum class CullOrder : uint8_t
    {
        CULL_ORDER_CLOCKWISE,
        CULL_ORDER_CCLOCKWISE
    };

    struct RasterizerStateDesc
    {
        FillMode    fillMode    = FillMode::FILL_SOLID;
        CullMode    cullMode    = CullMode::CULL_BACK;
        CullOrder   cullOrder   = CullOrder::CULL_ORDER_CLOCKWISE;
        uint32_t    depthBias = 0;
        float       depthBiasClamp = 0.0f;
        float       slopeScaledDepthBias = 0.0f;
        bool        enableDepthClip = true;
        bool        enableScissor = false;
        bool        enableMultisample = false;
        bool        enableAALine = false;
    };

    enum class CompareFunc : uint8_t 
    {
        COMPARE_NEVER,
        COMPARE_LESS,
        COMPARE_EQUAL,
        COMPARE_LESS_EQUAL,
        COMPARE_GREATER,
        COMPARE_NOT_EQUAL,
        COMPARE_GREATER_EQUAL,
        COMPARE_ALWAYS
    };

    enum class DepthWriteMask : uint8_t
    {
        DEPTH_WRITE_MASK_ZERO,
        DEPTH_WRITE_MASK_ALL
    };

    enum class StencilOp : uint8_t
    {
        STENCIL_OP_KEEP,
        STENCIL_OP_ZERO,
        STENCIL_OP_REPLACE,
        STENCIL_OP_INCR_SAT,
        STENCIL_OP_DECR_SAT,
        STENCIL_OP_INVERT,
        STENCIL_OP_INCR,
        STENCIL_OP_DECR
    };

    struct DepthStencilOpDesc
    {
        StencilOp   stencilFailOp = StencilOp::STENCIL_OP_KEEP;
        StencilOp   stencilDepthFailOp = StencilOp::STENCIL_OP_KEEP;
        StencilOp   stencilPassOp = StencilOp::STENCIL_OP_KEEP;
        CompareFunc stencilFunc = CompareFunc::COMPARE_ALWAYS;
    };

    struct DepthStencilStateDesc
    {
        bool                enableDepth         = true;
        bool                enableStencil       = false;
        DepthWriteMask      depthWriteMask      = DepthWriteMask::DEPTH_WRITE_MASK_ALL;
        CompareFunc         depthFunc           = CompareFunc::COMPARE_LESS;
        uint8_t             stencilReadMask     = 0xff;
        uint8_t             stencilWriteMask    = 0xff;
        DepthStencilOpDesc  frontFace;
        DepthStencilOpDesc  backFace;
    };

    struct PipelineStateDesc
    {
        Shader      vertexShader;
        Shader      pixelShader;
        Shader      geometryShader;
        Shader      hullShader;
        Shader      domainShader;

        VertexLayoutDesc vertexLayout;

        PrimitiveType   primitiveType = PrimitiveType::_DEFAULT;
        IndexFormat     indexFormat = IndexFormat::INDEX_FORMAT_NONE;

        BlendStateDesc          blendState;
        RasterizerStateDesc     rasterState;
        DepthStencilStateDesc   depthStencilState;
        // @TODO
    };

    

    
    struct ShaderDesc
    {
        ShaderType  type        = ShaderType::_DEFAULT;
        char*       code        = nullptr;
        size_t      codeSize    = 0;
    };


    struct AttachmentDesc
    {
        Image       image;
        uint16_t    mipmapLevel     = 0;
        uint16_t    slice           = 0;
    };

    struct RenderPassDesc
    {
        AttachmentDesc  colorAttachments[GFX_MAX_COLOR_ATTACHMENTS];
        AttachmentDesc  depthStencilAttachment;
    };

    enum class Action : uint8_t
    {
        _DEFAULT = 0,
        ACTION_CLEAR,
        ACTION_LOAD,
        ACTION_DONTCARE
    };

    struct ColorAttachmentAction
    {
        Action  action      = Action::_DEFAULT;
        float   color[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };
    };

    struct DepthAttachmentAction
    {
        Action  action  = Action::_DEFAULT;
        float   value   = 1.0f;
    };

    struct StencilAttachmentAction
    {
        Action  action  = Action::_DEFAULT;
        uint8_t value   = 0;
    };

    struct RenderPassAction
    {
        ColorAttachmentAction   colors[GFX_MAX_COLOR_ATTACHMENTS];
        DepthAttachmentAction   depth;
        StencilAttachmentAction stencil;
    };

    struct DrawCall
    {
        PipelineState pipelineState;

        uint32_t numElements = 0;
        uint32_t elementOffset = 0;
        uint32_t numInstances = 1;

        uint32_t startVertexLocation = 0;
        uint32_t startInstanceLocation = 0;
        
        uint32_t vertexOffsets[GFX_MAX_VERTEX_STREAMS];
        uint32_t vertexStrides[GFX_MAX_VERTEX_STREAMS];
        
        Buffer  vertexBuffers[GFX_MAX_VERTEX_STREAMS];
        Buffer  indexBuffer;

        Image   vsImageInputs[GFX_MAX_IMAGE_INPUTS_PER_STAGE];
        Image   psImageInputs[GFX_MAX_IMAGE_INPUTS_PER_STAGE];
        Image   gsImageInputs[GFX_MAX_IMAGE_INPUTS_PER_STAGE];
        Image   hsImageInputs[GFX_MAX_IMAGE_INPUTS_PER_STAGE];
        Image   dsImageInputs[GFX_MAX_IMAGE_INPUTS_PER_STAGE];

        Buffer  vsConstantInputs[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];
        Buffer  psConstantInputs[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];
        Buffer  gsConstantInputs[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];
        Buffer  hsConstantInputs[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];
        Buffer  dsConstantInputs[GFX_MAX_CONSTANT_INPUTS_PER_STAGE];
    };
    
    struct CommandBufferDesc
    {
        // @TODO
    };

    struct SwapChainDesc
    {
        uint8_t     bufferCountHint = 2;    // 2 indicates double buffering, 3 indicates triple buffering
        uint32_t    width = 0;
        uint32_t    height = 0;
        void*       window = nullptr;
        // @TODO multisampling
    };

    Buffer CreateBuffer(Device* device, BufferDesc* desc);
    Image CreateImage(Device* device, ImageDesc* desc);
    PipelineState CreatePipelineState(Device* device, PipelineStateDesc* desc);
    Shader CreateShader(Device* device, ShaderDesc* desc);
    RenderPass CreateRenderPass(Device* device, RenderPassDesc* desc);
    CommandBuffer CreateCommandBuffer(Device* device, CommandBufferDesc* desc);
    SwapChain CreateSwapChain(Device* device, SwapChainDesc* desc);

    void DestroyBuffer(Device* device, Buffer buffer);
    void DestroyImage(Device* device, Image image);

    BufferDesc GetBufferDesc(Device* device, Buffer buffer);
    ImageDesc GetImageDesc(Device* device, Image image);
    PipelineStateDesc GetPipelineStateDesc(Device* device, PipelineState pipelineState);
    ShaderDesc GetShaderDesc(Device* device, Shader shader);
    RenderPass GetRenderPassDesc(Device* device, RenderPass pass);
    SwapChainDesc GetSwapChainDesc(Device* device, SwapChain swapChain);


    void ResizeSwapChain(Device* device, SwapChain swapChain, uint32_t width, uint32_t height);

    CommandBuffer   GetImmediateCommandBuffer(Device* device);

    struct Viewport
    {
        float width;
        float height;
    };

    struct Rect
    {
        uint32_t left = 0;
        uint32_t top = 0;
        uint32_t right = 0;
        uint32_t bottom = 0;
    };

    void BeginDefaultRenderPass(Device* device, CommandBuffer cmdBuffer, SwapChain swapChain, RenderPassAction* action);
    void BeginRenderPass(Device* device, CommandBuffer cmdBuffer, RenderPass renderPass, RenderPassAction* action);
    // @NOTE might regret default arguments for viewport, scissor rect
    void SubmitDrawCall(Device* device, CommandBuffer cmdBuffer, DrawCall* drawCall, Viewport* viewport = nullptr, Rect* scissorRect = nullptr);
    void EndRenderPass(Device* device, CommandBuffer cmdBuffer);

    void PresentSwapChain(Device* device, SwapChain swapChain);


    void*   MapBuffer(Device* device, Buffer buffer, MapType mapType);
    void    UnmapBuffer(Device* device, Buffer buffer);

}
