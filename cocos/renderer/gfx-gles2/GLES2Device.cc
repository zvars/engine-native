#include "GLES2Std.h"
#include "GLES2Device.h"
#include "GLES2StateCache.h"
#include "GLES2Context.h"
#include "GLES2Window.h"
#include "GLES2Queue.h"
#include "GLES2CommandAllocator.h"
#include "GLES2CommandBuffer.h"
#include "GLES2Buffer.h"
#include "GLES2Texture.h"
#include "GLES2TextureView.h"
#include "GLES2Sampler.h"
#include "GLES2Shader.h"
#include "GLES2InputAssembler.h"
#include "GLES2RenderPass.h"
#include "GLES2Framebuffer.h"
#include "GLES2BindingLayout.h"
#include "GLES2PipelineLayout.h"
#include "GLES2PipelineState.h"

NS_CC_BEGIN

GLES2Device::GLES2Device()
{
}

GLES2Device::~GLES2Device()
{
}

bool GLES2Device::initialize(const GFXDeviceInfo& info)
{
    _api = GFXAPI::GLES2;
    _width = info.width;
    _height = info.height;
    _nativeWidth = info.native_width;
    _nativeHeight = info.native_height;
    _windowHandle = info.window_handle;

    state_cache = CC_NEW(GLES2StateCache);

    GFXContextInfo ctx_info;
    ctx_info.window_handle = _windowHandle;
    ctx_info.shared_ctx = info.shared_ctx;

    _context = CC_NEW(GLES2Context(this));
    if (!_context->Initialize(ctx_info))
    {
        destroy();
        return false;
    }

    String extStr = (const char*)glGetString(GL_EXTENSIONS);
    extensions_ = StringUtil::Split(extStr, " ");

    _features[(int)GFXFeature::TEXTURE_FLOAT] = true;
    _features[(int)GFXFeature::TEXTURE_HALF_FLOAT] = true;
    _features[(int)GFXFeature::FORMAT_R11G11B10F] = true;
    _features[(int)GFXFeature::FORMAT_D24S8] = true;
    _features[(int)GFXFeature::MSAA] = true;

    if (checkExtension("color_buffer_float"))
        _features[(int)GFXFeature::COLOR_FLOAT] = true;
    
    if (checkExtension("color_buffer_half_float"))
        _features[(int)GFXFeature::COLOR_HALF_FLOAT] = true;
    
    if (checkExtension("texture_float_linear"))
        _features[(int)GFXFeature::TEXTURE_FLOAT_LINEAR] = true;
    
    if (checkExtension("texture_half_float_linear"))
        _features[(int)GFXFeature::TEXTURE_HALF_FLOAT_LINEAR] = true;
    

    use_vao_ = checkExtension("GL_OES_depth_texture");
    use_draw_instanced_ = checkExtension("draw_instanced_");
    use_instanced_arrays_ = checkExtension("instanced_arrays");
    use_discard_framebuffer_ = checkExtension("discard_framebuffer");

    String compressed_fmts;

    if (checkExtension("compressed_ETC1"))
    {
        _features[(int)GFXFeature::FORMAT_ETC1] = true;
        compressed_fmts += "etc1 ";
    }

    _features[(int)GFXFeature::FORMAT_ETC2] = true;
    compressed_fmts += "etc2 ";

    if (checkExtension("texture_compression_pvrtc"))
    {
        _features[(int)GFXFeature::FORMAT_PVRTC] = true;
        compressed_fmts += "pvrtc ";
    }

    if (checkExtension("texture_compression_astc"))
    {
        _features[(int)GFXFeature::FORMAT_ASTC] = true;
        compressed_fmts += "astc ";
    }

    _renderer = (const char*)glGetString(GL_RENDERER);
    _vendor = (const char*)glGetString(GL_VENDOR);
    _version = (const char*)glGetString(GL_VERSION);

    CC_LOG_INFO("RENDERER: %s", _renderer.c_str());
    CC_LOG_INFO("VENDOR: %s", _vendor.c_str());
    CC_LOG_INFO("VERSION: %s", _version.c_str());
    CC_LOG_INFO("SCREEN_SIZE: %d x %d", _width, _height);
    CC_LOG_INFO("NATIVE_SIZE: %d x %d", _nativeWidth, _nativeHeight);
    CC_LOG_INFO("USE_VAO: %s", use_vao_ ? "true" : "false");
    CC_LOG_INFO("COMPRESSED_FORMATS: %s", compressed_fmts.c_str());

    GFXWindowInfo window_info;
    window_info.color_fmt = _context->color_fmt();
    window_info.depth_stencil_fmt = _context->depth_stencil_fmt();
    window_info.is_offscreen = false;
    _window = createWindow(window_info);

    GFXQueueInfo queue_info;
    queue_info.type = GFXQueueType::GRAPHICS;
    _queue = createQueue(queue_info);

    GFXCommandAllocatorInfo cmd_alloc_info;
    _cmdAllocator = createCommandAllocator(cmd_alloc_info);

    return true;
}

