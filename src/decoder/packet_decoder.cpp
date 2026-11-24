#include "packet_decoder.h"
#include <algorithm>
#include <array>
#include <stdexcept>

extern "C" {
#include "fec/fec.h"
}

namespace {
constexpr std::array<size_t, 4> PACKET_SIZES{24, 48, 72, 96};
constexpr size_t MAX_BUFFER_SIZE = 8192;
constexpr size_t MAX_DATA_GROUP_SIZE = 64 * 1024;
constexpr size_t FEC_APPLICATION_SIZE = 2256;
constexpr size_t FEC_PACKET_SIZE = 24;
constexpr size_t FEC_PACKET_COUNT = 9;
constexpr size_t FEC_PACKET_DATA_SIZE = 22;
constexpr size_t FEC_PACKET_SET_SIZE = FEC_PACKET_SIZE * FEC_PACKET_COUNT;
constexpr size_t FEC_PARITY_SIZE = 192;
constexpr size_t FEC_FRAME_SIZE = FEC_APPLICATION_SIZE + FEC_PACKET_SET_SIZE;
constexpr uint16_t FEC_PACKET_ADDRESS = 1022;
}

PacketDecoder::PacketDecoder(const DataGroupCallback &data_group_callback) {
    this->data_group_callback = data_group_callback;
    fec_handle = init_rs_char(8, 0x11D, 0, 1, 16, 51);

    if (fec_handle == nullptr) {
        throw std::runtime_error("Failed to initialize packet Reed-Solomon decoder");
    }
}

PacketDecoder::~PacketDecoder() {
    free_rs_char(fec_handle);
}

void PacketDecoder::reset() {
    buffer.clear();
    fec_buffer.clear();
    data_group.clear();
    continuity = -1;
    fec_scheme = 0;
    data_group_active = false;
#if DAB_ENABLE_DIAGNOSTICS
    statistics_input_bytes.store(0, std::memory_order_relaxed);
    statistics_packets_valid.store(0, std::memory_order_relaxed);
    statistics_packet_crc_errors.store(0, std::memory_order_relaxed);
    statistics_packets_addressed.store(0, std::memory_order_relaxed);
    statistics_data_groups.store(0, std::memory_order_relaxed);
    statistics_continuity_errors.store(0, std::memory_order_relaxed);
    statistics_fec_frames.store(0, std::memory_order_relaxed);
    statistics_fec_errors.store(0, std::memory_order_relaxed);
#endif
}

void PacketDecoder::configure(const uint16_t packet_address, const bool data_groups, const uint8_t fec_scheme) {
    reset();
    this->packet_address = packet_address;
    this->data_groups = data_groups;
    this->fec_scheme = fec_scheme;
}

void PacketDecoder::process(const std::vector<uint8_t> &data) {
    if (packet_address == 0xFFFF || data.empty()) {
        return;
    }

#if DAB_ENABLE_DIAGNOSTICS
    statistics_input_bytes.fetch_add(data.size(), std::memory_order_relaxed);
#endif

    if (fec_scheme == 1) {
        process_fec(data);
    } else {
        process_packets(data);
    }
}

void PacketDecoder::process_packets(const std::vector<uint8_t> &data) {
    if (buffer.size() + data.size() > MAX_BUFFER_SIZE) {
        buffer.clear();
    }

    buffer.insert(buffer.end(), data.begin(), data.end());

    while (buffer.size() >= PACKET_SIZES.front()) {
        const size_t packet_size = PACKET_SIZES[buffer[0] >> 6];

        if (buffer.size() < packet_size) {
            bool synchronized = false;

            for (size_t offset = 1; offset + PACKET_SIZES.front() <= buffer.size(); offset++) {
                const size_t candidate_size = PACKET_SIZES[buffer[offset] >> 6];

                if (offset + candidate_size > buffer.size()) {
                    continue;
                }

                const uint16_t candidate_crc = static_cast<uint16_t>(buffer[offset + candidate_size - 2] << 8 | buffer[offset + candidate_size - 1]);

                if (candidate_crc == calculate_crc(buffer.data() + offset, candidate_size - 2)) {
                    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(offset));
                    synchronized = true;
                    break;
                }
            }

            if (!synchronized) {
                return;
            }

            continue;
        }

        const uint16_t stored_crc = static_cast<uint16_t>(buffer[packet_size - 2] << 8 | buffer[packet_size - 1]);

        if (stored_crc != calculate_crc(buffer.data(), packet_size - 2)) {
#if DAB_ENABLE_DIAGNOSTICS
            statistics_packet_crc_errors.fetch_add(1, std::memory_order_relaxed);
#endif
            buffer.erase(buffer.begin());
            continue;
        }

#if DAB_ENABLE_DIAGNOSTICS
        statistics_packets_valid.fetch_add(1, std::memory_order_relaxed);
#endif
        process_packet(buffer.data(), packet_size);
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(packet_size));
    }
}

