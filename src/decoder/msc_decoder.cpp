//
// Created by tarek on 10/1/26.
//

/*
 * Portions of this file are derived from the IRT DAB library.
 * Copyright (C) 2018 IRT GmbH, Fabian Sattler.
 * Licensed under the GNU Lesser General Public License 2.1 or later.
 */

#include "msc_decoder.h"
#include <stdexcept>

extern "C" {
#include "fec/fec.h"
}

MSCDecoder::MSCDecoder(const std::function<void(const AccessUnit &)> &access_unit_callback, const PADCallback &pad_callback) {
    this->access_unit_callback = access_unit_callback;
    this->pad_callback = pad_callback;
}

void MSCDecoder::Superframe::clear() {
    data.clear();
    access_unit_starts.clear();
    access_unit_lengths.clear();
    access_unit_count = 0;
    sampling_rate = 0;
    channels = 0;
    sbr = false;
}

MSCDecoder::ReedSolomonDecoder::ReedSolomonDecoder() {
    handle = init_rs_char(8, 0x11D, 0, 1, 10, 135);

    if (handle == nullptr) {
        throw std::runtime_error("Failed to initialize Reed-Solomon decoder");
    }
}

MSCDecoder::ReedSolomonDecoder::~ReedSolomonDecoder() {
    free_rs_char(handle);
}

void MSCDecoder::ReedSolomonDecoder::decode(std::vector<uint8_t> &data) {
    const int subchannel_index = static_cast<int>(data.size() / 120);

    if (subchannel_index == 0) {
        return;
    }

    for (int packet_index = 0; packet_index < subchannel_index; packet_index++) {
        for (int position = 0; position < 120; position++) {
            packet[position] = data[position * subchannel_index + packet_index];
        }

        const int correction_count = decode_rs_char(handle, packet, correction_positions, 0);

        if (correction_count <= 0) {
            continue;
        }

        for (int correction = 0; correction < correction_count; correction++) {
            const int position = correction_positions[correction] - 135;

            if (position >= 0) {
                data[position * subchannel_index + packet_index] = packet[position];
            }
        }
    }
}

void MSCDecoder::reset() {
    current_superframe.clear();
    superframe_active = false;
    superframe_frame_count = 0;
    input_buffer.clear();
    frame_alignment_valid = false;
    aligned_frame_count = 0;
}

void MSCDecoder::set_bitrate(const uint16_t bitrate) {
    frame_size = static_cast<uint16_t>(bitrate * 3);
    superframe_size = static_cast<uint16_t>((bitrate / 8) * 110);
    reset();
}

void MSCDecoder::process(const std::vector<uint8_t> &data) {
    if (frame_size == 0 || data.empty()) {
        return;
    }

    synchronize(data);
}

void MSCDecoder::synchronize(const std::vector<uint8_t> &data) {
    std::vector<uint8_t> buffer;
    buffer.reserve(input_buffer.size() + data.size());
    buffer.insert(buffer.end(), input_buffer.begin(), input_buffer.end());
    buffer.insert(buffer.end(), data.begin(), data.end());
    input_buffer.clear();

    size_t offset = 0;

    while (offset + frame_size < buffer.size()) {
        if (frame_alignment_valid) {
            process_frame(std::vector(buffer.begin() + offset, buffer.begin() + offset + frame_size));
            offset += frame_size;
            aligned_frame_count++;

            if (aligned_frame_count == 5) {
                frame_alignment_valid = false;
                aligned_frame_count = 0;
            }

            continue;
        }

        const uint8_t *frame = buffer.data() + offset;

        if ((frame[0] != 0 || frame[1] != 0) && check_firecode(frame)) {
            process_frame(std::vector(buffer.begin() + offset, buffer.begin() + offset + frame_size));
            offset += frame_size;
            frame_alignment_valid = true;
            aligned_frame_count = 1;
            continue;
        }

        offset++;
    }

    input_buffer.insert(input_buffer.end(), buffer.begin() + offset, buffer.end());
}

void MSCDecoder::process_frame(const std::vector<uint8_t> &frame) {
    if (superframe_frame_count == 0) {
        if (!start_superframe(frame)) {
            return;
        }

        superframe_frame_count = 1;
        superframe_active = true;
        return;
    }

    if (!superframe_active) {
        return;
    }

    current_superframe.data.insert(current_superframe.data.end(), frame.begin(), frame.end());
    superframe_frame_count++;

    if (superframe_frame_count == 5) {
        finish_superframe();
        superframe_frame_count = 0;
        superframe_active = false;
    }
}

