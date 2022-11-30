#include "deko3d_host_display.h"
#include "common/align.h"
#include "common/assert.h"
#include "common/deko3d/context.h"
#include "common/deko3d/shader_cache.h"
#include "common/deko3d/util.h"
#include "common/log.h"
#include "common/string_util.h"
#include "core/settings.h"
#include "core/shader_cache_version.h"
#include "frontend-common/imgui_impl_deko3d.h"
#include "imgui.h"
#include "imgui_impl_deko3d.h"
#include "postprocessing_shadergen.h"
#include <array>
#include <switch.h>
#include <tuple>
Log_SetChannel(Deko3DHostDisplay);

Deko3DHostDisplay::Deko3DHostDisplay() = default;

Deko3DHostDisplay::~Deko3DHostDisplay()
{
  if (!g_deko3d_context)
    return;

  g_deko3d_context->WaitGPUIdle();

  DestroyResources();
  DestroyRenderSurface();

  Deko3D::Context::Destroy();
}

RenderAPI Deko3DHostDisplay::GetRenderAPI() const
{
  return RenderAPI::Deko3D;
}

void* Deko3DHostDisplay::GetRenderDevice() const
{
  return nullptr;
}

void* Deko3DHostDisplay::GetRenderContext() const
{
  return nullptr;
}

bool Deko3DHostDisplay::HasRenderDevice() const
{
  return g_deko3d_context != nullptr;
}

bool Deko3DHostDisplay::HasRenderSurface() const
{
  return g_deko3d_context != nullptr;
}

bool Deko3DHostDisplay::CreateRenderDevice(const WindowInfo& wi)
{
  WindowInfo local_wi(wi);
  if (!Deko3D::Context::Create(&local_wi))
  {
    Log_ErrorPrintf("Failed to create deko3D context");
    m_window_info = {};
    return false;
  }
  m_swap_chain = Deko3D::SwapChain::Create(local_wi);
  if (!m_swap_chain)
  {
    Log_ErrorPrintf("Failed to create swapchain");
  }

  m_window_info = m_swap_chain ? m_swap_chain->GetWindowInfo() : local_wi;
  return true;
}

bool Deko3DHostDisplay::InitializeRenderDevice()
{
#ifdef NDEBUG
  const bool debug = false;
#else
  const bool debug = true;
#endif

  Deko3D::ShaderCache::Create(EmuFolders::Cache, SHADER_CACHE_VERSION, debug);

  if (!CreateResources())
    return false;

  return true;
}

bool Deko3DHostDisplay::MakeRenderContextCurrent()
{
  return true;
}

bool Deko3DHostDisplay::DoneRenderContextCurrent()
{
  return true;
}

bool Deko3DHostDisplay::ChangeRenderWindow(const WindowInfo& new_wi)
{
  return false;
}

void Deko3DHostDisplay::ResizeRenderWindow(s32 new_window_width, s32 new_window_height) {}

bool Deko3DHostDisplay::SupportsFullscreen() const
{
  return false;
}

bool Deko3DHostDisplay::IsFullscreen()
{
  return false;
}

bool Deko3DHostDisplay::SetFullscreen(bool fullscreen, u32 width, u32 height, float refresh_rate)
{
  return false;
}

HostDisplay::AdapterAndModeList Deko3DHostDisplay::GetAdapterAndModeList()
{
  return {};
}

void Deko3DHostDisplay::DestroyRenderSurface()
{
  m_window_info = {};
  g_deko3d_context->WaitGPUIdle();
  m_swap_chain.reset();
}

bool Deko3DHostDisplay::SetPostProcessingChain(const std::string_view& config)
{
  return false;
}

static constexpr std::array<DkImageFormat, static_cast<u32>(GPUTexture::Format::Count)> s_display_pixel_format_mapping =
  {{DkImageFormat_None, DkImageFormat_RGBA8_Unorm, DkImageFormat_BGRA8_Unorm, DkImageFormat_BGR565_Unorm,
    DkImageFormat_BGR5A1_Unorm, DkImageFormat_R8_Unorm, DkImageFormat_Z16}};

