#ifndef JOURNALINE_DECODER_H
#define JOURNALINE_DECODER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

class JournalineDecoder {
public:
    struct Link {
        uint16_t object_id = 0;
        std::string text;

        bool operator==(const Link &other) const = default;
    };

    struct GeneralLink {
        uint8_t type = 0;
        std::string address;
        std::string label;

        bool operator==(const GeneralLink &other) const = default;
    };

    struct Object {
        uint16_t id = 0;
        uint8_t type = 0;
        bool static_content = false;
        uint8_t revision = 0;
        std::string title;
        std::string body;
        std::vector<Link> links;
        std::vector<std::vector<std::string>> items;
        std::vector<GeneralLink> general_links;
        std::string language;
        bool expiry_present = false;
        int64_t expiry = 0;

        bool operator==(const Object &other) const = default;
    };

    struct IndexEntry {
        uint16_t id = 0;
        uint8_t type = 0;
        bool static_content = false;
        uint8_t revision = 0;
        std::string title;

        bool operator==(const IndexEntry &other) const = default;
    };

    struct Status {
        bool configured = false;
        uint8_t version = 0;
        uint8_t extended_header_length = 0;
        uint16_t point_of_entry = 0;
        bool toc_present = false;
        uint8_t toc_revision = 0;
        uint16_t object_count = 0;
        uint16_t timeout_minutes = 0;

        bool operator==(const Status &other) const = default;
    };

    using UpdateCallback = std::function<void(const Status &, const std::vector<Object> &)>;

    explicit JournalineDecoder(const UpdateCallback &update_callback);

    void reset();

    bool configure(const std::vector<uint8_t> &parameters);

    bool configure_xpad(uint8_t application_type, const std::vector<uint8_t> &parameters);

    void process_xpad(int type, const std::vector<uint8_t> &data);

    void process_group(const std::vector<uint8_t> &group);

    void update_time(int64_t unix_time);

    std::vector<IndexEntry> get_index() const;

    std::vector<Object> get_object(uint16_t object_id) const;

private:
    struct TocFragment {
        uint16_t preceding_object_id = 0;
        std::vector<uint16_t> object_ids;
    };

    UpdateCallback update_callback = nullptr;
    Status status;
    std::map<uint16_t, Object> objects;
    std::map<uint8_t, TocFragment> toc_fragments;
    uint8_t toc_block_count = 0;
    int64_t current_time = 0;
    bool service_expiry_present = false;
    int64_t service_expiry = 0;
    size_t xpad_data_group_length = 0;
    std::vector<uint8_t> xpad_data_group;
    uint8_t xpad_application_type = 0xFF;

    bool process_object(const std::vector<uint8_t> &data);

    bool process_management(const std::vector<uint8_t> &data);

    bool parse_content(Object &object, const std::vector<uint8_t> &content) const;

    static bool parse_data_section(const std::vector<uint8_t> &content, size_t &offset, std::vector<uint8_t> &payload);

    static void apply_data_section(Object &object, const std::vector<uint8_t> &payload, int64_t current_time);

    static bool decompress_deflate(const uint8_t *data, size_t length, std::vector<uint8_t> &result);

    static uint16_t calculate_crc(const uint8_t *data, size_t length);

    void expire();

    void emit_update() const;
};

#endif //JOURNALINE_DECODER_H