bool MSCDecoder::start_superframe(const std::vector<uint8_t> &frame) {
    if (!check_firecode(frame.data())) {
        return false;
    }

    current_superframe.clear();

    const bool dac_rate = (frame[2] & 0x40) != 0;
    current_superframe.sbr = (frame[2] & 0x20) != 0;
    current_superframe.channels = (frame[2] & 0x10) != 0 ? 2 : 1;
    current_superframe.sampling_rate = dac_rate ? 48000 : 32000;

    if (!dac_rate && current_superframe.sbr) {
        current_superframe.access_unit_count = 2;
        current_superframe.access_unit_starts.push_back(5);
    } else if (dac_rate && current_superframe.sbr) {
        current_superframe.access_unit_count = 3;
        current_superframe.access_unit_starts.push_back(6);
    } else if (!dac_rate) {
        current_superframe.access_unit_count = 4;
        current_superframe.access_unit_starts.push_back(8);
    } else {
        current_superframe.access_unit_count = 6;
        current_superframe.access_unit_starts.push_back(11);
    }

    auto position = frame.cbegin() + 3;

    for (uint8_t index = 1; index < current_superframe.access_unit_count; index++) {
        uint16_t access_unit_start;

        if ((index % 2) != 0) {
            access_unit_start = static_cast<uint16_t>((*position++ << 4) | ((*position & 0xF0) >> 4));
        } else {
            access_unit_start = static_cast<uint16_t>(((*position++ & 0x0F) << 8) | *position++);
        }

        if (access_unit_start > superframe_size || access_unit_start <= current_superframe.access_unit_starts[index - 1]) {
            current_superframe.clear();
            return false;
        }

        current_superframe.access_unit_starts.push_back(access_unit_start);
    }

    current_superframe.access_unit_starts.push_back(superframe_size);

    for (uint8_t index = 0; index < current_superframe.access_unit_count; index++) {
        current_superframe.access_unit_lengths.push_back(current_superframe.access_unit_starts[index + 1] - current_superframe.access_unit_starts[index]);
    }

    current_superframe.data.insert(current_superframe.data.end(), frame.begin(), frame.end());
    return true;
}

void MSCDecoder::finish_superframe() {
    reed_solomon_decoder.decode(current_superframe.data);

    if (current_superframe.data.size() < superframe_size) {
        return;
    }

    for (uint8_t index = 0; index < current_superframe.access_unit_count; index++) {
        emit_access_unit(current_superframe.access_unit_starts[index], current_superframe.access_unit_lengths[index]);
    }
}

void MSCDecoder::emit_access_unit(const uint16_t start, const uint16_t length) {
    if (length < 2 || start + length > current_superframe.data.size() || !check_crc(current_superframe.data.data() + start, length)) {
        return;
    }

    size_t payload_start = start;
    size_t payload_length = length - 2;

    if (((current_superframe.data[start] >> 5) & 0x07) == 0x04) {
        size_t pad_header_length = 2;
        size_t pad_length = current_superframe.data[start + 1];

        if (pad_length == 0xFF && length >= 3) {
            pad_length += current_superframe.data[start + 2];
            pad_header_length++;
        }

        if (pad_length >= 2 && pad_header_length + pad_length <= payload_length) {
            const size_t pad_start = start + pad_header_length;
            const size_t xpad_length = pad_length - 2;

            if (pad_callback) {
                const std::vector xpad_data(current_superframe.data.begin() + pad_start, current_superframe.data.begin() + pad_start + xpad_length);
                const std::array fpad_data{current_superframe.data[pad_start + xpad_length], current_superframe.data[pad_start + xpad_length + 1]};
                pad_callback(xpad_data, fpad_data);
            }

            payload_start += pad_header_length + pad_length;
            payload_length -= pad_header_length + pad_length;
        }
    }

    if (payload_length == 0 || access_unit_callback == nullptr) {
        return;
    }

    AccessUnit access_unit;
    access_unit.data.insert(access_unit.data.end(), current_superframe.data.begin() + payload_start, current_superframe.data.begin() + payload_start + payload_length);
    access_unit.sampling_rate = current_superframe.sampling_rate;
    access_unit.channels = current_superframe.channels;
    access_unit.sbr = current_superframe.sbr;
    access_unit_callback(access_unit);
}

bool MSCDecoder::check_firecode(const uint8_t *data) {
    uint16_t state = static_cast<uint16_t>(data[2] << 8 | data[3]);

    for (int index = 4; index < 11; index++) {
        const uint16_t next_state = FIRECODE_TABLE[state >> 8];
        state = static_cast<uint16_t>(((next_state & 0x00FF) ^ data[index]) | ((next_state ^ (state << 8)) & 0xFF00));
    }

    for (int index = 0; index < 2; index++) {
        const uint16_t next_state = FIRECODE_TABLE[state >> 8];
        state = static_cast<uint16_t>(((next_state & 0x00FF) ^ data[index]) | ((next_state ^ (state << 8)) & 0xFF00));
    }

    return state == 0;
}

