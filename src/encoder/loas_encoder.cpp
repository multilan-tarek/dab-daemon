//
// Created by tarek on 10/1/26.
//

/*
 * Portions of this file are derived from the IRT DAB library.
 * Copyright (C) 2018 IRT GmbH, Fabian Sattler.
 * Licensed under the GNU Lesser General Public License 2.1 or later.
 */

#include "loas_encoder.h"
#include <algorithm>

namespace {
    class BitWriter {
    public:
        void add_bits(const uint32_t value, size_t count) {
            while (count > 0) {
                if (byte_bits == 0) {
                    data.push_back(0);
                }

                const size_t copy_bits = std::min(count, 8 - byte_bits);
                const uint8_t copy_data = static_cast<uint8_t>((value >> (count - copy_bits)) & (0xFF >> (8 - copy_bits)));
                data.back() |= static_cast<uint8_t>(copy_data << (8 - byte_bits - copy_bits));
                byte_bits = (byte_bits + copy_bits) % 8;
                count -= copy_bits;
            }
        }

        void add_bytes(const uint8_t *input, const size_t length) {
            for (size_t index = 0; index < length; index++) {
                add_bits(input[index], 8);
            }
        }

        std::vector<uint8_t> finish() {
            const size_t audio_mux_length = data.size() - 3;
            data[1] |= static_cast<uint8_t>((audio_mux_length >> 8) & 0x1F);
            data[2] = static_cast<uint8_t>(audio_mux_length & 0xFF);
            return data;
        }

    private:
        std::vector<uint8_t> data;
        size_t byte_bits = 0;
    };
}

std::vector<uint8_t> LOASEncoder::encode(const std::vector<uint8_t> &access_unit, const uint32_t sampling_rate, const uint8_t channels, const bool sbr) {
    BitWriter writer;

    writer.add_bits(0x2B7, 11);
    writer.add_bits(0, 13);
    writer.add_bits(0, 1);
    writer.add_bits(0, 1);
    writer.add_bits(1, 1);
    writer.add_bits(0, 6);
    writer.add_bits(0, 4);
    writer.add_bits(0, 3);

    const uint8_t output_sample_rate_index = sampling_rate == 48000 ? 3 : 5;
    const uint8_t core_sample_rate_index = sbr ? (sampling_rate == 48000 ? 6 : 8) : output_sample_rate_index;
    const uint8_t channel_configuration = channels == 2 ? 2 : 1;

    if (sbr) {
        writer.add_bits(0b00101, 5);
        writer.add_bits(core_sample_rate_index, 4);
        writer.add_bits(channel_configuration, 4);
        writer.add_bits(output_sample_rate_index, 4);
        writer.add_bits(0b00010, 5);
        writer.add_bits(0b100, 3);
    } else {
        writer.add_bits(0b00010, 5);
        writer.add_bits(core_sample_rate_index, 4);
        writer.add_bits(channel_configuration, 4);
        writer.add_bits(0b100, 3);
    }

    writer.add_bits(0, 3);
    writer.add_bits(0xFF, 8);
    writer.add_bits(0, 1);
    writer.add_bits(0, 1);

    for (size_t index = 0; index < access_unit.size() / 255; index++) {
        writer.add_bits(0xFF, 8);
    }

    writer.add_bits(access_unit.size() % 255, 8);
    writer.add_bytes(access_unit.data(), access_unit.size());
    return writer.finish();
}
