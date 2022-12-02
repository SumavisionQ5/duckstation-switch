#include "gpu_hw_deko3d.h"
#include "common/assert.h"
#include "common/deko3d/context.h"
#include "common/deko3d/shader_cache.h"
#include "common/deko3d/util.h"
#include "common/log.h"
#include "common/scoped_guard.h"
#include "common/timer.h"
#include "gpu_hw_shadergen.h"
#include "host_display.h"
#include "system.h"
#include "util/state_wrapper.h"
#include <switch.h>
Log_SetChannel(GPU_HW_Deko3D);

GPU_HW_Deko3D::GPU_HW_Deko3D() = default;

GPU_HW_Deko3D::~GPU_HW_Deko3D()
{
  if (g_host_display)
  {
    g_host_display->ClearDisplayTexture();
    ResetGraphicsAPIState();
  }

  DestroyResources();
}

GPURenderer GPU_HW_Deko3D::GetRendererType() const
{
  return GPURenderer::HardwareDeko3D;
}

bool GPU_HW_Deko3D::Initialize()
{
  m_supports_dual_source_blend = true;
  m_supports_per_sample_shading = true;
  m_supports_disable_color_perspective = true;
  m_max_resolution_scale = 4096 / VRAM_WIDTH;

  if (!Host::AcquireHostDisplay(RenderAPI::Deko3D))
  {
    Log_ErrorPrintf("Host render API is incompatible");
    return false;
  }

  Assert(g_deko3d_shader_cache);

  if (!GPU_HW::Initialize())
    return false;

  if (!CreateSamplers())
  {
    Log_ErrorPrintf("Failed to create samplers");
    return false;
  }

  if (!CreateVertexBuffer())
  {
    Log_ErrorPrintf("Failed to create vertex buffer");
    return false;
  }

  if (!CreateUniformBuffer())
  {
    Log_ErrorPrintf("Failed to create uniform buffer");
    return false;
  }

  if (!CreateTextureBuffer())
  {
    Log_ErrorPrintf("Failed to create texture buffer");
    return false;
  }

  if (!CreateFramebuffer())
  {
    Log_ErrorPrintf("Failed to create framebuffer");
    return false;
  }

  if (!CompileShaders())
  {
    Log_ErrorPrintf("Failed to compile shaders");
    return false;
  }

  UpdateDepthBufferFromMaskBit();
  RestoreGraphicsAPIStateEx(true, false);
  return true;
}

void GPU_HW_Deko3D::Reset(bool clear_vram)
{
  GPU_HW::Reset(clear_vram);

  if (clear_vram)
    ClearFramebuffer();
}

bool GPU_HW_Deko3D::DoState(StateWrapper& sw, GPUTexture** host_texture, bool update_display)
{
  if (host_texture)
  {
    dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();
    dk::ImageView vramView{m_vram_texture.GetImage()};
    if (sw.IsReading())
    {
      Deko3D::Texture* tex = static_cast<Deko3D::Texture*>(*host_texture);
      if (tex->GetWidth() != m_vram_texture.GetWidth() || tex->GetHeight() != m_vram_texture.GetHeight() ||
          tex->GetSamples() != m_vram_texture.GetSamples())
      {
        return false;
      }

      dk::ImageView srcView{tex->GetImage()};
      cmdbuf.copyImage(srcView, {0, 0, 0, tex->GetWidth(), tex->GetHeight(), 1}, vramView,
                       {0, 0, 0, tex->GetWidth(), tex->GetHeight(), 1});
    }
    else
    {
      Deko3D::Texture* tex = static_cast<Deko3D::Texture*>(*host_texture);
      if (!tex || tex->GetWidth() != m_vram_texture.GetWidth() || tex->GetHeight() != m_vram_texture.GetHeight() ||
          tex->GetSamples() != static_cast<u32>(m_vram_texture.GetSamples()))
      {
        delete tex;

        tex = static_cast<Deko3D::Texture*>(g_host_display
                                              ->CreateTexture(m_vram_texture.GetWidth(), m_vram_texture.GetHeight(), 1,
                                                              1, m_vram_texture.GetSamples(), GPUTexture::Format::RGBA8,
                                                              nullptr, 0, false)
                                              .release());
        *host_texture = tex;
        if (!tex)
          return false;
      }

      if (tex->GetWidth() != m_vram_texture.GetWidth() || tex->GetHeight() != m_vram_texture.GetHeight() ||
          tex->GetSamples() != m_vram_texture.GetSamples())
      {
        return false;
      }

      dk::ImageView dstView{tex->GetImage()};
      cmdbuf.copyImage(vramView, {0, 0, 0, tex->GetWidth(), tex->GetHeight(), 1}, dstView,
                       {0, 0, 0, tex->GetWidth(), tex->GetHeight(), 1});
    }
  }

  return GPU_HW::DoState(sw, host_texture, update_display);
}

void GPU_HW_Deko3D::ResetGraphicsAPIState()
{
  GPU_HW::ResetGraphicsAPIState();
}

void GPU_HW_Deko3D::RestoreGraphicsAPIStateEx(bool restore_rt, bool returning_from_known_state)
{
  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();
  Deko3D::MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();

  if (restore_rt)
  {
    dk::ImageView vramTextureView{m_vram_texture.GetImage()};
    dk::ImageView vramDepthTextureView{m_vram_depth_texture.GetImage()};
    cmdbuf.bindRenderTargets({&vramTextureView}, {&vramDepthTextureView});
  }

  SetDepthFunc(cmdbuf, !returning_from_known_state);
  SetBlendMode(cmdbuf, m_blending_enabled, m_subtractive_blending, !returning_from_known_state);

  if (!returning_from_known_state)
  {
    cmdbuf.bindSamplerDescriptorSet(heap.GpuAddr(m_sampler_memory), SAMPLERS_COUNT);
    cmdbuf.bindImageDescriptorSet(heap.GpuAddr(m_image_descriptor_memory), IMAGES_COUNT);

    cmdbuf.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));
  }

  cmdbuf.bindVtxBuffer(0, m_vertex_stream_buffer.GetPointer(), m_vertex_stream_buffer.GetCurrentSize());
  cmdbuf.bindVtxBufferState({{sizeof(BatchVertex), 0}});
  cmdbuf.bindVtxAttribState({{0, 0, offsetof(BatchVertex, x), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
                             {0, 0, offsetof(BatchVertex, color), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0},
                             {0, 0, offsetof(BatchVertex, u), DkVtxAttribSize_1x32, DkVtxAttribType_Uint, 0},
                             {0, 0, offsetof(BatchVertex, texpage), DkVtxAttribSize_1x32, DkVtxAttribType_Uint, 0},
                             {0, 0, offsetof(BatchVertex, uv_limits), DkVtxAttribSize_4x8, DkVtxAttribType_Unorm, 0}});

  cmdbuf.bindTextures(DkStage_Fragment, 0, {dkMakeTextureHandle(IMAGE_VRAM_READ, SAMPLER_POINT)});

  cmdbuf.setViewports(0, {{0.f, 0.f, static_cast<float>(m_vram_texture.GetWidth()),
                           static_cast<float>(m_vram_texture.GetHeight()), 0.f, 1.f}});
  SetScissorFromDrawingArea();

  cmdbuf.bindUniformBuffer(DkStage_Vertex, 1, heap.GpuAddr(m_batch_uniform), m_batch_uniform.size);
  cmdbuf.bindUniformBuffer(DkStage_Fragment, 1, heap.GpuAddr(m_batch_uniform), m_batch_uniform.size);
}

