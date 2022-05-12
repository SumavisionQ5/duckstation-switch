#pragma once
#include "common/audio_stream.h"
#include <cstdint>
#include <atomic>
#include <switch.h>

class SwitchAudioStream final : public AudioStream
{
public:
  SwitchAudioStream();
  ~SwitchAudioStream();

  static std::unique_ptr<SwitchAudioStream> Create();

protected:
  ALWAYS_INLINE bool IsOpen() const { return m_mem_pool != nullptr; }

  bool OpenDevice() override;
  void PauseDevice(bool paused) override;
  void CloseDevice() override;
  void FramesAvailable() override;
  void SetOutputVolume(u32 volume) override;

  static void AudioThread(void* userdata);

private:
  AudioDriver m_audio_driver;
  u8* m_mem_pool = nullptr;
  Thread m_audio_thread;
  u32 m_audio_thread_buffer_size, m_audio_thread_num_channels;

  enum class State
  {
    Paused,
    Playing,
    Stop
  };

  std::mutex m_settings_lock;
  std::atomic<State> m_state = State::Playing;
  std::atomic<float> m_thread_volume = 1.f;
};
