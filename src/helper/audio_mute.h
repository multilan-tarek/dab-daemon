#ifndef AUDIO_MUTE_H
#define AUDIO_MUTE_H

#include <chrono>
#include <cstdint>
#include <optional>

class AudioMute {
public:
    using Clock = std::chrono::steady_clock;

    void reset();

    void update(bool locked, bool msc_cer_valid, uint32_t msc_cer, Clock::time_point now);

    bool is_muted() const;

private:
    bool muted = true;
    std::optional<Clock::time_point> recovery_started;
    std::optional<Clock::time_point> previous_update;
};

#endif //AUDIO_MUTE_H