void GPU_HW_Deko3D::RestoreGraphicsAPIState()
{
  RestoreGraphicsAPIStateEx(true, false);
}

void GPU_HW_Deko3D::UpdateSettings()
{
  GPU_HW::UpdateSettings();

  bool framebuffer_changed, shaders_changed;
  UpdateHWSettings(&framebuffer_changed, &shaders_changed);

  if (framebuffer_changed)
  {
    RestoreGraphicsAPIState();
    ReadVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT);
    ResetGraphicsAPIState();
  }

  // Everything should be finished executing before recreating resources.
  g_host_display->ClearDisplayTexture();
  g_deko3d_context->ExecuteCommandBuffer(true);

  if (framebuffer_changed)
    CreateFramebuffer();

  if (shaders_changed)
  {
    DestroyShaders();
    CompileShaders();
  }

  // this has to be done here, because otherwise we're using destroyed pipelines in the same cmdbuffer
  if (framebuffer_changed)
  {
    RestoreGraphicsAPIState();
    UpdateVRAM(0, 0, VRAM_WIDTH, VRAM_HEIGHT, m_vram_ptr, false, false);
    UpdateDepthBufferFromMaskBit();
    UpdateDisplay();
    ResetGraphicsAPIState();
  }
}

void GPU_HW_Deko3D::ExecuteCommandBuffer(bool wait_for_completion, bool restore_state)
{
  g_deko3d_context->ExecuteCommandBuffer(wait_for_completion);
  m_batch_ubo_dirty = true;
  if (restore_state)
    RestoreGraphicsAPIState();
}

bool GPU_HW_Deko3D::CompileShaders()
{
  GPU_HW_ShaderGen shadergen(g_host_display->GetRenderAPI(), m_resolution_scale, m_multisamples, m_per_sample_shading,
                             m_true_color, m_scaled_dithering, m_texture_filtering, m_using_uv_limits,
                             m_pgxp_depth_buffer, m_disable_color_perspective, m_supports_dual_source_blend);

  ShaderCompileProgressTracker progress("Compiling shaders", 2 + (4 * 9 * 2 * 2) + 2 + (2 * 2) + 4 + (2 * 3) + 1);

  for (u8 textured = 0; textured < 2; textured++)
  {
    const std::string vs = shadergen.GenerateBatchVertexShader(ConvertToBoolUnchecked(textured));

    Shader& shader = m_batch_vertex_shaders[textured];
    if (!g_deko3d_shader_cache->GetVertexShader(vs, shader.shader, shader.memory))
      return false;
    progress.Increment();
  }

  for (u8 render_mode = 0; render_mode < 4; render_mode++)
  {
    for (u8 texture_mode = 0; texture_mode < 9; texture_mode++)
    {
      for (u8 dithering = 0; dithering < 2; dithering++)
      {
        for (u8 interlacing = 0; interlacing < 2; interlacing++)
        {
          const std::string fs = shadergen.GenerateBatchFragmentShader(
            static_cast<BatchRenderMode>(render_mode), static_cast<GPUTextureMode>(texture_mode),
            ConvertToBoolUnchecked(dithering), ConvertToBoolUnchecked(interlacing));

          Shader& shader = m_batch_fragment_shaders[render_mode][texture_mode][dithering][interlacing];

          if (!g_deko3d_shader_cache->GetFragmentShader(fs, shader.shader, shader.memory))
            return false;

          progress.Increment();
        }
      }
    }
  }

  if (!g_deko3d_shader_cache->GetVertexShader(shadergen.GenerateScreenQuadVertexShader(),
                                              m_fullscreen_quad_vertex_shader.shader,
                                              m_fullscreen_quad_vertex_shader.memory))
    return false;
  if (!g_deko3d_shader_cache->GetVertexShader(shadergen.GenerateScreenQuadVertexShader(), uv_quad_vertex_shader.shader,
                                              uv_quad_vertex_shader.memory))
    return false;
  progress.Increment();

  // VRAM fill
  for (u8 wrapped = 0; wrapped < 2; wrapped++)
  {
    for (u8 interlaced = 0; interlaced < 2; interlaced++)
    {
      Shader& shader = m_vram_fill_shaders[wrapped][interlaced];
      if (!g_deko3d_shader_cache->GetFragmentShader(
            shadergen.GenerateVRAMFillFragmentShader(ConvertToBoolUnchecked(wrapped),
                                                     ConvertToBoolUnchecked(interlaced)),
            shader.shader, shader.memory))
        return false;

      progress.Increment();
    }
  }

  // VRAM read
  if (!g_deko3d_shader_cache->GetFragmentShader(shadergen.GenerateVRAMReadFragmentShader(), m_vram_read_shader.shader,
                                                m_vram_read_shader.memory))
    return false;

  // VRAM write
  if (!g_deko3d_shader_cache->GetFragmentShader(shadergen.GenerateVRAMWriteFragmentShader(false),
                                                m_vram_write_shader.shader, m_vram_write_shader.memory))
    return false;

  // VRAM update depth
  if (!g_deko3d_shader_cache->GetFragmentShader(shadergen.GenerateVRAMUpdateDepthFragmentShader(),
                                                m_vram_update_depth_shader.shader, m_vram_update_depth_shader.memory))
    return false;

  // VRAM copy
  if (!g_deko3d_shader_cache->GetFragmentShader(shadergen.GenerateVRAMCopyFragmentShader(), m_vram_copy_shader.shader,
                                                m_vram_copy_shader.memory))
    return false;

  for (u8 depth_24 = 0; depth_24 < 2; depth_24++)
  {
    for (u8 interlace_mode = 0; interlace_mode < 3; interlace_mode++)
    {
      Shader& shader = m_display_shaders[depth_24][interlace_mode];

      if (!g_deko3d_shader_cache->GetFragmentShader(
            shadergen.GenerateDisplayFragmentShader(
              ConvertToBoolUnchecked(depth_24), static_cast<InterlacedRenderMode>(interlace_mode), m_chroma_smoothing),
            shader.shader, shader.memory))
        return false;

      progress.Increment();
    }
  }

  /*if (m_downsample_mode == GPUDownsampleMode::Adaptive)
  {

  }
  else */
  if (m_downsample_mode == GPUDownsampleMode::Box)
  {
    if (!g_deko3d_shader_cache->GetFragmentShader(shadergen.GenerateBoxSampleDownsampleFragmentShader(),
                                                  m_boxsample_downsample_shader.shader,
                                                  m_boxsample_downsample_shader.memory))
      return false;
  }

  progress.Increment();

  return true;
}

