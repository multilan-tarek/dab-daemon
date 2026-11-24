#include "audio_mute.h"
#include "signal_metrics.h"

namespace {
constexpr auto AUDIO_RECOVERY_TIME = std::chrono::milliseconds(300);
constexpr auto AUDIO_STATUS_TIMEOUT = std::chrono::milliseconds(250);
constexpr uint32_t AUDIO_MUTE_CER = DAB_ANTENNA_LEVEL_CER_THRESHOLDS[0];
constexpr uint32_t AUDIO_RECOVERY_CER = DAB_ANTENNA_LEVEL_CER_THRESHOLDS[1];
}

void AudioMute::reset() {
    muted = true;
    recovery_started.reset();
    previous_update.reset();
}

void AudioMute::update(const bool locked, const bool msc_cer_valid, const uint32_t msc_cer, const Clock::time_point now) {
    if (previous_update && (now < *previous_update || now - *previous_update > AUDIO_STATUS_TIMEOUT)) {
        reset();
    }

    previous_update = now;

    if (!locked || !msc_cer_valid || msc_cer >= AUDIO_MUTE_CER) {
        reset();
        return;
    }

    if (!muted) {
        return;
    }

    if (msc_cer > AUDIO_RECOVERY_CER) {
        recovery_started.reset();
        return;
    }

    if (!recovery_started) {
        recovery_started = now;
    } else if (now - *recovery_started >= AUDIO_RECOVERY_TIME) {
        muted = false;
        recovery_started.reset();
    }
}

bool AudioMute::is_muted() const {
    return muted;
}