void PacketDecoder::process_fec(const std::vector<uint8_t> &data) {
    fec_buffer.insert(fec_buffer.end(), data.begin(), data.end());

    while (fec_buffer.size() >= FEC_PACKET_SET_SIZE) {
        size_t fec_offset = fec_buffer.size();

        for (size_t offset = 0; offset + FEC_PACKET_SET_SIZE <= fec_buffer.size(); offset++) {
            if (is_fec_packet_set(fec_buffer.data() + offset, fec_buffer.size() - offset)) {
                fec_offset = offset;
                break;
            }
        }

        if (fec_offset == fec_buffer.size()) {
            if (fec_buffer.size() > FEC_FRAME_SIZE - 1) {
                fec_buffer.erase(fec_buffer.begin(), fec_buffer.end() - static_cast<std::ptrdiff_t>(FEC_FRAME_SIZE - 1));
            }

            return;
        }

        const size_t fec_end = fec_offset + FEC_PACKET_SET_SIZE;

        if (fec_offset < FEC_APPLICATION_SIZE) {
            fec_buffer.erase(fec_buffer.begin(), fec_buffer.begin() + static_cast<std::ptrdiff_t>(fec_end));
            continue;
        }

        const size_t application_offset = fec_offset - FEC_APPLICATION_SIZE;
        std::vector<uint8_t> corrected_data;
#if DAB_ENABLE_DIAGNOSTICS
        statistics_fec_frames.fetch_add(1, std::memory_order_relaxed);
#endif

        if (decode_fec_frame(fec_buffer.data() + application_offset, fec_buffer.data() + fec_offset, corrected_data)) {
            process_packets(corrected_data);
        } else {
#if DAB_ENABLE_DIAGNOSTICS
            statistics_fec_errors.fetch_add(1, std::memory_order_relaxed);
#endif
        }

        fec_buffer.erase(fec_buffer.begin(), fec_buffer.begin() + static_cast<std::ptrdiff_t>(fec_end));
    }
}

bool PacketDecoder::decode_fec_frame(const uint8_t *application_data, const uint8_t *fec_packets, std::vector<uint8_t> &corrected_data) const {
    std::array<uint8_t, FEC_PARITY_SIZE> parity{};
    size_t parity_offset = 0;

    for (size_t packet = 0; packet < FEC_PACKET_COUNT; packet++) {
        const uint8_t *fec_packet = fec_packets + packet * FEC_PACKET_SIZE;
        const size_t copy_length = std::min(FEC_PACKET_DATA_SIZE, FEC_PARITY_SIZE - parity_offset);

        if (copy_length != 0) {
            std::copy_n(fec_packet + 2, copy_length, parity.begin() + static_cast<std::ptrdiff_t>(parity_offset));
            parity_offset += copy_length;
        }
    }

    corrected_data.assign(application_data, application_data + FEC_APPLICATION_SIZE);
    std::array<uint8_t, 204> codeword{};
    std::array<int, 16> correction_positions{};

    for (size_t row = 0; row < 12; row++) {
        for (size_t column = 0; column < 188; column++) {
            codeword[column] = corrected_data[column * 12 + row];
        }

        for (size_t column = 0; column < 16; column++) {
            codeword[188 + column] = parity[column * 12 + row];
        }

        if (decode_rs_char(fec_handle, codeword.data(), correction_positions.data(), 0) < 0) {
            corrected_data.clear();
            return false;
        }

        for (size_t column = 0; column < 188; column++) {
            corrected_data[column * 12 + row] = codeword[column];
        }
    }

    return true;
}