void GPU_HW_Deko3D::MapBatchVertexPointer(u32 required_vertices)
{
  DebugAssert(!m_batch_start_vertex_ptr);

  const u32 required_space = required_vertices * sizeof(BatchVertex);
  if (!m_vertex_stream_buffer.ReserveMemory(required_space, sizeof(BatchVertex)))
  {
    Log_PerfPrintf("Executing command buffer while waiting for %u bytes in vertex stream buffer", required_space);
    ExecuteCommandBuffer(false, true);
    if (!m_vertex_stream_buffer.ReserveMemory(required_space, sizeof(BatchVertex)))
      Panic("Failed to reserve vertex stream buffer memory");
  }

  m_batch_start_vertex_ptr = reinterpret_cast<BatchVertex*>(m_vertex_stream_buffer.GetCurrentHostPointer());
  m_batch_current_vertex_ptr = m_batch_start_vertex_ptr;
  m_batch_end_vertex_ptr = m_batch_start_vertex_ptr + (m_vertex_stream_buffer.GetCurrentSpace() / sizeof(BatchVertex));
  m_batch_base_vertex = m_vertex_stream_buffer.GetCurrentOffset() / sizeof(BatchVertex);
}

void GPU_HW_Deko3D::UnmapBatchVertexPointer(u32 used_vertices)
{
  DebugAssert(m_batch_start_vertex_ptr);
  if (used_vertices > 0)
    m_vertex_stream_buffer.CommitMemory(used_vertices * sizeof(BatchVertex));

  m_batch_start_vertex_ptr = nullptr;
  m_batch_end_vertex_ptr = nullptr;
  m_batch_current_vertex_ptr = nullptr;
}

void GPU_HW_Deko3D::UploadUniformBuffer(const void* data, u32 data_size)
{
  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();
  Deko3D::MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();

  cmdbuf.pushConstants(heap.GpuAddr(m_batch_uniform), m_batch_uniform.size, 0, data_size, data);
}

void GPU_HW_Deko3D::DestroyShaders()
{
  Deko3D::MemoryHeap& heap = g_deko3d_context->GetShaderHeap();

  auto free_shader = [this, &heap](Shader& shader) {
    if (shader.memory.size)
    {
      heap.Free(shader.memory);
      shader.memory = {};
    }
  };

  for (u8 textured = 0; textured < 2; textured++)
    free_shader(m_batch_vertex_shaders[textured]);

  for (u8 render_mode = 0; render_mode < 4; render_mode++)
  {
    for (u8 texture_mode = 0; texture_mode < 9; texture_mode++)
    {
      for (u8 dithering = 0; dithering < 2; dithering++)
      {
        for (u8 interlacing = 0; interlacing < 2; interlacing++)
        {
          free_shader(m_batch_fragment_shaders[render_mode][texture_mode][dithering][interlacing]);
        }
      }
    }
  }

  free_shader(m_fullscreen_quad_vertex_shader);
  free_shader(uv_quad_vertex_shader);

  // VRAM fill
  for (u8 wrapped = 0; wrapped < 2; wrapped++)
  {
    for (u8 interlaced = 0; interlaced < 2; interlaced++)
    {
      free_shader(m_vram_fill_shaders[wrapped][interlaced]);
    }
  }

  // VRAM read
  free_shader(m_vram_read_shader);

  // VRAM write
  free_shader(m_vram_write_shader);

  // VRAM update depth
  free_shader(m_vram_update_depth_shader);

  // VRAM copy
  free_shader(m_vram_copy_shader);

  for (u8 depth_24 = 0; depth_24 < 2; depth_24++)
  {
    for (u8 interlace_mode = 0; interlace_mode < 3; interlace_mode++)
    {
      free_shader(m_display_shaders[depth_24][interlace_mode]);
    }
  }

  /*if (m_downsample_mode == GPUDownsampleMode::Adaptive)
  {

  }
  else */
  if (m_downsample_mode == GPUDownsampleMode::Box)
  {
    free_shader(m_boxsample_downsample_shader);
  }
}

void GPU_HW_Deko3D::DestroyResources()
{
  // Everything should be finished executing before recreating resources.
  if (g_deko3d_context)
    g_deko3d_context->ExecuteCommandBuffer(true);

  DestroyFramebuffer();

  Deko3D::MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();
  if (m_sampler_memory.size != 0)
  {
    heap.Free(m_sampler_memory);
    m_sampler_memory = {};
  }
  if (m_image_descriptor_memory.size != 0)
  {
    heap.Free(m_image_descriptor_memory);
    m_image_descriptor_memory = {};
  }

  if (m_batch_uniform.size != 0)
  {
    heap.Free(m_batch_uniform);
    m_batch_uniform = {};
  }
  if (m_other_uniforms.size != 0)
  {
    heap.Free(m_other_uniforms);
    m_other_uniforms = {};
  }

  DestroyShaders();

  m_vertex_stream_buffer.Destroy(false);
  m_texture_stream_buffer.Destroy(false);
}