std::unique_ptr<GPUTexture> Deko3DHostDisplay::CreateTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                                             GPUTexture::Format format, const void* data,
                                                             u32 data_stride, bool dynamic)
{
  const DkImageFormat dk_format = s_display_pixel_format_mapping[static_cast<u32>(format)];
  if (dk_format == DkImageFormat_None)
    return {};

  std::unique_ptr<Deko3D::Texture> texture(std::make_unique<Deko3D::Texture>());
  if (!texture->Create(width, height, levels, layers, dk_format, static_cast<DkMsMode>(__builtin_ctz(samples)),
                       (layers > 1) ? DkImageType_2DArray : DkImageType_2D, 0))
  {
    return {};
  }

  if (data)
  {
    texture->Update(0, 0, width, height, 0, 0, data, data_stride);
  }

  return texture;
}

bool Deko3DHostDisplay::UpdateTexture(GPUTexture* texture, u32 x, u32 y, u32 width, u32 height, const void* data,
                                      u32 data_stride)
{
  return static_cast<Deko3D::Texture*>(texture)->Update(x, y, width, height, 0, 0, data, data_stride);
}

bool Deko3DHostDisplay::BeginTextureUpdate(GPUTexture* texture, u32 width, u32 height, void** out_buffer,
                                           u32* out_pitch)
{
  return static_cast<Deko3D::Texture*>(texture)->BeginUpdate(width, height, out_buffer, out_pitch);
}

void Deko3DHostDisplay::EndTextureUpdate(GPUTexture* texture, u32 x, u32 y, u32 width, u32 height)
{
  static_cast<Deko3D::Texture*>(texture)->EndUpdate(x, y, width, height, 0, 0);
}