bool PacketDecoder::is_fec_packet_set(const uint8_t *data, const size_t length) {
    if (length < FEC_PACKET_SET_SIZE) {
        return false;
    }

    for (uint16_t counter = 0; counter < FEC_PACKET_COUNT; counter++) {
        const uint16_t header = static_cast<uint16_t>(counter << 10 | FEC_PACKET_ADDRESS);
        const uint8_t *packet = data + counter * FEC_PACKET_SIZE;

        if (packet[0] != static_cast<uint8_t>(header >> 8) || packet[1] != static_cast<uint8_t>(header)) {
            return false;
        }
    }

    // Ignore the unused tail; RS and packet CRC checks validate the protected data.
    return true;
}

void PacketDecoder::process_packet(const uint8_t *packet, const size_t packet_size) {
    const uint8_t packet_continuity = packet[0] >> 4 & 0x03;
    const bool first = (packet[0] & 0x08) != 0;
    const bool last = (packet[0] & 0x04) != 0;
    const uint16_t address = static_cast<uint16_t>((packet[0] & 0x03) << 8 | packet[1]);
    const bool command = (packet[2] & 0x80) != 0;
    const size_t useful_length = packet[2] & 0x7F;
    const size_t maximum_useful_length = packet_size - 5;

    if (address != packet_address) {
        return;
    }

#if DAB_ENABLE_DIAGNOSTICS
    statistics_packets_addressed.fetch_add(1, std::memory_order_relaxed);
#endif

    if (command || useful_length > maximum_useful_length) {
        return;
    }

    if (!data_groups) {
        if (useful_length != 0 && data_group_callback) {
#if DAB_ENABLE_DIAGNOSTICS
            statistics_data_groups.fetch_add(1, std::memory_order_relaxed);
#endif
            data_group_callback(std::vector<uint8_t>(packet + 3, packet + 3 + useful_length));
        }

        return;
    }

    if (first) {
        data_group.clear();
        data_group_active = true;
        continuity = packet_continuity;
    } else {
        if (!data_group_active || continuity < 0 || packet_continuity != (continuity + 1) % 4) {
#if DAB_ENABLE_DIAGNOSTICS
            statistics_continuity_errors.fetch_add(1, std::memory_order_relaxed);
#endif
            data_group.clear();
            data_group_active = false;
            continuity = -1;
            return;
        }

        continuity = packet_continuity;
    }

    if (data_group.size() + useful_length > MAX_DATA_GROUP_SIZE) {
        data_group.clear();
        data_group_active = false;
        continuity = -1;
        return;
    }

    data_group.insert(data_group.end(), packet + 3, packet + 3 + useful_length);

    if (!last) {
        return;
    }

    if (data_group_callback) {
#if DAB_ENABLE_DIAGNOSTICS
        statistics_data_groups.fetch_add(1, std::memory_order_relaxed);
#endif
        data_group_callback(data_group);
    }

    data_group.clear();
    data_group_active = false;
    continuity = -1;
}

#if DAB_ENABLE_DIAGNOSTICS
PacketDecoder::Statistics PacketDecoder::get_statistics() const {
    return {
        statistics_input_bytes.load(std::memory_order_relaxed),
        statistics_packets_valid.load(std::memory_order_relaxed),
        statistics_packet_crc_errors.load(std::memory_order_relaxed),
        statistics_packets_addressed.load(std::memory_order_relaxed),
        statistics_data_groups.load(std::memory_order_relaxed),
        statistics_continuity_errors.load(std::memory_order_relaxed),
        statistics_fec_frames.load(std::memory_order_relaxed),
        statistics_fec_errors.load(std::memory_order_relaxed)
    };
}
#endif

uint16_t PacketDecoder::calculate_crc(const uint8_t *data, const size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t offset = 0; offset < length; offset++) {
        crc ^= static_cast<uint16_t>(data[offset]) << 8;

        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>(crc << 1 ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }

    return static_cast<uint16_t>(~crc);
}