bool GPU_HW_Deko3D::CreateFramebuffer()
{
  DestroyFramebuffer();

  // scale vram size to internal resolution
  const u32 texture_width = VRAM_WIDTH * m_resolution_scale;
  const u32 texture_height = VRAM_HEIGHT * m_resolution_scale;
  const DkMsMode multisamples = static_cast<DkMsMode>(__builtin_ctz(m_multisamples));
  const DkImageType multisample_image_type = multisamples != DkMsMode_1x ? DkImageType_2DMS : DkImageType_2D;

  if (!m_vram_texture.Create(texture_width, texture_height, 1, 1, DkImageFormat_RGBA8_Unorm, multisamples,
                             multisample_image_type,
                             DkImageFlags_UsageRender | DkImageFlags_HwCompression | DkImageFlags_Usage2DEngine) ||
      !m_vram_depth_texture.Create(texture_width, texture_height, 1, 1, DkImageFormat_Z16, multisamples,
                                   multisample_image_type, DkImageFlags_UsageRender | DkImageFlags_HwCompression) ||
      !m_vram_read_texture.Create(texture_width, texture_height, 1, 1, DkImageFormat_RGBA8_Unorm, DkMsMode_1x,
                                  DkImageType_2D, DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine) ||
      !m_vram_readback_texture.Create(VRAM_WIDTH, VRAM_HEIGHT, 1, 1, DkImageFormat_RGBA8_Unorm, DkMsMode_1x,
                                      DkImageType_2D, DkImageFlags_UsageRender) ||
      !m_display_texture.Create(GPU_MAX_DISPLAY_WIDTH * m_resolution_scale, GPU_MAX_DISPLAY_HEIGHT * m_resolution_scale,
                                1, 1, DkImageFormat_RGBA8_Unorm, DkMsMode_1x, DkImageType_2D, DkImageFlags_UsageRender))
  {
    return false;
  }

  if (m_downsample_mode == GPUDownsampleMode::Box)
  {
    if (!m_downsample_texture.Create(VRAM_WIDTH, VRAM_HEIGHT, 1, 1, DkImageFormat_RGBA8_Unorm, DkMsMode_1x,
                                     DkImageType_2D, DkImageFlags_UsageRender))
    {
      return false;
    }
  }

  Deko3D::MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();
  m_image_descriptor_memory = heap.Alloc(sizeof(dk::ImageDescriptor) * IMAGES_COUNT, DK_IMAGE_DESCRIPTOR_ALIGNMENT);

  dk::ImageView vramView{m_vram_texture.GetImage()};
  dk::ImageView vramDepthView{m_vram_texture.GetImage()};
  dk::ImageView vramReadView{m_vram_texture.GetImage()};
  dk::ImageView vramReadbackView{m_vram_texture.GetImage()};
  dk::ImageView displayView{m_vram_texture.GetImage()};
  dk::ImageView textureBufferView{m_texture_buffer};

  dk::ImageDescriptor* descriptors = heap.CpuAddr<dk::ImageDescriptor>(m_image_descriptor_memory);
  descriptors[IMAGE_VRAM].initialize(vramView);
  descriptors[IMAGE_VRAM_DEPTH].initialize(vramDepthView);
  descriptors[IMAGE_VRAM_READ].initialize(vramReadView);
  descriptors[IMAGE_VRAM_READBACK].initialize(vramReadbackView);
  descriptors[IMAGE_DISPLAY].initialize(displayView);
  descriptors[IMAGE_TEXTURE_BUFFER].initialize(textureBufferView);

  ClearDisplay();
  SetFullVRAMDirtyRectangle();
  return true;
}

bool GPU_HW_Deko3D::CreateSamplers()
{
  Deko3D::MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();
  m_sampler_memory = heap.Alloc(sizeof(dk::SamplerDescriptor) * SAMPLERS_COUNT, DK_SAMPLER_DESCRIPTOR_ALIGNMENT);

  dk::SamplerDescriptor* samplers = heap.CpuAddr<dk::SamplerDescriptor>(m_sampler_memory);
  samplers[SAMPLER_POINT].initialize(dk::Sampler{}.setFilter(DkFilter_Nearest, DkFilter_Nearest));
  samplers[SAMPLER_LINEAR].initialize(dk::Sampler{}.setFilter(DkFilter_Linear, DkFilter_Linear));
  samplers[SAMPLER_TRILINEAR].initialize(dk::Sampler{}.setFilter(DkFilter_Linear, DkFilter_Linear, DkMipFilter_Linear));
  return true;
}

void GPU_HW_Deko3D::ClearFramebuffer()
{
  const float depth_clear_value = m_pgxp_depth_buffer ? 1.0f : 0.0f;

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  cmdbuf.setScissors(0, {{0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight()}});
  cmdbuf.clearColor(0, DkColorMask_RGBA, 0.f, 0.f, 0.f, 0.f);
  cmdbuf.clearDepthStencil(true, depth_clear_value, 0, 0);

  m_last_depth_z = 1.0f;
  SetFullVRAMDirtyRectangle();
  SetScissorFromDrawingArea();
}

void GPU_HW_Deko3D::DestroyFramebuffer()
{
  m_downsample_texture.Destroy(false);
  m_vram_read_texture.Destroy(false);
  m_vram_depth_texture.Destroy(false);
  m_vram_texture.Destroy(false);
  m_vram_readback_texture.Destroy(false);
  m_display_texture.Destroy(false);
}

bool GPU_HW_Deko3D::CreateVertexBuffer()
{
  if (!m_vertex_stream_buffer.Create(VERTEX_BUFFER_SIZE))
    return false;

  return true;
}

bool GPU_HW_Deko3D::CreateUniformBuffer()
{
  Deko3D::MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();

  m_batch_uniform = heap.Alloc(sizeof(BatchUBOData), DK_UNIFORM_BUF_ALIGNMENT);
  m_other_uniforms = heap.Alloc(MAX_PUSH_CONSTANTS_SIZE, DK_UNIFORM_BUF_ALIGNMENT);

  return true;
}

bool GPU_HW_Deko3D::CreateTextureBuffer()
{
  if (!m_texture_stream_buffer.Create(VRAM_UPDATE_TEXTURE_BUFFER_SIZE))
    return false;

  dk::ImageLayout layout;
  dk::ImageLayoutMaker{g_deko3d_context->GetDevice()}
    .setType(DkImageType_Buffer)
    .setDimensions(VRAM_UPDATE_TEXTURE_BUFFER_SIZE / 2)
    .setFormat(DkImageFormat_R16_Uint)
    .initialize(layout);

  DebugAssert(layout.getSize() == m_texture_stream_buffer.GetCurrentSize());

  m_texture_buffer.initialize(layout, g_deko3d_context->GetGeneralHeap().GetMemBlock(),
                              m_texture_stream_buffer.GetBuffer().offset);

  return true;
}

