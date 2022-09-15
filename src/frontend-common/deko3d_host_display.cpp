#include "deko3d_host_display.h"
#include "common/align.h"
#include "common/assert.h"
#include "common/deko3d/context.h"
#include "common/deko3d/shader_cache.h"
#include "common/deko3d/util.h"
#include "common/log.h"
#include "common/string_util.h"
#include "common_host_interface.h"
#include "core/shader_cache_version.h"
#include "frontend-common/imgui_impl_deko3d.h"
#include "imgui.h"
#include "imgui_impl_deko3d.h"
#include "postprocessing_shadergen.h"
#include <array>
#include <switch.h>
#include <tuple>
Log_SetChannel(Deko3DHostDisplay);

namespace FrontendCommon {

class Deko3DHostDisplayTexture : public HostDisplayTexture
{
public:
  Deko3DHostDisplayTexture(Deko3D::Texture texture, Deko3D::StagingTexture staging_texture,
                           HostDisplayPixelFormat format)
    : m_texture(std::move(texture)), m_staging_texture(std::move(staging_texture)), m_format(format)
  {
  }
  ~Deko3DHostDisplayTexture() override = default;

  void* GetHandle() const override { return const_cast<Deko3D::Texture*>(&m_texture); }
  u32 GetWidth() const override { return m_texture.GetWidth(); }
  u32 GetHeight() const override { return m_texture.GetHeight(); }
  u32 GetLayers() const override { return m_texture.GetLayers(); }
  u32 GetLevels() const override { return m_texture.GetLevels(); }
  u32 GetSamples() const override { return m_texture.GetSamples(); }
  HostDisplayPixelFormat GetFormat() const override { return m_format; }

  const Deko3D::Texture& GetTexture() const { return m_texture; }
  Deko3D::Texture& GetTexture() { return m_texture; }
  Deko3D::StagingTexture& GetStagingTexture() { return m_staging_texture; }

private:
  Deko3D::Texture m_texture;
  Deko3D::StagingTexture m_staging_texture;
  HostDisplayPixelFormat m_format;
};

Deko3DHostDisplay::Deko3DHostDisplay() = default;

Deko3DHostDisplay::~Deko3DHostDisplay()
{
  AssertMsg(!m_swap_chain, "Swap chain should have been destroyed by now");
  AssertMsg(!g_deko3d_context, "Context should have been destroyed by now");
}

HostDisplay::RenderAPI Deko3DHostDisplay::GetRenderAPI() const
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

bool Deko3DHostDisplay::CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device,
                                           bool threaded_presentation)
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

bool Deko3DHostDisplay::InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                                               bool threaded_presentation)
{
  Deko3D::ShaderCache::Create(shader_cache_directory, SHADER_CACHE_VERSION, debug_device);

  if (!CreateResources())
    return false;

  return true;
}