void Deko3DHostDisplay::CheckStagingBufferSize(u32 required_size)
{
  if (m_readback_buffer.size >= required_size)
    return;

  Deko3D::MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();
  // no synchronisation necessary, because there's always a GPU/CPU sync
  // when using this buffer
  if (m_readback_buffer.size > 0)
    heap.Free(m_readback_buffer);
  m_readback_buffer = heap.Alloc(required_size, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
}

bool Deko3DHostDisplay::DownloadTexture(GPUTexture* texture, u32 x, u32 y, u32 width, u32 height, void* out_data,
                                        u32 out_data_stride)
{
  Deko3D::Texture* tex = static_cast<Deko3D::Texture*>(texture);
  const u32 pitch = tex->CalcUpdatePitch(width);
  const u32 size = pitch * height;
  CheckStagingBufferSize(size);

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  dk::ImageView srcView{tex->GetImage()};
  if (texture->GetFormat() == GPUTexture::Format::D16)
    srcView.setFormat(DkImageFormat_R16_Uint);

  Deko3D::MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();

  // do the copy
  cmdbuf.copyImageToBuffer(srcView, {x, y, 0, width, height, 1}, {heap.GpuAddr(m_readback_buffer), 0, 0});
  cmdbuf.barrier(DkBarrier_Full, DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache);

  g_deko3d_context->ExecuteCommandBuffer(true);

  StringUtil::StrideMemCpy(out_data, out_data_stride, heap.CpuAddr<void>(m_readback_buffer), pitch,
                            std::min(pitch, out_data_stride), height);
  return true;
}

bool Deko3DHostDisplay::SupportsTextureFormat(GPUTexture::Format format) const
{
  return format == GPUTexture::Format::RGBA8 || format == GPUTexture::Format::RGB565 ||
         format == GPUTexture::Format::RGBA5551;
}

void Deko3DHostDisplay::SetVSync(bool enabled) {}

bool Deko3DHostDisplay::Render(bool skip_present)
{
  if (ShouldSkipDisplayingFrame() || !m_swap_chain)
  {
    if (ImGui::GetCurrentContext())
      ImGui::Render();

    return false;
  }

  int imageSlot = m_swap_chain->AcquireImage();

  dk::CmdBuf cmdbuf = g_deko3d_context->GetCmdBuf();

  dk::ImageView colorTargetView{m_swap_chain->GetImage(imageSlot).GetImage()};
  cmdbuf.bindRenderTargets({&colorTargetView});
  cmdbuf.setScissors(0, {{0, 0, m_window_info.surface_width, m_window_info.surface_height}});
  cmdbuf.clearColor(0, DkColorMask_RGBA, 0.f, 0.f, 0.f, 1.f);

  RenderDisplay();

  if (ImGui::GetCurrentContext())
    RenderImGui();

  g_deko3d_context->SubmitCommandBuffer(&m_swap_chain->GetCurrentAcquireFence(), false);
  g_deko3d_context->MoveToNextCommandBuffer();
  m_swap_chain->PresentImage(imageSlot);

  return true;
}

void Deko3DHostDisplay::RenderImGui()
{
  ImGui::Render();
  ImGui_ImplDeko3D_RenderDrawData(ImGui::GetDrawData(), g_deko3d_context->GetCmdBuf());
}

void Deko3DHostDisplay::RenderDisplay()
{
  if (!HasDisplayTexture())
  {
    return;
  }

  const auto [left, top, width, height] = CalculateDrawRect(GetWindowWidth(), GetWindowHeight());

  // no post processing for now
  /*if (!m_post_processing_chain.IsEmpty())
  {
    ApplyPostProcessingChain(m_swap_chain->GetCurrentFramebuffer(), left, top, width, height, m_display_texture_handle,
                             m_display_texture_width, m_display_texture_height, m_display_texture_view_x,
                             m_display_texture_view_y, m_display_texture_view_width, m_display_texture_view_height,
                             m_swap_chain->GetWidth(), m_swap_chain->GetHeight());
    return;
  }*/

  RenderDisplay(left, top, width, height, static_cast<Deko3D::Texture*>(m_display_texture), m_display_texture_view_x,
                m_display_texture_view_y, m_display_texture_view_width, m_display_texture_view_height,
                IsUsingLinearFiltering());
}

void Deko3DHostDisplay::RenderDisplay(s32 left, s32 top, s32 width, s32 height, Deko3D::Texture* texture,
                                      s32 texture_view_x, s32 texture_view_y, s32 texture_view_width,
                                      s32 texture_view_height, bool linear_filter)
{
  dk::CmdBuf cmdbuffer = g_deko3d_context->GetCmdBuf();

  const float position_adjust = linear_filter ? 0.5f : 0.0f;
  const float size_adjust = linear_filter ? 1.0f : 0.0f;
  const UniformBuffer pc{
    (static_cast<float>(texture_view_x) + position_adjust) / static_cast<float>(texture->GetWidth()),
    (static_cast<float>(texture_view_y) + position_adjust) / static_cast<float>(texture->GetHeight()),
    (static_cast<float>(texture_view_width) - size_adjust) / static_cast<float>(texture->GetWidth()),
    (static_cast<float>(texture_view_height) - size_adjust) / static_cast<float>(texture->GetHeight())};
  cmdbuffer.bindVtxAttribState({});
  cmdbuffer.bindColorState(dk::ColorState{});
  cmdbuffer.bindColorWriteState(dk::ColorWriteState{}.setMask(0, DkColorMask_RGBA));
  cmdbuffer.bindDepthStencilState(dk::DepthStencilState{}.setDepthWriteEnable(false).setDepthTestEnable(false));
  cmdbuffer.bindUniformBuffer(DkStage_Vertex, 0, g_deko3d_context->GetGeneralHeap().GpuAddr(m_uniform_buffer),
                              m_uniform_buffer.size);
  cmdbuffer.bindRasterizerState(dk::RasterizerState{}.setCullMode(DkFace_None));

  cmdbuffer.barrier(DkBarrier_Primitives,
                    DkInvalidateFlags_Descriptors | DkInvalidateFlags_Image);

  dk::ImageDescriptor descriptor;
  dk::ImageView view{texture->GetImage()};

  descriptor.initialize(view);
  cmdbuffer.pushData(g_deko3d_context->GetGeneralHeap().GpuAddr(m_descriptor_buffer), &descriptor, sizeof(descriptor));
  cmdbuffer.bindSamplerDescriptorSet(g_deko3d_context->GetGeneralHeap().GpuAddr(m_sampler_buffer), 2);
  cmdbuffer.bindImageDescriptorSet(g_deko3d_context->GetGeneralHeap().GpuAddr(m_descriptor_buffer), 1);

  cmdbuffer.bindTextures(DkStage_Fragment, 0, {dkMakeTextureHandle(0, (u32)linear_filter)});

  cmdbuffer.bindShaders(DkStageFlag_Vertex | DkStageFlag_Fragment, {&m_vertex_shader, &m_display_fragment_shader});

  cmdbuffer.pushConstants(g_deko3d_context->GetGeneralHeap().GpuAddr(m_uniform_buffer), m_uniform_buffer.size, 0,
                          sizeof(pc), &pc);
  Deko3D::Util::SetViewportAndScissor(cmdbuffer, left, top, width, height);
  cmdbuffer.draw(DkPrimitive_Triangles, 3, 1, 0, 0);
}

bool Deko3DHostDisplay::RenderScreenshot(u32 width, u32 height, std::vector<u32>* out_pixels, u32* out_stride,
                                         GPUTexture::Format* out_format)
{
  return false;
}

bool Deko3DHostDisplay::CreateResources()
{
  static constexpr char fullscreen_quad_vertex_shader[] = R"(
#version 450 core

layout(std140, binding = 0) uniform PushConstants {
  uniform vec4 u_src_rect;
};

layout(location = 0) out vec2 v_tex0;

void main()
{
  vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  v_tex0 = u_src_rect.xy + pos * u_src_rect.zw;
  gl_Position = vec4(pos * vec2(2.0f, -2.0f) + vec2(-1.0f, 1.0f), 0.0f, 1.0f);
}
)";

  static constexpr char display_fragment_shader_src[] = R"(
#version 450 core

layout(binding = 0) uniform sampler2D samp0;

layout(location = 0) in vec2 v_tex0;
layout(location = 0) out vec4 o_col0;

void main()
{
  o_col0 = vec4(texture(samp0, v_tex0).rgb, 1.0);
}
)";

  if (!g_deko3d_shader_cache->GetVertexShader(fullscreen_quad_vertex_shader, m_vertex_shader, m_vertex_shader_memory))
    return false;
  if (!g_deko3d_shader_cache->GetFragmentShader(display_fragment_shader_src, m_display_fragment_shader,
                                                m_display_fragment_shader_memory))
    return false;

  m_uniform_buffer = g_deko3d_context->GetGeneralHeap().Alloc(sizeof(UniformBuffer), DK_UNIFORM_BUF_ALIGNMENT);
  m_descriptor_buffer =
    g_deko3d_context->GetGeneralHeap().Alloc(sizeof(dk::ImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT);
  m_sampler_buffer =
    g_deko3d_context->GetGeneralHeap().Alloc(sizeof(dk::SamplerDescriptor) * 2, DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
  dk::SamplerDescriptor* samplers = g_deko3d_context->GetGeneralHeap().CpuAddr<dk::SamplerDescriptor>(m_sampler_buffer);
  samplers[0].initialize(dk::Sampler{}.setWrapMode(DkWrapMode_ClampToBorder, DkWrapMode_ClampToBorder));
  samplers[1].initialize(dk::Sampler{}
                           .setWrapMode(DkWrapMode_ClampToBorder, DkWrapMode_ClampToBorder)
                           .setFilter(DkFilter_Linear, DkFilter_Linear));

  return true;
}

void Deko3DHostDisplay::DestroyResources()
{
  if (m_vertex_shader_memory.size > 0)
  {
    g_deko3d_context->GetShaderHeap().Free(m_vertex_shader_memory);
    m_vertex_shader_memory = {};
  }
  if (m_display_fragment_shader_memory.size > 0)
  {
    g_deko3d_context->GetShaderHeap().Free(m_display_fragment_shader_memory);
    m_display_fragment_shader_memory = {};
  }

  g_deko3d_context->GetGeneralHeap().Free(m_uniform_buffer);

  m_display_pixels_texture.Destroy(false);
}

bool Deko3DHostDisplay::CreateImGuiContext()
{
  ImGui_ImplDeko3D_InitInfo vii = {};
  vii.Device = g_deko3d_context->GetDevice();
  vii.Queue = g_deko3d_context->GetQueue();
  vii.MinImageCount = Deko3D::SwapChain::NumSwapchainEntries;
  vii.ImageCount = Deko3D::SwapChain::NumSwapchainEntries;

  return ImGui_ImplDeko3D_Init(&vii);
}

void Deko3DHostDisplay::DestroyImGuiContext()
{
  g_deko3d_context->WaitGPUIdle();
  ImGui_ImplDeko3D_Shutdown();
}

bool Deko3DHostDisplay::UpdateImGuiFontTexture()
{
  g_deko3d_context->ExecuteCommandBuffer(true);
  ImGui_ImplDeko3D_DestroyFontUploadObjects();
  return ImGui_ImplDeko3D_CreateFontsTexture(g_deko3d_context->GetCmdBuf());
}