void GPU_HW_Deko3D::DrawBatchVertices(BatchRenderMode render_mode, u32 base_vertex, u32 num_vertices)
{
  const bool textured = (static_cast<GPUTextureMode>(m_batch.texture_mode) != GPUTextureMode::Disabled);
  Shader& vertshader = m_batch_vertex_shaders[BoolToUInt8(textured)];
  Shader& fragshader = m_batch_fragment_shaders[static_cast<u8>(render_mode)][static_cast<u8>(m_batch.texture_mode)]
                                               [BoolToUInt8(m_batch.dithering)][BoolToUInt8(m_batch.interlacing)];

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  cmdbuf.bindShaders(DkStageFlag_Vertex | DkStageFlag_Fragment, {&vertshader.shader, &fragshader.shader});

  SetBlendMode(cmdbuf, UseAlphaBlending(m_batch.transparency_mode, render_mode),
               m_batch.transparency_mode == GPUTransparencyMode::BackgroundMinusForeground);
  SetDepthFunc(cmdbuf);

  cmdbuf.draw(DkPrimitive_Triangles, num_vertices, 1, m_batch_base_vertex, 0);
}

void GPU_HW_Deko3D::DisableBlending(dk::CmdBuf cmdbuf)
{
  if (m_blending_enabled)
  {
    cmdbuf.bindColorState(dk::ColorState{});
    m_blending_enabled = false;
  }
}

void GPU_HW_Deko3D::SetBlendMode(dk::CmdBuf cmdbuf, bool enable_blending, bool subtractive_blending, bool force)
{
  if (enable_blending != m_blending_enabled || force)
  {
    m_blending_enabled = enable_blending;
    cmdbuf.bindColorState(dk::ColorState{}.setBlendEnable(0, enable_blending));
  }

  if ((enable_blending && m_subtractive_blending != subtractive_blending) || force)
  {
    m_subtractive_blending = subtractive_blending;
    cmdbuf.bindBlendStates(
      0, {dk::BlendState{}
            .setOps(subtractive_blending ? DkBlendOp_RevSub : DkBlendOp_Add, DkBlendOp_Add)
            .setFactors(DkBlendFactor_One, DkBlendFactor_Src1Alpha, DkBlendFactor_One, DkBlendFactor_Zero)});
  }
}

void GPU_HW_Deko3D::SetDepthFunc(dk::CmdBuf cmdbuf, bool force)
{
  SetDepthTest(cmdbuf, true,
               m_batch.use_depth_buffer ? DkCompareOp_Lequal :
                                          (m_batch.check_mask_before_draw ? DkCompareOp_Gequal : DkCompareOp_Always),
               force);
}

void GPU_HW_Deko3D::SetDepthTest(dk::CmdBuf cmdbuf, bool enable, DkCompareOp func, bool force)
{
  if (!force && (m_current_depth_state.depthTestEnable == enable && m_current_depth_state.depthCompareOp == func))
    return;

  m_current_depth_state.setDepthWriteEnable(true);
  m_current_depth_state.setDepthTestEnable(enable);
  m_current_depth_state.setDepthCompareOp(func);
  cmdbuf.bindDepthStencilState(m_current_depth_state);
}

void GPU_HW_Deko3D::SetScissorFromDrawingArea()
{
  int left, top, right, bottom;
  CalcScissorRect(&left, &top, &right, &bottom);

  g_deko3d_context->GetCmdBuf().setScissors(0, {{static_cast<u32>(left), static_cast<u32>(top),
                                                 static_cast<u32>(right - left), static_cast<u32>(bottom - top)}});
}

void GPU_HW_Deko3D::PushOtherUniform(dk::CmdBuf cmdbuf, DkStage stage, const void* data, u32 data_size)
{
  Deko3D::MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();

  cmdbuf.bindUniformBuffer(DkStage_Fragment, 1, heap.GpuAddr(m_other_uniforms), data_size);
  cmdbuf.pushConstants(heap.GpuAddr(m_other_uniforms), m_other_uniforms.size, 0, data_size, data);
}

void GPU_HW_Deko3D::ClearDisplay()
{
  GPU_HW::ClearDisplay();

  g_host_display->ClearDisplayTexture();

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  dk::ImageView displayTextureView{m_display_texture.GetImage()};
  cmdbuf.bindRenderTargets({&displayTextureView});
  cmdbuf.setScissors(0, {{0, 0, m_display_texture.GetWidth(), m_display_texture.GetHeight()}});
  cmdbuf.clearColor(0, DkColorMask_RGBA, 0.f, 0.f, 0.f, 1.f);

  SetScissorFromDrawingArea();
}