bool MSCDecoder::check_crc(const uint8_t *data, const size_t length) {
    if (length < 2) {
        return false;
    }

    uint16_t crc = 0xFFFF;

    for (size_t index = 0; index < length - 2; index++) {
        crc ^= static_cast<uint16_t>(data[index] << 8);

        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }

    const uint16_t stored_crc = static_cast<uint16_t>(data[length - 2] << 8 | data[length - 1]);
    return crc == static_cast<uint16_t>(stored_crc ^ 0xFFFF);
}

const uint16_t MSCDecoder::FIRECODE_TABLE[256] = {
    0x0000, 0x782f, 0xf05e, 0x8871, 0x9893, 0xe0bc, 0x68cd, 0x10e2,
    0x4909, 0x3126, 0xb957, 0xc178, 0xd19a, 0xa9b5, 0x21c4, 0x59eb,
    0x9212, 0xea3d, 0x624c, 0x1a63, 0x0a81, 0x72ae, 0xfadf, 0x82f0,
    0xdb1b, 0xa334, 0x2b45, 0x536a, 0x4388, 0x3ba7, 0xb3d6, 0xcbf9,
    0x5c0b, 0x2424, 0xac55, 0xd47a, 0xc498, 0xbcb7, 0x34c6, 0x4ce9,
    0x1502, 0x6d2d, 0xe55c, 0x9d73, 0x8d91, 0xf5be, 0x7dcf, 0x05e0,
    0xce19, 0xb636, 0x3e47, 0x4668, 0x568a, 0x2ea5, 0xa6d4, 0xdefb,
    0x8710, 0xff3f, 0x774e, 0x0f61, 0x1f83, 0x67ac, 0xefdd, 0x97f2,
    0xb816, 0xc039, 0x4848, 0x3067, 0x2085, 0x58aa, 0xd0db, 0xa8f4,
    0xf11f, 0x8930, 0x0141, 0x796e, 0x698c, 0x11a3, 0x99d2, 0xe1fd,
    0x2a04, 0x522b, 0xda5a, 0xa275, 0xb297, 0xcab8, 0x42c9, 0x3ae6,
    0x630d, 0x1b22, 0x9353, 0xeb7c, 0xfb9e, 0x83b1, 0x0bc0, 0x73ef,
    0xe41d, 0x9c32, 0x1443, 0x6c6c, 0x7c8e, 0x04a1, 0x8cd0, 0xf4ff,
    0xad14, 0xd53b, 0x5d4a, 0x2565, 0x3587, 0x4da8, 0xc5d9, 0xbdf6,
    0x760f, 0x0e20, 0x8651, 0xfe7e, 0xee9c, 0x96b3, 0x1ec2, 0x66ed,
    0x3f06, 0x4729, 0xcf58, 0xb777, 0xa795, 0xdfba, 0x57cb, 0x2fe4,
    0x0803, 0x702c, 0xf85d, 0x8072, 0x9090, 0xe8bf, 0x60ce, 0x18e1,
    0x410a, 0x3925, 0xb154, 0xc97b, 0xd999, 0xa1b6, 0x29c7, 0x51e8,
    0x9a11, 0xe23e, 0x6a4f, 0x1260, 0x0282, 0x7aad, 0xf2dc, 0x8af3,
    0xd318, 0xab37, 0x2346, 0x5b69, 0x4b8b, 0x33a4, 0xbbd5, 0xc3fa,
    0x5408, 0x2c27, 0xa456, 0xdc79, 0xcc9b, 0xb4b4, 0x3cc5, 0x44ea,
    0x1d01, 0x652e, 0xed5f, 0x9570, 0x8592, 0xfdbd, 0x75cc, 0x0de3,
    0xc61a, 0xbe35, 0x3644, 0x4e6b, 0x5e89, 0x26a6, 0xaed7, 0xd6f8,
    0x8f13, 0xf73c, 0x7f4d, 0x0762, 0x1780, 0x6faf, 0xe7de, 0x9ff1,
    0xb015, 0xc83a, 0x404b, 0x3864, 0x2886, 0x50a9, 0xd8d8, 0xa0f7,
    0xf91c, 0x8133, 0x0942, 0x716d, 0x618f, 0x19a0, 0x91d1, 0xe9fe,
    0x2207, 0x5a28, 0xd259, 0xaa76, 0xba94, 0xc2bb, 0x4aca, 0x32e5,
    0x6b0e, 0x1321, 0x9b50, 0xe37f, 0xf39d, 0x8bb2, 0x03c3, 0x7bec,
    0xec1e, 0x9431, 0x1c40, 0x646f, 0x748d, 0x0ca2, 0x84d3, 0xfcfc,
    0xa517, 0xdd38, 0x5549, 0x2d66, 0x3d84, 0x45ab, 0xcdda, 0xb5f5,
    0x7e0c, 0x0623, 0x8e52, 0xf67d, 0xe69f, 0x9eb0, 0x16c1, 0x6eee,
    0x3705, 0x4f2a, 0xc75b, 0xbf74, 0xaf96, 0xd7b9, 0x5fc8, 0x27e7
};
