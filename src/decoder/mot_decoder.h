#ifndef MOT_DECODER_H
#define MOT_DECODER_H

#include "../config/features.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

class MOTDecoder {
public:
#if DAB_ENABLE_DIAGNOSTICS
    struct Statistics {
        uint64_t groups_received = 0;
        uint64_t groups_accepted = 0;
        uint64_t group_crc_errors = 0;
        uint64_t segments = 0;
        uint64_t directories = 0;
        uint64_t objects = 0;
    };
#endif

    struct Object {
        uint16_t transport_id = 0;
        uint32_t body_size = 0;
        uint8_t content_type = 0;
        uint16_t content_subtype = 0;
        std::string content_name;
        std::string mime_type;
        int compression_type = -1;
        std::vector<uint8_t> profile_subset;
        std::vector<uint8_t> scope_start;
        std::vector<uint8_t> scope_end;
        std::vector<uint8_t> scope_id;
        std::vector<uint8_t> body;
    };

    using ObjectCallback = std::function<void(const Object &)>;
    using ObjectRemovedCallback = std::function<void(uint16_t)>;

    explicit MOTDecoder(const ObjectCallback &object_callback, const ObjectRemovedCallback &object_removed_callback = nullptr);

    void reset();

    void configure_xpad(uint8_t application_type);

    void process_xpad(int type, const std::vector<uint8_t> &data);

    void process_group(const std::vector<uint8_t> &group);

#if DAB_ENABLE_DIAGNOSTICS
    Statistics get_statistics() const;
#endif

private:
    struct Segments {
        std::map<uint16_t, std::vector<uint8_t>> values;
        int last = -1;

        void reset();

        bool complete() const;

        std::vector<uint8_t> get_data() const;
    };

    struct ObjectState {
        Segments header_segments;
        Segments body_segments;
        Object object;
        bool header_complete = false;
        bool emitted = false;
    };

    struct DirectoryState {
        Segments segments;
        uint8_t type = 0;
        bool processed = false;
    };

    ObjectCallback object_callback = nullptr;
    ObjectRemovedCallback object_removed_callback = nullptr;
    size_t xpad_data_group_length = 0;
    std::vector<uint8_t> xpad_data_group;
    uint8_t xpad_application_type = 0xFF;
    std::map<uint16_t, ObjectState> objects;
    std::map<uint16_t, DirectoryState> directories;
    std::vector<uint16_t> active_directory_objects;
#if DAB_ENABLE_DIAGNOSTICS
    std::atomic<uint64_t> statistics_groups_received{0};
    std::atomic<uint64_t> statistics_groups_accepted{0};
    std::atomic<uint64_t> statistics_group_crc_errors{0};
    std::atomic<uint64_t> statistics_segments{0};
    std::atomic<uint64_t> statistics_directories{0};
    std::atomic<uint64_t> statistics_objects{0};
#endif

    void process_segment(uint8_t type, uint16_t transport_id, uint16_t segment_number, bool last, const std::vector<uint8_t> &data);

    void process_directory(uint16_t transport_id, uint8_t type);

    bool parse_directory(const std::vector<uint8_t> &data);

    bool parse_header(uint16_t transport_id, const std::vector<uint8_t> &header);

    void update_object(uint16_t transport_id);

    static bool read_parameter(const std::vector<uint8_t> &data, size_t &offset, uint8_t &parameter_id, std::vector<uint8_t> &parameter_data);

    static bool decompress_gzip(const uint8_t *data, size_t length, size_t expected_length, std::vector<uint8_t> &result);

    static uint16_t calculate_crc(const uint8_t *data, size_t length);
};

#endif //MOT_DECODER_H
