#ifndef SIGNAL_QUALITY_FILTER_H
#define SIGNAL_QUALITY_FILTER_H

#include <cstdint>

class SignalQualityFilter {
public:
    struct Status {
        bool msc_cer_valid = false;
        uint8_t antenna_level = 0;
        uint32_t msc_cer = 0;
        bool fic_crc_error_rate_valid = false;
        uint32_t fic_crc_error_rate = 0;
        bool signal_quality_valid = false;
        uint8_t signal_quality = 0;

        bool operator==(const Status &other) const = default;
    };

    void reset();

    Status update(bool ofdm_lock, bool fec_lock, bool fec_sync, bool msc_cer_valid, uint8_t antenna_level, uint32_t msc_cer, bool fic_crc_error_rate_valid, uint32_t fic_crc_error_rate);

private:
    bool msc_cer_initialized = false;
    uint32_t smoothed_msc_cer = 0;
    bool fic_crc_error_rate_initialized = false;
    uint32_t smoothed_fic_crc_error_rate = 0;

    static uint32_t smooth(uint32_t previous, uint32_t current);


    static uint8_t calculate_msc_quality(uint32_t msc_cer);
};

#endif