void GPU_HW_Deko3D::UpdateDisplay()
{
  GPU_HW::UpdateDisplay();

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  if (g_settings.debugging.show_vram)
  {
    if (IsUsingMultisampling())
    {
      if (m_vram_dirty_rect.Intersects(
            Common::Rectangle<u32>::FromExtents(m_crtc_state.display_vram_left, m_crtc_state.display_vram_top,
                                                m_crtc_state.display_vram_width, m_crtc_state.display_vram_height)))
      {
        UpdateVRAMReadTexture();
      }

      g_host_display->SetDisplayTexture(&m_vram_read_texture, 0, 0, m_vram_read_texture.GetWidth(),
                                        m_vram_read_texture.GetHeight());
    }
    else
    {
      g_host_display->SetDisplayTexture(&m_vram_texture, 0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());
    }
    g_host_display->SetDisplayParameters(VRAM_WIDTH, VRAM_HEIGHT, 0, 0, VRAM_WIDTH, VRAM_HEIGHT,
                                         static_cast<float>(VRAM_WIDTH) / static_cast<float>(VRAM_HEIGHT));
  }
  else
  {
    g_host_display->SetDisplayParameters(m_crtc_state.display_width, m_crtc_state.display_height,
                                         m_crtc_state.display_origin_left, m_crtc_state.display_origin_top,
                                         m_crtc_state.display_vram_width, m_crtc_state.display_vram_height,
                                         GetDisplayAspectRatio());

    const u32 resolution_scale = m_GPUSTAT.display_area_color_depth_24 ? 1 : m_resolution_scale;
    const u32 vram_offset_x = m_crtc_state.display_vram_left;
    const u32 vram_offset_y = m_crtc_state.display_vram_top;
    const u32 scaled_vram_offset_x = vram_offset_x * resolution_scale;
    const u32 scaled_vram_offset_y = vram_offset_y * resolution_scale;
    const u32 display_width = m_crtc_state.display_vram_width;
    const u32 display_height = m_crtc_state.display_vram_height;
    const u32 scaled_display_width = display_width * resolution_scale;
    const u32 scaled_display_height = display_height * resolution_scale;
    const InterlacedRenderMode interlaced = GetInterlacedRenderMode();

    if (IsDisplayDisabled())
    {
      g_host_display->ClearDisplayTexture();
    }
    else if (!m_GPUSTAT.display_area_color_depth_24 && interlaced == InterlacedRenderMode::None &&
             !IsUsingMultisampling() && (scaled_vram_offset_x + scaled_display_width) <= m_vram_texture.GetWidth() &&
             (scaled_vram_offset_y + scaled_display_height) <= m_vram_texture.GetHeight())
    {
      if (IsUsingDownsampling())
      {
        DownsampleFramebuffer(m_vram_texture, scaled_vram_offset_x, scaled_vram_offset_y, scaled_display_width,
                              scaled_display_height);
      }
      else
      {
        g_host_display->SetDisplayTexture(&m_vram_texture, scaled_vram_offset_x, scaled_vram_offset_y,
                                          scaled_display_width, scaled_display_height);
      }
    }
    else
    {
      cmdbuf.barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);

      Deko3D::Util::SetViewportAndScissor(cmdbuf, 0, 0, scaled_display_width, scaled_display_height);
      SetDepthTest(cmdbuf, false, DkCompareOp_Always);
      DisableBlending(cmdbuf);

      cmdbuf.bindShaders(
        DkStageFlag_Vertex | DkStageFlag_Fragment,
        {&m_fullscreen_quad_vertex_shader.shader,
         &m_display_shaders[BoolToUInt8(m_GPUSTAT.display_area_color_depth_24)][static_cast<u8>(interlaced)].shader});

      dk::ImageView displayView{m_display_texture.GetImage()};
      cmdbuf.bindRenderTargets({&displayView});
      if (interlaced == InterlacedRenderMode::None)
        cmdbuf.discardColor(0);

      const u32 reinterpret_field_offset = (interlaced != InterlacedRenderMode::None) ? GetInterlacedDisplayField() : 0;
      const u32 reinterpret_start_x = m_crtc_state.regs.X * resolution_scale;
      const u32 reinterpret_crop_left = (m_crtc_state.display_vram_left - m_crtc_state.regs.X) * resolution_scale;
      const u32 uniforms[4] = {reinterpret_start_x, scaled_vram_offset_y + reinterpret_field_offset,
                               reinterpret_crop_left, reinterpret_field_offset};
      PushOtherUniform(cmdbuf, DkStage_Fragment, uniforms, sizeof(uniforms));

      Assert(scaled_display_width <= m_display_texture.GetWidth() &&
             scaled_display_height <= m_display_texture.GetHeight());

      cmdbuf.bindTextures(DkStage_Fragment, 0, {dkMakeTextureHandle(IMAGE_VRAM, SAMPLER_POINT)});
      cmdbuf.bindVtxAttribState({});

      cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);

      if (IsUsingDownsampling())
      {
        DownsampleFramebuffer(m_display_texture, 0, 0, scaled_display_width, scaled_display_height);
      }
      else
      {
        g_host_display->SetDisplayTexture(&m_display_texture, 0, 0, scaled_display_width, scaled_display_height);
      }

      RestoreGraphicsAPIStateEx(true, true);
    }
  }
}

void GPU_HW_Deko3D::ReadVRAM(u32 x, u32 y, u32 width, u32 height)
{
  if (IsUsingSoftwareRendererForReadbacks())
  {
    ReadSoftwareRendererVRAM(x, y, width, height);
    return;
  }

  // Get bounds with wrap-around handled.
  const Common::Rectangle<u32> copy_rect = GetVRAMTransferBounds(x, y, width, height);
  const u32 encoded_width = (copy_rect.GetWidth() + 1) / 2;
  const u32 encoded_height = copy_rect.GetHeight();

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  cmdbuf.barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);

  // Encode the 24-bit texture as 16-bit.
  dk::ImageView view{m_vram_readback_texture.GetImage()};
  cmdbuf.bindRenderTargets({&view});

  const u32 uniforms[4] = {copy_rect.left, copy_rect.top, copy_rect.GetWidth(), copy_rect.GetHeight()};
  PushOtherUniform(cmdbuf, DkStage_Fragment, uniforms, sizeof(uniforms));

  DisableBlending(cmdbuf);
  Deko3D::Util::SetViewportAndScissor(cmdbuf, 0, 0, encoded_width, encoded_height);
  cmdbuf.bindVtxAttribState({});
  cmdbuf.bindShaders(DkStageFlag_Vertex | DkStageFlag_Fragment,
                     {&m_fullscreen_quad_vertex_shader.shader, &m_vram_read_shader.shader});
  cmdbuf.bindTextures(DkStage_Fragment, 0, {dkMakeTextureHandle(IMAGE_VRAM, SAMPLER_POINT)});

  cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);

  cmdbuf.barrier(DkBarrier_Primitives, DkInvalidateFlags_Image);
  // Stage the readback and copy it into our shadow buffer (will execute command buffer and stall).
  g_host_display->DownloadTexture(&m_vram_readback_texture, 0, 0, encoded_width, encoded_height,
                                  &m_vram_shadow[copy_rect.top * VRAM_WIDTH + copy_rect.left],
                                  VRAM_WIDTH * sizeof(u16));

  RestoreGraphicsAPIStateEx(true, true);
}

