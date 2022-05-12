#pragma once
#include "nogui_host_interface.h"
#include <switch.h>

class SwitchHostInterface final : public NoGUIHostInterface
{
public:
  SwitchHostInterface();
  ~SwitchHostInterface();

  static std::unique_ptr<SwitchHostInterface> Create();

  const char* GetFrontendName() const override;

  bool Initialize() override;
  void Shutdown() override;

  bool RequestRenderWindowSize(s32 new_window_width, s32 new_window_height) override;

  bool IsFullscreen() const override;
  bool SetFullscreen(bool enabled) override;

  void AppletModeChange(AppletHookType type);
protected:
  void SetMouseMode(bool relative, bool hide_cursor) override;

  void PollAndUpdate() override;

  std::optional<HostKeyCode> GetHostKeyCode(const std::string_view key_code) const override;

  bool CreatePlatformWindow() override;
  void DestroyPlatformWindow() override;
  std::optional<WindowInfo> GetPlatformWindowInfo() override;

private:
  AppletHookCookie m_applet_mode_change;
};
