#ifndef TPEG_DECODER_H
#define TPEG_DECODER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class TPEGDecoder {
public:
    struct ServiceIdentifier {
        uint8_t a = 0;
        uint8_t b = 0;
        uint8_t c = 0;

        bool operator==(const ServiceIdentifier &other) const = default;
    };

    struct Component {
        uint8_t id = 0;
        uint16_t header_crc = 0;
        std::vector<uint8_t> data;

        bool operator==(const Component &other) const = default;
    };

    struct Frame {
        uint64_t sequence = 0;
        uint8_t type = 0;
        ServiceIdentifier service;
        uint8_t encryption = 0;
        std::vector<Component> components;
        std::vector<uint8_t> data;

        bool operator==(const Frame &other) const = default;
    };

    struct Status {
        uint64_t frames_received = 0;
        uint64_t groups_received = 0;
        uint64_t group_crc_errors = 0;
        uint64_t transport_crc_errors = 0;
        uint64_t directory_crc_errors = 0;
        uint64_t component_crc_errors = 0;
        uint64_t continuity_errors = 0;
        uint64_t malformed_frames = 0;
        uint16_t service_count = 0;

        bool operator==(const Status &other) const = default;
    };

    using UpdateCallback = std::function<void(const Status &, const std::vector<ServiceIdentifier> &)>;
    using FrameCallback = std::function<void(const Frame &)>;

    TPEGDecoder(const UpdateCallback &update_callback, const FrameCallback &frame_callback);

    void reset();

    void process_group(const std::vector<uint8_t> &group);

private:
    UpdateCallback update_callback = nullptr;
    FrameCallback frame_callback = nullptr;
    Status status;
    std::vector<ServiceIdentifier> services;
    uint64_t sequence = 0;
    bool continuity_valid = false;
    uint8_t continuity_index = 0;

    void process_stream(const uint8_t *data, size_t length);

    bool process_transport_frame(const uint8_t *data, size_t length);

    bool process_directory(const uint8_t *data, size_t length, Frame &frame);

    bool process_service_data(const uint8_t *data, size_t length, Frame &frame);

    static uint16_t calculate_crc(const uint8_t *data, size_t length);

    static uint16_t read_uint16(const uint8_t *data);

    void emit_update() const;
};

#endif //TPEG_DECODER_H
