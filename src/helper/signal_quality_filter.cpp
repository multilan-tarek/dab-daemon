#include "signal_quality_filter.h"
#include <algorithm>
#include "signal_metrics.h"

void SignalQualityFilter::reset() {
    msc_cer_initialized = false;
    smoothed_msc_cer = 0;
    fic_crc_error_rate_initialized = false;
    smoothed_fic_crc_error_rate = 0;
}

SignalQualityFilter::Status SignalQualityFilter::update(const bool ofdm_lock, const bool fec_lock, const bool fec_sync, const bool msc_cer_valid, const uint8_t antenna_level, const uint32_t msc_cer, const bool fic_crc_error_rate_valid, const uint32_t fic_crc_error_rate) {
    Status status;

    if (!ofdm_lock || !fec_lock || !fec_sync) {
        reset();
        status.signal_quality_valid = true;
        return status;
    }

    if (msc_cer_valid) {
        if (msc_cer_initialized) {
            smoothed_msc_cer = smooth(smoothed_msc_cer, msc_cer);
        } else {
            smoothed_msc_cer = msc_cer;
            msc_cer_initialized = true;
        }

        status.msc_cer_valid = true;
        status.antenna_level = antenna_level;
        status.msc_cer = smoothed_msc_cer;
    } else {
        msc_cer_initialized = false;
    }

    if (fic_crc_error_rate_valid) {
        if (fic_crc_error_rate_initialized) {
            smoothed_fic_crc_error_rate = smooth(smoothed_fic_crc_error_rate, fic_crc_error_rate);
        } else {
            smoothed_fic_crc_error_rate = fic_crc_error_rate;
            fic_crc_error_rate_initialized = true;
        }

        status.fic_crc_error_rate_valid = true;
        status.fic_crc_error_rate = smoothed_fic_crc_error_rate;
    } else {
        fic_crc_error_rate_initialized = false;
    }

    if (!status.msc_cer_valid) {
        return status;
    }

    status.signal_quality_valid = true;
    status.signal_quality = calculate_msc_quality(status.msc_cer);

    if (status.fic_crc_error_rate_valid) {
        const uint8_t fic_quality = static_cast<uint8_t>(100 - std::min<uint32_t>(100, status.fic_crc_error_rate / 100));
        status.signal_quality = std::min(status.signal_quality, fic_quality);
    }

    return status;
}

uint32_t SignalQualityFilter::smooth(const uint32_t previous, const uint32_t current) {
    return static_cast<uint32_t>((static_cast<uint64_t>(previous) * 3 + current + 2) / 4);
}

uint8_t SignalQualityFilter::calculate_msc_quality(const uint32_t msc_cer) {
    if (msc_cer >= DAB_ANTENNA_LEVEL_CER_THRESHOLDS[0]) {
        return 0;
    }

    for (size_t index = 1; index < DAB_ANTENNA_LEVEL_CER_THRESHOLDS.size(); index++) {
        if (msc_cer >= DAB_ANTENNA_LEVEL_CER_THRESHOLDS[index]) {
            const uint32_t upper = DAB_ANTENNA_LEVEL_CER_THRESHOLDS[index - 1];
            const uint32_t lower = DAB_ANTENNA_LEVEL_CER_THRESHOLDS[index];
            const uint32_t range = upper - lower;
            const uint32_t position = upper - msc_cer;
            const uint32_t low_quality = static_cast<uint32_t>((index - 1) * 100 / 6);
            const uint32_t high_quality = static_cast<uint32_t>(index * 100 / 6);
            return static_cast<uint8_t>(low_quality + (high_quality - low_quality) * position / range);
        }
    }

    return 100;
}
