#ifndef PACKET_DECODER_H
#define PACKET_DECODER_H

#include "../config/features.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class PacketDecoder {
public:
#if DAB_ENABLE_DIAGNOSTICS
    struct Statistics {
        uint64_t input_bytes = 0;
        uint64_t packets_valid = 0;
        uint64_t packet_crc_errors = 0;
        uint64_t packets_addressed = 0;
        uint64_t data_groups = 0;
        uint64_t continuity_errors = 0;
        uint64_t fec_frames = 0;
        uint64_t fec_errors = 0;
    };
#endif

    using DataGroupCallback = std::function<void(const std::vector<uint8_t> &)>;

    explicit PacketDecoder(const DataGroupCallback &data_group_callback);

    ~PacketDecoder();

    void reset();

    void configure(uint16_t packet_address, bool data_groups, uint8_t fec_scheme);

    void process(const std::vector<uint8_t> &data);

#if DAB_ENABLE_DIAGNOSTICS
    Statistics get_statistics() const;
#endif

private:
    DataGroupCallback data_group_callback = nullptr;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> fec_buffer;
    std::vector<uint8_t> data_group;
    uint16_t packet_address = 0xFFFF;
    int continuity = -1;
    uint8_t fec_scheme = 0;
    bool data_groups = false;
    bool data_group_active = false;
    void *fec_handle = nullptr;
#if DAB_ENABLE_DIAGNOSTICS
    std::atomic<uint64_t> statistics_input_bytes{0};
    std::atomic<uint64_t> statistics_packets_valid{0};
    std::atomic<uint64_t> statistics_packet_crc_errors{0};
    std::atomic<uint64_t> statistics_packets_addressed{0};
    std::atomic<uint64_t> statistics_data_groups{0};
    std::atomic<uint64_t> statistics_continuity_errors{0};
    std::atomic<uint64_t> statistics_fec_frames{0};
    std::atomic<uint64_t> statistics_fec_errors{0};
#endif

    void process_packets(const std::vector<uint8_t> &data);

    void process_fec(const std::vector<uint8_t> &data);

    bool decode_fec_frame(const uint8_t *application_data, const uint8_t *fec_packets, std::vector<uint8_t> &corrected_data) const;

    static bool is_fec_packet_set(const uint8_t *data, size_t length);

    void process_packet(const uint8_t *packet, size_t packet_size);

    static uint16_t calculate_crc(const uint8_t *data, size_t length);
};

#endif //PACKET_DECODER_H
