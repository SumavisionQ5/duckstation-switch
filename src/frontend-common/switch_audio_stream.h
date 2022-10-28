#pragma once
#include "util/audio_stream.h"
#include <cstdint>
#include <atomic>
#include <switch.h>

class SwitchAudioStream final : public AudioStream
{
public:
  SwitchAudioStream(u32 sample_rate, u32 channels, u32 buffer_ms, AudioStretchMode stretch);
  ~SwitchAudioStream();

  void SetPaused(bool paused) override;
  void SetOutputVolume(u32 volume) override;

  bool Initialize(u32 latency_ms);

private:
  void DestroyContextAndStream();

  static void AudioThread(void* userdata);
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

  std::atomic<State> m_state = State::Playing;
  std::atomic<float> m_thread_volume = 1.f;
};