void GPU_HW_Deko3D::FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color)
{
  if (IsUsingSoftwareRendererForReadbacks())
    FillSoftwareRendererVRAM(x, y, width, height, color);

  GPU_HW::FillVRAM(x, y, width, height, color);

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();
  const Common::Rectangle<u32> bounds(GetVRAMTransferBounds(x, y, width, height));

  cmdbuf.setScissors(0, {{bounds.left * m_resolution_scale, bounds.top * m_resolution_scale,
                          bounds.GetWidth() * m_resolution_scale, bounds.GetHeight() * m_resolution_scale}});

  const bool wrapped = IsVRAMFillOversized(x, y, width, height);
  const bool interlaced = IsInterlacedRenderingEnabled();

  if (!wrapped && !interlaced)
  {
    const auto [r, g, b, a] =
      RGBA8ToFloat(m_true_color ? color : VRAMRGBA5551ToRGBA8888(VRAMRGBA8888ToRGBA5551(color)));

    cmdbuf.clearColor(0, DkColorMask_RGBA, r, g, b, a);
    cmdbuf.clearDepthStencil(true, a, 0, 0);

    SetScissorFromDrawingArea();
  }
  else
  {
    const VRAMFillUBOData uniforms = GetVRAMFillUBOData(x, y, width, height, color);
    PushOtherUniform(cmdbuf, DkStage_Fragment, &uniforms, sizeof(uniforms));

    cmdbuf.bindShaders(DkStageFlag_Vertex | DkStageFlag_Fragment,
                       {&m_fullscreen_quad_vertex_shader.shader,
                        &m_vram_fill_shaders[BoolToUInt8(wrapped)][BoolToUInt8(interlaced)].shader});

    DisableBlending(cmdbuf);
    cmdbuf.bindVtxAttribState({});
    SetDepthTest(cmdbuf, true, DkCompareOp_Always);
    cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);
    RestoreGraphicsAPIStateEx(false, true);
  }
}

void GPU_HW_Deko3D::UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data, bool set_mask, bool check_mask)
{
  if (IsUsingSoftwareRendererForReadbacks())
    UpdateSoftwareRendererVRAM(x, y, width, height, data, set_mask, check_mask);

  const Common::Rectangle<u32> bounds = GetVRAMTransferBounds(x, y, width, height);
  GPU_HW::UpdateVRAM(bounds.left, bounds.top, bounds.GetWidth(), bounds.GetHeight(), data, set_mask, check_mask);

  if (!check_mask)
  {
    const TextureReplacementTexture* rtex = g_texture_replacements.GetVRAMWriteReplacement(width, height, data);
    if (rtex && BlitVRAMReplacementTexture(rtex, x * m_resolution_scale, y * m_resolution_scale,
                                           width * m_resolution_scale, height * m_resolution_scale))
    {
      return;
    }
  }

  const u32 data_size = width * height * sizeof(u16);
  if (!m_texture_stream_buffer.ReserveMemory(data_size, 2))
  {
    Log_PerfPrintf("Executing command buffer while waiting for %u bytes in stream buffer", data_size);
    ExecuteCommandBuffer(false, true);
    if (!m_texture_stream_buffer.ReserveMemory(data_size, 2))
    {
      Panic("Failed to allocate space in stream buffer for VRAM write");
      return;
    }
  }

  const u32 start_index = m_texture_stream_buffer.GetCurrentOffset() / sizeof(u16);
  std::memcpy(m_texture_stream_buffer.GetCurrentHostPointer(), data, data_size);
  m_texture_stream_buffer.CommitMemory(data_size);

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  const VRAMWriteUBOData uniforms = GetVRAMWriteUBOData(x, y, width, height, start_index, set_mask, check_mask);
  PushOtherUniform(cmdbuf, DkStage_Fragment, &uniforms, sizeof(uniforms));

  cmdbuf.bindVtxAttribState({});
  cmdbuf.bindShaders(DkStageFlag_Vertex | DkStageFlag_Fragment,
                     {&m_fullscreen_quad_vertex_shader.shader, &m_vram_write_shader.shader});

  SetDepthTest(cmdbuf, true, (check_mask && !m_pgxp_depth_buffer) ? DkCompareOp_Gequal : DkCompareOp_Always);
  DisableBlending(cmdbuf);

  cmdbuf.bindTextures(DkStage_Fragment, 0, {dkMakeTextureHandle(IMAGE_TEXTURE_BUFFER, 0)});

  // the viewport should already be set to the full vram, so just adjust the scissor
  const Common::Rectangle<u32> scaled_bounds = bounds * m_resolution_scale;
  cmdbuf.setScissors(0, {{scaled_bounds.left, scaled_bounds.top, scaled_bounds.GetWidth(), scaled_bounds.GetHeight()}});
  cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);

  RestoreGraphicsAPIStateEx(false, true);
}

void GPU_HW_Deko3D::CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height)
{
  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  if (IsUsingSoftwareRendererForReadbacks())
    CopySoftwareRendererVRAM(src_x, src_y, dst_x, dst_y, width, height);

  if (UseVRAMCopyShader(src_x, src_y, dst_x, dst_y, width, height) || IsUsingMultisampling())
  {
    const Common::Rectangle<u32> src_bounds = GetVRAMTransferBounds(src_x, src_y, width, height);
    const Common::Rectangle<u32> dst_bounds = GetVRAMTransferBounds(dst_x, dst_y, width, height);
    if (m_vram_dirty_rect.Intersects(src_bounds))
      UpdateVRAMReadTexture();
    IncludeVRAMDirtyRectangle(dst_bounds);

    cmdbuf.bindShaders(DkStageFlag_Vertex | DkStageFlag_Fragment,
                       {&m_fullscreen_quad_vertex_shader.shader, &m_vram_copy_shader.shader});
    DisableBlending(cmdbuf);
    SetDepthTest(cmdbuf, true,
                 (m_GPUSTAT.check_mask_before_draw && !m_pgxp_depth_buffer) ? DkCompareOp_Gequal : DkCompareOp_Always);

    const VRAMCopyUBOData uniforms(GetVRAMCopyUBOData(src_x, src_y, dst_x, dst_y, width, height));
    const Common::Rectangle<u32> dst_bounds_scaled(dst_bounds * m_resolution_scale);
    PushOtherUniform(cmdbuf, DkStage_Fragment, &uniforms, sizeof(uniforms));

    Deko3D::Util::SetViewportAndScissor(cmdbuf, dst_bounds_scaled.left, dst_bounds_scaled.top,
                                        dst_bounds_scaled.GetWidth(), dst_bounds_scaled.GetHeight());

    cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);
    RestoreGraphicsAPIStateEx(false, true);

    if (m_GPUSTAT.check_mask_before_draw)
      m_current_depth++;

    return;
  }

  GPU_HW::CopyVRAM(src_x, src_y, dst_x, dst_y, width, height);

  src_x *= m_resolution_scale;
  src_y *= m_resolution_scale;
  dst_x *= m_resolution_scale;
  dst_y *= m_resolution_scale;
  width *= m_resolution_scale;
  height *= m_resolution_scale;

  dk::ImageView vram_view{m_vram_texture.GetImage()};
  cmdbuf.blitImage(vram_view, {src_x, src_y, 0, width, height, 1}, vram_view, {dst_x, dst_y, 0, width, height, 1});
}

