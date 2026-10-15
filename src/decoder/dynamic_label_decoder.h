#ifndef DYNAMIC_LABEL_DECODER_H
#define DYNAMIC_LABEL_DECODER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

class DynamicLabelDecoder {
public:
    struct AudioText {
        std::string dynamic_label;
        std::string title;
        std::string artist;
        std::string album;
        bool item_running = false;

        bool operator==(const AudioText &other) const = default;
    };

    using AudioTextCallback = std::function<void(const AudioText &)>;

    explicit DynamicLabelDecoder(const AudioTextCallback &audio_text_callback);

    void reset();

    void process(bool start, const std::vector<uint8_t> &data);

private:
    struct Segment {
        uint8_t prefix[2]{};
        std::vector<uint8_t> characters;

        bool toggle() const;

        bool first() const;

        bool last() const;

        bool dl_plus_link() const;

        int number() const;
    };

    class SegmentReassembler {
    public:
        void reset();

        bool add(const Segment &segment);

        bool complete();

        bool get_toggle(bool &value) const;

        bool get_dl_plus_link(bool &value) const;

        int get_charset() const;

        const std::vector<uint8_t> &get_data() const;

    private:
        std::map<int, Segment> segments;
        std::vector<uint8_t> data;
    };

    class DataGroup {
    public:
        void reset();

        bool append(bool start, const std::vector<uint8_t> &new_data, std::vector<uint8_t> &complete_group);

    private:
        std::vector<uint8_t> data;
        size_t needed = 4;

        bool update_needed();
    };

    AudioTextCallback audio_text_callback = nullptr;
    DataGroup data_group;
    SegmentReassembler label_segments;
    SegmentReassembler dl_plus_segments;
    AudioText audio_text;
    AudioText emitted_audio_text;

    void process_group(const std::vector<uint8_t> &group);

    void publish_audio_text();

    void apply_dl_plus();

    void emit_audio_text();

    static uint16_t calculate_crc(const uint8_t *data, size_t length);

    static std::string decode_text(const std::vector<uint8_t> &data, int charset);

    static std::string decode_ebu_latin(const std::vector<uint8_t> &data);

    static std::string decode_ucs2(const std::vector<uint8_t> &data);

    static std::string utf8_substring(const std::string &value, size_t position, size_t length);

    static size_t utf8_byte_offset(const std::string &value, size_t characters);

    static void append_utf8(std::string &output, uint32_t codepoint);
};

#endif //DYNAMIC_LABEL_DECODER_H
