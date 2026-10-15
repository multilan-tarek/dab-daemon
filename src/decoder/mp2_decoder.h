#ifndef MP2_DECODER_H
#define MP2_DECODER_H

#include "../helper/audio_statistics.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class MP2Decoder {
public:
    struct Frame {
        std::vector<uint8_t> data;
        uint32_t sampling_rate = 0;
        uint16_t bitrate = 0;
        uint8_t channels = 0;
    };

    using FrameCallback = std::function<void(const Frame &)>;
    using PADCallback = std::function<void(const std::vector<uint8_t> &, const std::array<uint8_t, 2> &)>;

    MP2Decoder(const FrameCallback &frame_callback, const PADCallback &pad_callback);

    void reset();

    void set_bitrate(uint16_t bitrate);

    void process(const std::vector<uint8_t> &data);

#if DAB_ENABLE_DIAGNOSTICS
    AudioStatistics get_statistics();
#endif

private:
    struct Header {
        bool valid = false;
        uint32_t sampling_rate = 0;
        uint16_t bitrate = 0;
        uint16_t frame_size = 0;
        uint8_t channels = 0;
    };

    FrameCallback frame_callback = nullptr;
    PADCallback pad_callback = nullptr;
    std::vector<uint8_t> input_buffer;
    uint16_t expected_bitrate = 0;
    size_t last_xpad_length = 0;

#if DAB_ENABLE_DIAGNOSTICS
    AudioStatistics statistics;
#endif

    Header parse_header(const uint8_t *data) const;

    void emit_frame(const uint8_t *data, const Header &header);
};

#endif //MP2_DECODER_H
