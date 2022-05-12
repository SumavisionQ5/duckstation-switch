#pragma once
#include "controller_interface.h"
#include "core/types.h"
#include <array>
#include <functional>
#include <mutex>
#include <vector>
#include <switch.h>

class SwitchControllerInterface final : public ControllerInterface
{
public:
  SwitchControllerInterface();
  ~SwitchControllerInterface();

  Backend GetBackend() const override;
  bool Initialize(CommonHostInterface* host_interface) override;
  void Shutdown() override;

  /// Returns the path of the optional game controller database file.
  std::string GetGameControllerDBFileName() const;

  // Removes all bindings. Call before setting new bindings.
  void ClearBindings() override;

  // Binding to events. If a binding for this axis/button already exists, returns false.
  bool BindControllerAxis(int controller_index, int axis_number, AxisSide axis_side, AxisCallback callback) override;
  bool BindControllerButton(int controller_index, int button_number, ButtonCallback callback) override;
  bool BindControllerAxisToButton(int controller_index, int axis_number, bool direction,
                                  ButtonCallback callback) override;
  bool BindControllerHatToButton(int controller_index, int hat_number, std::string_view hat_position,
                                 ButtonCallback callback) override;
  bool BindControllerButtonToAxis(int controller_index, int button_number, AxisCallback callback) override;

  // Changing rumble strength.
  u32 GetControllerRumbleMotorCount(int controller_index) override;
  void SetControllerRumbleStrength(int controller_index, const float* strengths, u32 num_motors) override;

  // Set deadzone that will be applied on axis-to-button mappings
  bool SetControllerDeadzone(int controller_index, float size = 0.25f) override;

  void PollEvents() override;

private:
  enum : int
  {
    MAX_CONTROLLERS = 2,
    MAX_NUM_AXES = 4,
    MAX_NUM_BUTTONS = 32,
  };

  struct ControllerData
  {
    PadState pad;
    bool connected = false;
    std::array<std::array<AxisCallback, 3>, MAX_NUM_AXES> axis_mapping;
    std::array<ButtonCallback, MAX_NUM_BUTTONS> button_mapping;
    std::array<std::array<ButtonCallback, 2>, MAX_NUM_AXES> axis_button_mapping;
    std::array<AxisCallback, MAX_NUM_BUTTONS> button_axis_mapping;
    float deadzone = 0.25f;
  };
  ControllerData m_controllers[MAX_CONTROLLERS];

  void HandleButtons(int player_id, u64 mask, bool pressed);
  void HandleAxis(int controller_id, int axis_number, float value);
};
