#ifndef AUDIO_STATISTICS_H
#define AUDIO_STATISTICS_H

#include "../config/features.h"

#if DAB_ENABLE_DIAGNOSTICS
#include <chrono>
#include <cstdint>

struct AudioStatistics {
    uint32_t sampling_rate = 0;
    uint8_t channels = 0;
    bool sbr = false;
    uint64_t frames_received = 0;
    uint64_t frames_output = 0;
    uint64_t frames_discarded = 0;
    uint64_t rs_corrected = 0;
    uint64_t rs_uncorrectable = 0;
    uint64_t crc_errors = 0;
    uint64_t sync_losses = 0;
    uint64_t dropouts = 0;
    bool rs_available = false;
    bool crc_available = false;

    void record_interruption() {
        if (frames_output != 0 && !interrupted) {
            dropouts++;
        }

        interrupted = true;
    }

    void record_sync_loss() {
        if (synchronized) {
            sync_losses++;
            record_interruption();
            synchronized = false;
        }
    }

    void record_discard() {
        frames_discarded++;
        record_interruption();
    }

    void check_gap() {
        if (frames_output != 0 && std::chrono::steady_clock::now() - last_output >= std::chrono::milliseconds(500)) {
            record_interruption();
        }
    }

    void record_output() {
        check_gap();
        frames_output++;
        interrupted = false;
        last_output = std::chrono::steady_clock::now();
    }

    bool synchronized = false;

private:
    bool interrupted = false;
    std::chrono::steady_clock::time_point last_output;
};
#endif

#endif //AUDIO_STATISTICS_H