void GPU_HW_Deko3D::UpdateVRAMReadTexture()
{
  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  const auto scaled_rect = m_vram_dirty_rect * m_resolution_scale;

  dk::ImageView src{m_vram_texture.GetImage()};
  dk::ImageView dst{m_vram_read_texture.GetImage()};

  if (m_vram_texture.GetSamples() > DkMsMode_1x)
  {
    cmdbuf.blitImage(src,
                     {static_cast<u32>(scaled_rect.left), static_cast<u32>(scaled_rect.top), 0, scaled_rect.GetWidth(),
                      scaled_rect.GetHeight(), 1},
                     dst,
                     {static_cast<u32>(scaled_rect.left), static_cast<u32>(scaled_rect.top), 0, scaled_rect.GetWidth(),
                      scaled_rect.GetHeight(), 1},
                     DkFilter_Linear);
  }
  else
  {
    cmdbuf.copyImage(src,
                     {static_cast<u32>(scaled_rect.left), static_cast<u32>(scaled_rect.top), 0, scaled_rect.GetWidth(),
                      scaled_rect.GetHeight(), 1},
                     dst,
                     {static_cast<u32>(scaled_rect.left), static_cast<u32>(scaled_rect.top), 0, scaled_rect.GetWidth(),
                      scaled_rect.GetHeight(), 1});
  }

  GPU_HW::UpdateVRAMReadTexture();
}

void GPU_HW_Deko3D::UpdateDepthBufferFromMaskBit()
{
  if (m_pgxp_depth_buffer)
    return;

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  cmdbuf.barrier(DkBarrier_Fragments, DkInvalidateFlags_Image);

  cmdbuf.bindColorWriteState(dk::ColorWriteState{}.setMask(0, 0));
  Deko3D::Util::SetViewportAndScissor(cmdbuf, 0, 0, m_vram_texture.GetWidth(), m_vram_texture.GetHeight());
  cmdbuf.bindVtxAttribState({});
  cmdbuf.bindTextures(DkStage_Fragment, 0, {dkMakeTextureHandle(IMAGE_VRAM, SAMPLER_LINEAR)});
  cmdbuf.bindShaders(DkStageFlag_Vertex | DkStageFlag_Fragment,
                     {&m_fullscreen_quad_vertex_shader.shader, &m_vram_update_depth_shader.shader});
  DisableBlending(cmdbuf);
  SetDepthTest(cmdbuf, true, DkCompareOp_Always);
  cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);
  cmdbuf.bindColorWriteState(dk::ColorWriteState{}.setMask(0, DkColorMask_RGBA));

  RestoreGraphicsAPIStateEx(false, true);
}

void GPU_HW_Deko3D::ClearDepthBuffer()
{
  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  cmdbuf.clearDepthStencil(true, 1.f, 0xFF, 0);

  m_last_depth_z = 1.0f;
}

bool GPU_HW_Deko3D::BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, u32 dst_x, u32 dst_y, u32 width,
                                               u32 height)
{
  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();
  if (m_vram_write_replacement_texture.GetWidth() < tex->GetWidth() ||
      m_vram_write_replacement_texture.GetHeight() < tex->GetHeight())
  {
    if (!m_vram_write_replacement_texture.Create(tex->GetWidth(), tex->GetHeight(), 1, 1, DkImageFormat_RGBA8_Unorm,
                                                 DkMsMode_1x, DkImageType_2D, 0))
    {
      Log_ErrorPrint("Failed to create VRAM write replacement texture");
      return false;
    }
  }

  m_vram_write_replacement_texture.Update(0, 0, tex->GetWidth(), tex->GetHeight(), 0, 0, tex->GetPixels(),
                                          tex->GetPitch());

  // texture -> vram
  dk::ImageView src{m_vram_write_replacement_texture.GetImage()};
  dk::ImageView dst{m_vram_texture.GetImage()};
  cmdbuf.blitImage(src, {0, 0, 0, static_cast<uint32_t>(tex->GetWidth()), static_cast<uint32_t>(tex->GetHeight()), 1},
                   dst,
                   {static_cast<uint32_t>(dst_x), static_cast<uint32_t>(dst_y), 0, static_cast<uint32_t>(dst_x + width),
                    static_cast<uint32_t>(dst_y + height), 1},
                   DkBlitFlag_FilterLinear);
  return true;
}

void GPU_HW_Deko3D::DownsampleFramebuffer(Deko3D::Texture& source, u32 left, u32 top, u32 width, u32 height)
{
  Assert(m_downsample_mode == GPUDownsampleMode::Box);
  DownsampleFramebufferBoxFilter(source, left, top, width, height);
}

void GPU_HW_Deko3D::DownsampleFramebufferBoxFilter(Deko3D::Texture& source, u32 left, u32 top, u32 width, u32 height)
{
  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  Assert(&source == &m_vram_texture || &source == &m_display_texture);

  const u32 ds_left = left / m_resolution_scale;
  const u32 ds_top = top / m_resolution_scale;
  const u32 ds_width = width / m_resolution_scale;
  const u32 ds_height = height / m_resolution_scale;

  dk::ImageView downsampleTextureView{m_downsample_texture.GetImage()};

  cmdbuf.bindRenderTargets({&downsampleTextureView});
  DisableBlending(cmdbuf);
  SetDepthTest(cmdbuf, false, DkCompareOp_Always);
  Deko3D::Util::SetViewportAndScissor(cmdbuf, ds_left, ds_top, ds_width, ds_height);
  cmdbuf.bindVtxAttribState({});
  cmdbuf.bindTextures(DkStage_Fragment, 0,
                      {dkMakeTextureHandle(&source == &m_vram_texture ? IMAGE_VRAM : IMAGE_DISPLAY, SAMPLER_LINEAR)});
  cmdbuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);

  RestoreGraphicsAPIStateEx(true, true);

  g_host_display->SetDisplayTexture(&m_downsample_texture, ds_left, ds_top, ds_width, ds_height);
}

std::unique_ptr<GPU> GPU::CreateHardwareDeko3DRenderer()
{
  return std::make_unique<GPU_HW_Deko3D>();
}