void GLES2Device::destroy()
{
    CC_SAFE_DESTROY(_cmdAllocator);
    CC_SAFE_DESTROY(_queue);
    CC_SAFE_DESTROY(_window);
    CC_SAFE_DESTROY(_context);
    CC_SAFE_DELETE(state_cache);
}

void GLES2Device::resize(uint width, uint height)
{
    _width = width;
    _height = height;
    _window->Resize(width, height);
}

void GLES2Device::present()
{
    ((GLES2CommandAllocator*)_cmdAllocator)->releaseCmds();
    GLES2Queue* queue = (GLES2Queue*)_queue;
    _numDrawCalls += queue->_numDrawCalls;
    _numTriangles += queue->_numTriangles;

    _context->Present();

    // Clear queue stats
    queue->_numDrawCalls = 0;
    queue->_numTriangles = 0;
}

GFXWindow* GLES2Device::createWindow(const GFXWindowInfo& info)
{
    GFXWindow* gfx_window = CC_NEW(GLES2Window(this));
    if (gfx_window->Initialize(info))
        return gfx_window;

    CC_SAFE_DESTROY(gfx_window);
    return nullptr;
}

GFXQueue* GLES2Device::createQueue(const GFXQueueInfo& info)
{
    GFXQueue* gfx_queue = CC_NEW(GLES2Queue(this));
    if (gfx_queue->Initialize(info))
        return gfx_queue;

    CC_SAFE_DESTROY(gfx_queue);
    return nullptr;
}

GFXCommandAllocator* GLES2Device::createCommandAllocator(const GFXCommandAllocatorInfo& info)
{
    GFXCommandAllocator* gfx_cmd_allocator = CC_NEW(GLES2CommandAllocator(this));
    if (gfx_cmd_allocator->initialize(info))
        return gfx_cmd_allocator;

    CC_SAFE_DESTROY(gfx_cmd_allocator);

    return nullptr;
}

GFXCommandBuffer* GLES2Device::createCommandBuffer(const GFXCommandBufferInfo& info)
{
    GFXCommandBuffer* gfx_cmd_buff = CC_NEW(GLES2CommandBuffer(this));
    if (gfx_cmd_buff->Initialize(info))
        return gfx_cmd_buff;

    CC_SAFE_DESTROY(gfx_cmd_buff);
    return nullptr;
}

GFXBuffer* GLES2Device::createBuffer(const GFXBufferInfo& info)
{
    GFXBuffer* gfx_buffer = CC_NEW(GLES2Buffer(this));
    if (gfx_buffer->Initialize(info))
        return gfx_buffer;

    CC_SAFE_DESTROY(gfx_buffer);
    return nullptr;
}