void Deko3DHostDisplay::DestroyRenderDevice()
{
  if (!g_deko3d_context)
    return;

  g_deko3d_context->WaitGPUIdle();

  DestroyResources();
  DestroyRenderSurface();

  Deko3D::Context::Destroy();
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

static constexpr std::array<DkImageFormat, static_cast<u32>(HostDisplayPixelFormat::Count)>
  s_display_pixel_format_mapping = {{DkImageFormat_None, DkImageFormat_RGBA8_Unorm, DkImageFormat_BGRA8_Unorm,
                                     DkImageFormat_RGB565_Unorm, DkImageFormat_BGR5A1_Unorm}};

std::unique_ptr<HostDisplayTexture> Deko3DHostDisplay::CreateTexture(u32 width, u32 height, u32 layers, u32 levels,
                                                                     u32 samples, HostDisplayPixelFormat format,
                                                                     const void* data, u32 data_stride, bool dynamic)
{
  const DkImageFormat dk_format = s_display_pixel_format_mapping[static_cast<u32>(format)];
  if (dk_format == DkImageFormat_None)
    return {};

  printf("creating texture %d %d\n", width, height);
  Deko3D::Texture texture;
  if (!texture.Create(width, height, levels, layers, dk_format, static_cast<DkMsMode>(__builtin_ctz(samples)),
                      (layers > 1) ? DkImageType_2DArray : DkImageType_2D, 0))
  {
    return {};
  }

  Deko3D::StagingTexture staging_texture;
  if (data || dynamic)
  {
    if (!staging_texture.Create(dk_format, width, height))
    {
      return {};
    }
  }

  if (data)
  {
    staging_texture.WriteTexels(0, 0, width, height, data, data_stride);
    staging_texture.CopyToTexture(g_deko3d_context->GetCmdBuf(), 0, 0, texture, 0, 0, 0, 0, width, height);
  }
  else
  {
    // clear it instead so we don't read uninitialized data (and keep the validation layer happy!)
    /*static constexpr VkClearColorValue ccv = {};
    static constexpr VkImageSubresourceRange isr = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vkCmdClearColorImage(g_vulkan_context->GetCurrentCommandBuffer(), texture.GetImage(), texture.GetLayout(), &ccv, 1u,
                         &isr);*/
  }

  // don't need to keep the staging texture around if we're not dynamic
  if (!dynamic)
    staging_texture.Destroy(true);

  return std::make_unique<Deko3DHostDisplayTexture>(std::move(texture), std::move(staging_texture), format);
}

void Deko3DHostDisplay::UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height,
                                      const void* data, u32 data_stride)
{
  Deko3DHostDisplayTexture* dk_texture = static_cast<Deko3DHostDisplayTexture*>(texture);

  Deko3D::StagingTexture* staging_texture;
  if (dk_texture->GetStagingTexture().IsValid())
  {
    staging_texture = &dk_texture->GetStagingTexture();
  }
  else
  {
    // TODO: This should use a stream buffer instead for speed.
    if (m_upload_staging_texture.IsValid())
      m_upload_staging_texture.Flush();

    if ((m_upload_staging_texture.GetWidth() < width || m_upload_staging_texture.GetHeight() < height) &&
        !m_upload_staging_texture.Create(DkImageFormat_RGBA8_Unorm, width, height))
    {
      Panic("Failed to create upload staging texture");
    }

    staging_texture = &m_upload_staging_texture;
  }

  staging_texture->WriteTexels(0, 0, width, height, data, data_stride);
  staging_texture->CopyToTexture(0, 0, dk_texture->GetTexture(), x, y, 0, 0, width, height);
}

bool Deko3DHostDisplay::DownloadTexture(const void* texture_handle, HostDisplayPixelFormat texture_format, u32 x, u32 y,
                                        u32 width, u32 height, void* out_data, u32 out_data_stride)
{
  Deko3D::Texture* texture = static_cast<Deko3D::Texture*>(const_cast<void*>(texture_handle));

  if ((m_readback_staging_texture.GetWidth() < width || m_readback_staging_texture.GetHeight() < height) &&
      !m_readback_staging_texture.Create(texture->GetFormat(), width, height))
  {
    return false;
  }

  m_readback_staging_texture.CopyFromTexture(*texture, x, y, 0, 0, 0, 0, width, height);
  m_readback_staging_texture.ReadTexels(0, 0, width, height, out_data, out_data_stride);
  return true;
}

bool Deko3DHostDisplay::SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const
{
  //const DkImageFormat dk_format = s_display_pixel_format_mapping[static_cast<u32>(format)];
  //return dk_format != DkImageFormat_None;
  return format == HostDisplayPixelFormat::RGBA8;
}

bool Deko3DHostDisplay::BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, void** out_buffer,
                                              u32* out_pitch)
{
  const DkImageFormat dk_format = s_display_pixel_format_mapping[static_cast<u32>(format)];

  if (m_display_pixels_texture.GetWidth() < width || m_display_pixels_texture.GetHeight() < height ||
      m_display_pixels_texture.GetFormat() != dk_format)
  {
    if (!m_display_pixels_texture.Create(width, height, 1, 1, dk_format, DkMsMode_1x, DkImageType_2D, 0))
    {
      printf("failed to create pixel blah\n");
      return false;
    }
  }

  if ((m_upload_staging_texture.GetWidth() < width || m_upload_staging_texture.GetHeight() < height) &&
      !m_upload_staging_texture.Create(dk_format, width, height))
  {
    printf("failed to create upload buffer\n");
    return false;
  }

  SetDisplayTexture(&m_display_pixels_texture, format, m_display_pixels_texture.GetWidth(),
                    m_display_pixels_texture.GetHeight(), 0, 0, width, height);

  *out_buffer = m_upload_staging_texture.GetMappedPointer();
  *out_pitch = m_upload_staging_texture.GetMappedStride();
  return true;
}

void Deko3DHostDisplay::EndSetDisplayPixels()
{
  m_upload_staging_texture.CopyToTexture(0, 0, m_display_pixels_texture, 0, 0, 0, 0,
                                         static_cast<u32>(m_display_texture_view_width),
                                         static_cast<u32>(m_display_texture_view_height));
}

void Deko3DHostDisplay::SetVSync(bool enabled) {}

