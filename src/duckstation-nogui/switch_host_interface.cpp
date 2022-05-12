#include "switch_host_interface.h"
#include "core/system.h"
#include "frontend-common/controller_interface.h"
#include "frontend-common/fullscreen_ui.h"
#include "frontend-common/icon.h"
#include "frontend-common/ini_settings_interface.h"
#include "imgui.h"
#include "imgui_impl_switch.h"
#include "scmversion/scmversion.h"
#include <cinttypes>
#include <cmath>
#include <switch.h>
Log_SetChannel(SwitchHostInterface);

SwitchHostInterface::SwitchHostInterface() = default;

SwitchHostInterface::~SwitchHostInterface() = default;

const char* SwitchHostInterface::GetFrontendName() const
{
  return "DuckStation Nintendo Switch Frontend";
}

std::unique_ptr<SwitchHostInterface> SwitchHostInterface::Create()
{
  return std::make_unique<SwitchHostInterface>();
}

void SwitchHostInterface::AppletModeChange(AppletHookType type)
{
  switch (type)
  {
  case AppletHookType_OnOperationMode:
  {
    std::optional<WindowInfo> wi = GetPlatformWindowInfo();
    m_display->ResizeRenderWindow(wi->surface_width, wi->surface_height);
    OnHostDisplayResized();
    break;
  }
  default:
    break;
  }
}

static void AppletModeChange(AppletHookType type, void* host_interface)
{
  static_cast<SwitchHostInterface*>(host_interface)->AppletModeChange(type);
}

bool SwitchHostInterface::Initialize()
{
  if (!NoGUIHostInterface::Initialize())
    return false;

  //appletHook(&m_applet_mode_change, ::AppletModeChange, this);

  return true;
}

void SwitchHostInterface::Shutdown()
{
  NoGUIHostInterface::Shutdown();
}

bool SwitchHostInterface::IsFullscreen() const
{
  return true;
}

bool SwitchHostInterface::SetFullscreen(bool enabled)
{
  return enabled;
}

bool SwitchHostInterface::RequestRenderWindowSize(s32 new_window_width, s32 new_window_height)
{
  return false;
}

bool SwitchHostInterface::CreatePlatformWindow()
{
  ImGui_ImplSwitch_Init();
  return true;
}

void SwitchHostInterface::DestroyPlatformWindow()
{
  ImGui_ImplSwitch_Shutdown();
}

std::optional<WindowInfo> SwitchHostInterface::GetPlatformWindowInfo()
{
  WindowInfo wi;
  AppletOperationMode mode = appletGetOperationMode();
  if (mode == AppletOperationMode_Handheld)
  {
    wi.surface_width = 1280;
    wi.surface_height = 720;
  }
  else
  {
    wi.surface_width = 1920;
    wi.surface_height = 1080;
  }
  wi.surface_scale = 1.2f;
  wi.surface_format = WindowInfo::SurfaceFormat::RGBA8;
  wi.type = WindowInfo::Type::Switch;
  wi.window_handle = nwindowGetDefault();
  return wi;
}

std::optional<CommonHostInterface::HostKeyCode> SwitchHostInterface::GetHostKeyCode(const std::string_view key_code) const
{
  return std::nullopt;
}

void SwitchHostInterface::SetMouseMode(bool relative, bool hide_cursor) {}

void SwitchHostInterface::PollAndUpdate()
{
  if (!appletMainLoop())
    m_quit_request = true;

  ImGui_ImplSwitch_NewFrame();
  NoGUIHostInterface::PollAndUpdate();
}
