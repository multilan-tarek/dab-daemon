#ifndef MSC_DECODER_H
#define MSC_DECODER_H

#include "../helper/audio_statistics.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

class MSCDecoder {
public:
    struct AccessUnit {
        std::vector<uint8_t> data;
        uint32_t sampling_rate = 0;
        uint8_t channels = 0;
        bool sbr = false;
    };

    using PADCallback = std::function<void(const std::vector<uint8_t> &, const std::array<uint8_t, 2> &)>;

    MSCDecoder(const std::function<void(const AccessUnit &)> &access_unit_callback, const PADCallback &pad_callback);

    void reset();

    void set_bitrate(uint16_t bitrate);

    void process(const std::vector<uint8_t> &data);

#if DAB_ENABLE_DIAGNOSTICS
    AudioStatistics get_statistics();
#endif

private:
    struct Superframe {
        std::vector<uint8_t> data;
        std::vector<uint16_t> access_unit_starts;
        std::vector<uint16_t> access_unit_lengths;
        uint8_t access_unit_count = 0;
        uint32_t sampling_rate = 0;
        uint8_t channels = 0;
        bool sbr = false;

        void clear();
    };

    class ReedSolomonDecoder {
    public:
        ReedSolomonDecoder();

        ~ReedSolomonDecoder();

        std::pair<uint32_t, uint32_t> decode(std::vector<uint8_t> &data);

    private:
        void *handle = nullptr;
        uint8_t packet[120]{};
        int correction_positions[10]{};
    };

    std::function<void(const AccessUnit &)> access_unit_callback = nullptr;
    PADCallback pad_callback = nullptr;
    ReedSolomonDecoder reed_solomon_decoder;

    Superframe current_superframe;
    bool superframe_active = false;
    uint8_t superframe_frame_count = 0;

    uint16_t frame_size = 0;
    uint16_t superframe_size = 0;

    std::vector<uint8_t> input_buffer;
    bool frame_alignment_valid = false;
    uint8_t aligned_frame_count = 0;

#if DAB_ENABLE_DIAGNOSTICS
    AudioStatistics statistics;
#endif

    void synchronize(const std::vector<uint8_t> &data);

    void process_frame(const std::vector<uint8_t> &frame);

    bool start_superframe(const std::vector<uint8_t> &frame);

    void finish_superframe();

    void emit_access_unit(uint16_t start, uint16_t length);

    static bool check_firecode(const uint8_t *data);

    static bool check_crc(const uint8_t *data, size_t length);

    static const uint16_t FIRECODE_TABLE[256];
};

#endif //MSC_DECODER_H
