#ifndef SPI_DECODER_H
#define SPI_DECODER_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

class SPIDecoder {
public:
    struct Multimedia {
        std::string url;
        std::string path;
        std::string mime_type;
        std::string type;
        uint16_t width = 0;
        uint16_t height = 0;

        bool operator==(const Multimedia &) const = default;
    };

    struct ServiceInformation {
        uint32_t service_id = 0;
        bool data_service = false;
        uint8_t component_id = 0;
        uint8_t ecc = 0;
        uint16_t ensemble_id = 0;
        std::string short_name;
        std::string medium_name;
        std::string long_name;
        std::string short_description;
        std::string long_description;
        std::string language;
        std::vector<std::string> genres;
        std::vector<Multimedia> multimedia;

        bool operator==(const ServiceInformation &other) const = default;
    };

    struct Programme {
        uint32_t service_id = 0;
        bool data_service = false;
        uint8_t component_id = 0;
        std::string id;
        uint32_t short_id = 0;
        std::string short_name;
        std::string medium_name;
        std::string long_name;
        std::string short_description;
        std::string long_description;
        std::string language;
        std::string start_time;
        uint32_t duration = 0;
        std::string actual_time;
        uint32_t actual_duration = 0;
        std::vector<std::string> genres;
        std::vector<Multimedia> multimedia;

        bool operator==(const Programme &other) const = default;
    };

    struct ProgrammeGroup {
        std::string id;
        uint32_t short_id = 0;
        std::string short_name;
        std::string medium_name;
        std::string long_name;
        std::string short_description;
        std::string long_description;
        std::vector<std::string> genres;
        std::vector<Multimedia> multimedia;

        bool operator==(const ProgrammeGroup &other) const = default;
    };

    struct ProcessResult {
        bool accepted = false;
        uint8_t root_tag = 0;
        uint32_t services = 0;
        uint32_t programmes = 0;
        uint32_t programme_groups = 0;
        std::string error;
    };

    using UpdateCallback = std::function<void(const std::vector<ServiceInformation> &, const std::vector<Programme> &, const std::vector<ProgrammeGroup> &)>;

    explicit SPIDecoder(const UpdateCallback &update_callback);

    void reset();

    ProcessResult process(uint16_t transport_id, const std::string &content_name, uint16_t content_subtype, const std::vector<uint8_t> &data, const std::vector<uint8_t> &scope_id = {}, const std::vector<uint8_t> &scope_end = {});

    void remove(uint16_t transport_id);

    void expire(int64_t timestamp);

private:
    struct Node {
        uint8_t tag = 0;
        std::map<uint8_t, std::vector<uint8_t>> attributes;
        std::vector<Node> children;
        std::vector<uint8_t> token_table;
        std::vector<uint8_t> default_language;
    };

    struct Bearer {
        bool valid = false;
        uint8_t ecc = 0;
        uint16_t ensemble_id = 0;
        bool data_service = false;
        uint32_t service_id = 0;
        uint8_t component_id = 0;
    };

    struct ObjectState {
        uint16_t content_subtype = 0;
        std::string content_name;
        int64_t expiry = 0;
        std::map<uint64_t, ServiceInformation> services;
        std::map<std::string, Programme> programmes;
        std::map<std::string, int64_t> programme_expiry;
        std::map<std::string, ProgrammeGroup> programme_groups;
    };

    UpdateCallback update_callback = nullptr;
    std::map<uint16_t, ObjectState> objects;
    std::map<uint64_t, ServiceInformation> services;
    std::map<std::string, Programme> programmes;
    std::map<std::string, ProgrammeGroup> programme_groups;

    bool parse_object(const std::vector<uint8_t> &data, Node &root, std::map<uint8_t, std::string> &tokens, std::string &default_language) const;

    bool parse_node(uint8_t tag, const uint8_t *data, size_t length, bool top_level, Node &node) const;

    static bool read_item(const uint8_t *data, size_t length, size_t &offset, uint8_t &tag, const uint8_t *&value, size_t &value_length);

    static bool is_element_tag(uint8_t tag);

    static std::map<uint8_t, std::string> parse_tokens(const std::vector<uint8_t> &data);

    static std::string decode_string(const std::vector<uint8_t> &data, const std::map<uint8_t, std::string> &tokens);

    static std::string get_string_attribute(const Node &node, uint8_t tag, const std::map<uint8_t, std::string> &tokens);

    static std::string get_text(const Node &node, const std::map<uint8_t, std::string> &tokens);

    static const Node *find_child(const Node &node, uint8_t tag);

    static std::vector<const Node *> find_children(const Node &node, uint8_t tag);

    static void collect_names(const Node &node, const std::map<uint8_t, std::string> &tokens, std::string &short_name, std::string &medium_name, std::string &long_name);

    static void collect_media(const Node &node, const std::map<uint8_t, std::string> &tokens, std::string &short_description, std::string &long_description, std::vector<std::string> &genres, std::vector<Multimedia> &multimedia);

    static Bearer decode_bearer(const std::vector<uint8_t> &data);

    static std::string decode_time(const std::vector<uint8_t> &data);

    static int64_t decode_time_timestamp(const std::vector<uint8_t> &data);

    static uint32_t decode_integer(const std::vector<uint8_t> &data);

    static std::string multimedia_type(uint8_t value);

    static uint64_t service_key(const Bearer &bearer);

    void process_service_information(const Node &root, const std::map<uint8_t, std::string> &tokens, const std::string &default_language, ObjectState &object);

    void process_programme_information(const Node &root, const std::map<uint8_t, std::string> &tokens, const std::string &default_language, const std::vector<uint8_t> &scope_id, ObjectState &object);

    void process_group_information(const Node &root, const std::map<uint8_t, std::string> &tokens, ObjectState &object);

    void rebuild();

    void publish() const;
};

#endif //SPI_DECODER_H