GFXTexture* GLES2Device::createTexture(const GFXTextureInfo& info)
{
    GFXTexture* gfx_texture = CC_NEW(GLES2Texture(this));
    if (gfx_texture->Initialize(info))
        return gfx_texture;

    CC_SAFE_DESTROY(gfx_texture);
    return nullptr;
}

GFXTextureView* GLES2Device::createTextureView(const GFXTextureViewInfo& info)
{
    GFXTextureView* gfx_tex_view = CC_NEW(GLES2TextureView(this));
    if (gfx_tex_view->Initialize(info))
        return gfx_tex_view;

    CC_SAFE_DESTROY(gfx_tex_view);
    return nullptr;
}

GFXSampler* GLES2Device::createSampler(const GFXSamplerInfo& info)
{
    GFXSampler* gfx_sampler = CC_NEW(GLES2Sampler(this));
    if (gfx_sampler->Initialize(info))
        return gfx_sampler;

    CC_SAFE_DESTROY(gfx_sampler);
    return nullptr;
}

GFXShader* GLES2Device::createShader(const GFXShaderInfo& info)
{
    GFXShader* gfx_shader = CC_NEW(GLES2Shader(this));
    if (gfx_shader->Initialize(info))
        return gfx_shader;

    CC_SAFE_DESTROY(gfx_shader);
    return nullptr;
}

GFXInputAssembler* GLES2Device::createInputAssembler(const GFXInputAssemblerInfo& info)
{
    GFXInputAssembler* gfx_input_assembler = CC_NEW(GLES2InputAssembler(this));
    if (gfx_input_assembler->Initialize(info))
        return gfx_input_assembler;

    CC_SAFE_DESTROY(gfx_input_assembler);
    return nullptr;
}

GFXRenderPass* GLES2Device::createRenderPass(const GFXRenderPassInfo& info)
{
    GFXRenderPass* gfx_render_pass = CC_NEW(GLES2RenderPass(this));
    if (gfx_render_pass->Initialize(info))
        return gfx_render_pass;

    CC_SAFE_DESTROY(gfx_render_pass);
    return nullptr;
}

GFXFramebuffer* GLES2Device::createFramebuffer(const GFXFramebufferInfo& info)
{
    GFXFramebuffer* gfx_framebuffer = CC_NEW(GLES2Framebuffer(this));
    if (gfx_framebuffer->Initialize(info))
        return gfx_framebuffer;

    CC_SAFE_DESTROY(gfx_framebuffer);
    return nullptr;
}

GFXBindingLayout* GLES2Device::createBindingLayout(const GFXBindingLayoutInfo& info)
{
    GFXBindingLayout* gfx_binding_layout = CC_NEW(GLES2BindingLayout(this));
    if (gfx_binding_layout->Initialize(info))
        return gfx_binding_layout;

    CC_SAFE_DESTROY(gfx_binding_layout);
    return nullptr;
}

GFXPipelineState* GLES2Device::createPipelineState(const GFXPipelineStateInfo& info)
{
    GFXPipelineState* pipelineState = CC_NEW(GLES2PipelineState(this));
    if (pipelineState->Initialize(info))
        return pipelineState;

    CC_SAFE_DESTROY(pipelineState);
    return nullptr;
}

GFXPipelineLayout* GLES2Device::createPipelineLayout(const GFXPipelineLayoutInfo& info)
{
    GFXPipelineLayout* layout = CC_NEW(GLES2PipelineLayout(this));
    if (layout->Initialize(info))
        return layout;

    CC_SAFE_DESTROY(layout);
    return nullptr;
}

void GLES2Device::copyBuffersToTexture(GFXBuffer *src, GFXTexture *dst, const GFXBufferTextureCopyList &regions)
{
    GLES2CmdFuncCopyBuffersToTexture(this, &((GLES2Buffer*)src)->gpu_buffer()->buffer, 1, ((GLES2Texture*)dst)->gpu_texture(), regions);
}

NS_CC_END