#ifndef SLIDESHOW_DECODER_H
#define SLIDESHOW_DECODER_H

#include "../config/features.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

class SlideShowDecoder {
public:
#if DAB_ENABLE_DIAGNOSTICS
    struct Statistics {
        uint64_t groups_received = 0;
        uint64_t groups_accepted = 0;
        uint64_t group_crc_errors = 0;
        uint64_t segments = 0;
        uint64_t objects = 0;
        uint64_t header_parses = 0;
        uint16_t transport_id = 0;
        uint32_t received_segments = 0;
        uint32_t total_segments = 0;
        bool total_segments_known = false;
        bool first_image_received = false;
        uint64_t first_image_ms = 0;
        bool cache_hit = false;

        bool operator==(const Statistics &other) const = default;
    };
#endif

    struct Slide {
        uint16_t transport_id = 0;
        std::string content_name;
        std::vector<uint8_t> data;
        std::string extension;
        bool trigger_time_present = false;
        bool trigger_now = false;
        int64_t trigger_time = 0;
        bool expire_time_present = false;
        int64_t expire_time = 0;
        uint8_t category_id = 0;
        uint8_t slide_id = 0;
        std::string category_title;
        std::string click_through_url;
        std::string alternative_location_url;
        uint8_t alert = 0;
        bool metadata_update = false;

        bool operator==(const Slide &other) const = default;
    };

    using SlideCallback = std::function<void(const Slide &)>;
    using SlideRemovedCallback = std::function<void(const std::string &)>;

    explicit SlideShowDecoder(const SlideCallback &slide_callback, const SlideCallback &display_callback = nullptr, const SlideRemovedCallback &removed_callback = nullptr);

    void reset();

    void select_service(uint32_t frequency, uint16_t ensemble_id, uint16_t service_id);

    void process(int type, const std::vector<uint8_t> &data);

    void update_time(int64_t timestamp);

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

    struct HeaderParameters {
        std::string content_name;
        bool trigger_time_present = false;
        bool trigger_now = false;
        int64_t trigger_time = 0;
        bool expire_time_present = false;
        int64_t expire_time = 0;
        bool category_present = false;
        uint8_t category_id = 0;
        uint8_t slide_id = 0;
        bool category_title_present = false;
        std::string category_title;
        std::string click_through_url;
        std::string alternative_location_url;
        uint8_t alert = 0;
    };

    struct Object {
        Segments header_segments;
        Segments body_segments;
        uint32_t body_size = 0;
        uint16_t header_size = 0;
        uint8_t content_type = 0;
        uint16_t content_subtype = 0;
        bool header_complete = false;
        bool emitted = false;
        HeaderParameters parameters;
    };

    struct StoredSlide {
        Slide slide;
        bool trigger_pending = false;
        bool trigger_consumed = false;
    };

    struct CachedSlide {
        Slide slide;
        std::chrono::steady_clock::time_point received;
    };

    SlideCallback slide_callback = nullptr;
    SlideCallback display_callback = nullptr;
    SlideRemovedCallback removed_callback = nullptr;
    size_t data_group_length = 0;
    std::vector<uint8_t> data_group;
    std::map<uint16_t, Object> objects;
    std::map<std::string, StoredSlide> slides;
    int64_t current_time = 0;
    bool current_time_valid = false;
    uint64_t service_key = 0;
    std::map<uint64_t, CachedSlide> cached_slides;
    int64_t reference_time = 0;
    bool reference_time_valid = false;
    std::chrono::steady_clock::time_point reference_time_received;
#if DAB_ENABLE_DIAGNOSTICS
    Statistics statistics;
    std::chrono::steady_clock::time_point reception_started = std::chrono::steady_clock::now();
#endif

    void process_group(const std::vector<uint8_t> &group);

    void update_object(uint16_t transport_id);

    bool parse_header(Object &object, const std::vector<uint8_t> &header);

    void store_slide(const Slide &slide);

    void apply_header_update(const HeaderParameters &parameters);

    void apply_category(const std::string &content_name, bool category_present, uint8_t category_id, uint8_t slide_id, const std::string &category_title, bool category_title_present);

    void arm_trigger(StoredSlide &slide, bool from_update);

    void display_slide(StoredSlide &slide, bool from_cache = false);

    void expire_slides();

    void cache_slide(const Slide &slide);

    void prune_cache();

    static bool read_parameter(const std::vector<uint8_t> &data, size_t &offset, uint8_t &parameter_id, std::vector<uint8_t> &parameter_data);

    static bool decode_time(const std::vector<uint8_t> &data, bool &now, int64_t &timestamp);

    static uint16_t calculate_crc(const uint8_t *data, size_t length);

    static std::string detect_extension(const std::vector<uint8_t> &data);
};

#endif //SLIDESHOW_DECODER_H
