//
// Created by tarek on 10/1/26.
//

#ifndef SLIDESHOW_DECODER_H
#define SLIDESHOW_DECODER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

class SlideShowDecoder {
public:
    using SlideCallback = std::function<void(const std::vector<uint8_t> &, const std::string &)>;

    explicit SlideShowDecoder(const SlideCallback &slide_callback);

    void reset();

    void process(int type, const std::vector<uint8_t> &data);

private:
    struct Segments {
        std::map<uint16_t, std::vector<uint8_t>> values;
        int last = -1;

        void reset();

        bool complete() const;

        std::vector<uint8_t> get_data() const;
    };

    struct Object {
        Segments header_segments;
        Segments body_segments;
        uint32_t body_size = 0;
        uint16_t header_size = 0;
        uint8_t content_type = 0;
        uint16_t content_subtype = 0;
        int header_continuity = -1;
        int body_continuity = -1;
        bool header_complete = false;
        bool emitted = false;
    };

    SlideCallback slide_callback = nullptr;
    size_t data_group_length = 0;
    std::vector<uint8_t> data_group;
    std::map<uint16_t, Object> objects;

    void process_group(const std::vector<uint8_t> &group);

    void update_object(uint16_t transport_id);

    static uint16_t calculate_crc(const uint8_t *data, size_t length);

    static std::string detect_extension(const std::vector<uint8_t> &data);
};

#endif //SLIDESHOW_DECODER_H
