#include "mp2_decoder.h"

namespace {
constexpr uint16_t MPEG1_LAYER_II_BITRATES[16] = {
    0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0
};

constexpr uint16_t MPEG2_LAYER_II_BITRATES[16] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
};

constexpr size_t XPAD_LENGTHS[8] = {4, 6, 8, 12, 16, 24, 32, 48};
}

MP2Decoder::MP2Decoder(const FrameCallback &frame_callback, const PADCallback &pad_callback) {
    this->frame_callback = frame_callback;
    this->pad_callback = pad_callback;
}

void MP2Decoder::reset() {
    input_buffer.clear();
    last_xpad_length = 0;
#if DAB_ENABLE_DIAGNOSTICS
    statistics = {};
#endif
}

#if DAB_ENABLE_DIAGNOSTICS
AudioStatistics MP2Decoder::get_statistics() {
    statistics.check_gap();
    return statistics;
}
#endif

void MP2Decoder::set_bitrate(const uint16_t bitrate) {
    expected_bitrate = bitrate;
    reset();
}

void MP2Decoder::process(const std::vector<uint8_t> &data) {
    if (expected_bitrate == 0 || data.empty()) {
        return;
    }

    input_buffer.insert(input_buffer.end(), data.begin(), data.end());
    size_t offset = 0;

    while (offset + 4 <= input_buffer.size()) {
        const Header header = parse_header(input_buffer.data() + offset);

        if (!header.valid) {
#if DAB_ENABLE_DIAGNOSTICS
            statistics.record_sync_loss();
#endif
            offset++;
            continue;
        }

        if (offset + header.frame_size > input_buffer.size()) {
            break;
        }

#if DAB_ENABLE_DIAGNOSTICS
        statistics.frames_received++;
#endif
        if (offset + header.frame_size + 4 <= input_buffer.size()) {
            const Header next_header = parse_header(input_buffer.data() + offset + header.frame_size);

            if (!next_header.valid) {
#if DAB_ENABLE_DIAGNOSTICS
                statistics.record_discard();
                statistics.record_sync_loss();
#endif
                offset++;
                continue;
            }
        }

#if DAB_ENABLE_DIAGNOSTICS
        statistics.synchronized = true;
        statistics.sampling_rate = header.sampling_rate;
        statistics.channels = header.channels;
#endif
        emit_frame(input_buffer.data() + offset, header);
        offset += header.frame_size;
    }

    if (offset != 0) {
        input_buffer.erase(input_buffer.begin(), input_buffer.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    if (input_buffer.size() > 65536) {
        input_buffer.erase(input_buffer.begin(), input_buffer.end() - 3);
    }
}

MP2Decoder::Header MP2Decoder::parse_header(const uint8_t *data) const {
    Header result;

    if (data[0] != 0xFF || (data[1] & 0xE0) != 0xE0) {
        return result;
    }

    const uint8_t version = static_cast<uint8_t>((data[1] >> 3) & 0x03);
    const uint8_t layer = static_cast<uint8_t>((data[1] >> 1) & 0x03);
    const bool crc_present = (data[1] & 0x01) == 0;
    const uint8_t bitrate_index = static_cast<uint8_t>((data[2] >> 4) & 0x0F);
    const uint8_t sampling_rate_index = static_cast<uint8_t>((data[2] >> 2) & 0x03);
    const bool padding = (data[2] & 0x02) != 0;

    if ((version != 0x03 && version != 0x02) || layer != 0x02 || !crc_present || bitrate_index == 0 || bitrate_index == 0x0F || sampling_rate_index == 0x03) {
        return result;
    }

    const bool mpeg1 = version == 0x03;
    const uint16_t bitrate = mpeg1 ? MPEG1_LAYER_II_BITRATES[bitrate_index] : MPEG2_LAYER_II_BITRATES[bitrate_index];
    const uint32_t sampling_rate = mpeg1 ? (sampling_rate_index == 1 ? 48000 : 0) : (sampling_rate_index == 1 ? 24000 : 0);

    if (bitrate == 0 || sampling_rate == 0 || bitrate != expected_bitrate) {
        return result;
    }

    const uint32_t frame_size = 144000U * bitrate / sampling_rate + static_cast<uint32_t>(padding);

    if (frame_size < 8 || frame_size > 8192) {
        return result;
    }

    const uint8_t channel_mode = static_cast<uint8_t>((data[3] >> 6) & 0x03);
    result.valid = true;
    result.sampling_rate = sampling_rate;
    result.bitrate = bitrate;
    result.frame_size = static_cast<uint16_t>(frame_size);
    result.channels = channel_mode == 0x03 ? 1 : 2;
    return result;
}

void MP2Decoder::emit_frame(const uint8_t *data, const Header &header) {
    if (header.frame_size < 4) {
        return;
    }

    const std::array<uint8_t, 2> fpad_data{data[header.frame_size - 2], data[header.frame_size - 1]};
    const uint8_t xpad_indicator = static_cast<uint8_t>((fpad_data[0] & 0x30) >> 4);
    const bool content_indicator_present = (fpad_data[1] & 0x02) != 0;
    size_t xpad_length = 0;

    if (xpad_indicator == 1) {
        xpad_length = 4;
    } else if (xpad_indicator == 2) {
        if (content_indicator_present) {
            size_t announced_length = 0;
            size_t indicator_length = 0;
            const size_t xpad_end = header.frame_size - 4;

            while (indicator_length < 4 && indicator_length < xpad_end) {
                const uint8_t indicator = data[xpad_end - 1 - indicator_length];
                indicator_length++;

                if ((indicator & 0x1F) == 0) {
                    break;
                }

                announced_length += XPAD_LENGTHS[indicator >> 5];
            }

            xpad_length = indicator_length + announced_length;

            if (xpad_length > 196 || xpad_length > xpad_end) {
                xpad_length = 0;
            }
        } else {
            xpad_length = last_xpad_length;
        }
    }

    if (xpad_indicator == 0) {
        last_xpad_length = 0;
    } else if (xpad_length != 0) {
        last_xpad_length = xpad_length;
    }

    if (pad_callback && xpad_length != 0 && xpad_length + 4 <= header.frame_size) {
        const size_t xpad_end = header.frame_size - 4;
        const size_t xpad_start = xpad_end - xpad_length;
        pad_callback(std::vector<uint8_t>(data + xpad_start, data + xpad_end), fpad_data);
    }

    if (!frame_callback) {
        return;
    }

    Frame frame;
    frame.data.assign(data, data + header.frame_size);
    frame.sampling_rate = header.sampling_rate;
    frame.bitrate = header.bitrate;
    frame.channels = header.channels;
#if DAB_ENABLE_DIAGNOSTICS
    statistics.record_output();
#endif
    frame_callback(frame);
}