bool Deko3DHostDisplay::Render()
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
  cmdbuf.clearColor(0, DkColorMask_RGBA, 1.f, 0.f, 0.f, 1.f);

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

  const auto [left, top, width, height] = CalculateDrawRect(GetWindowWidth(), GetWindowHeight(), m_display_top_margin);

  // no post processing for now
  /*if (!m_post_processing_chain.IsEmpty())
  {
    ApplyPostProcessingChain(m_swap_chain->GetCurrentFramebuffer(), left, top, width, height, m_display_texture_handle,
                             m_display_texture_width, m_display_texture_height, m_display_texture_view_x,
                             m_display_texture_view_y, m_display_texture_view_width, m_display_texture_view_height,
                             m_swap_chain->GetWidth(), m_swap_chain->GetHeight());
    return;
  }*/

  RenderDisplay(left, top, width, height, m_display_texture_handle, m_display_texture_width, m_display_texture_height,
                m_display_texture_view_x, m_display_texture_view_y, m_display_texture_view_width,
                m_display_texture_view_height, m_display_linear_filtering);
}

void Deko3DHostDisplay::RenderDisplay(s32 left, s32 top, s32 width, s32 height, void* texture_handle, u32 texture_width,
                                      s32 texture_height, s32 texture_view_x, s32 texture_view_y,
                                      s32 texture_view_width, s32 texture_view_height, bool linear_filter)
{
  dk::CmdBuf cmdbuffer = g_deko3d_context->GetCmdBuf();

  const float position_adjust = m_display_linear_filtering ? 0.5f : 0.0f;
  const float size_adjust = m_display_linear_filtering ? 1.0f : 0.0f;
  const UniformBuffer pc{(static_cast<float>(texture_view_x) + position_adjust) / static_cast<float>(texture_width),
                         (static_cast<float>(texture_view_y) + position_adjust) / static_cast<float>(texture_height),
                         (static_cast<float>(texture_view_width) - size_adjust) / static_cast<float>(texture_width),
                         (static_cast<float>(texture_view_height) - size_adjust) / static_cast<float>(texture_height)};
  cmdbuffer.bindVtxAttribState({});
  cmdbuffer.bindColorState(dk::ColorState{});
  cmdbuffer.bindColorWriteState(dk::ColorWriteState{}.setMask(0, DkColorMask_RGBA));
  cmdbuffer.bindDepthStencilState(dk::DepthStencilState{}.setDepthWriteEnable(false).setDepthTestEnable(false));
  cmdbuffer.bindUniformBuffer(DkStage_Vertex, 0, g_deko3d_context->GetGeneralHeap().GpuAddr(m_uniform_buffer), m_uniform_buffer.size);

  Deko3D::Texture* texture = static_cast<Deko3D::Texture*>(texture_handle);

  cmdbuffer.barrier(DkBarrier_Full, DkInvalidateFlags_Descriptors|DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache);

  dk::ImageDescriptor descriptor;
  dk::ImageView view {texture->GetImage()};
  descriptor.initialize(view);
  cmdbuffer.pushData(g_deko3d_context->GetGeneralHeap().GpuAddr(m_descriptor_buffer), &descriptor, sizeof(descriptor));
  cmdbuffer.bindSamplerDescriptorSet(g_deko3d_context->GetGeneralHeap().GpuAddr(m_sampler_buffer), 2);
  cmdbuffer.bindImageDescriptorSet(g_deko3d_context->GetGeneralHeap().GpuAddr(m_descriptor_buffer), 1);

  cmdbuffer.bindImages(DkStage_Fragment, 0, {dkMakeTextureHandle(0, (u32)linear_filter)});

  cmdbuffer.bindShaders(DkStageFlag_Vertex | DkStageFlag_Fragment, {&m_vertex_shader, &m_display_fragment_shader});

  cmdbuffer.pushConstants(g_deko3d_context->GetGeneralHeap().GpuAddr(m_uniform_buffer), m_uniform_buffer.size, 0,
                          sizeof(pc), &pc);
  Deko3D::Util::SetViewportAndScissor(cmdbuffer, left, top, width, height);
  cmdbuffer.draw(DkPrimitive_Triangles, 3, 1, 0, 0);
}

bool Deko3DHostDisplay::RenderScreenshot(u32 width, u32 height, std::vector<u32>* out_pixels, u32* out_stride,
                                         HostDisplayPixelFormat* out_format)
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
  gl_Position.y = -gl_Position.y;
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
  m_readback_staging_texture.Destroy(false);
  m_upload_staging_texture.Destroy(false);
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

} // namespace FrontendCommon