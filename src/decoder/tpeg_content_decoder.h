#ifndef TPEG_CONTENT_DECODER_H
#define TPEG_CONTENT_DECODER_H

#include "tpeg_decoder.h"
#include <string>
#include <vector>

class TPEGContentDecoder {
public:
    struct Application {
        uint8_t component_id = 0;
        uint8_t content_id = 0;
        uint16_t id = 0;
        uint8_t major_version = 0;
        uint8_t minor_version = 0;
        uint32_t message_count = 0;
        uint8_t encryption = 0;
        std::string description;

        bool operator==(const Application &other) const = default;
    };

    struct Service {
        TPEGDecoder::ServiceIdentifier id;
        std::string name;
        std::string description;
        uint8_t table_version = 0;
        uint8_t character_encoding = 0;
        std::vector<Application> applications;
    };

    struct Field {
        std::string component;
        std::string name;
        std::string value;
    };

    struct Message {
        TPEGDecoder::ServiceIdentifier service;
        uint8_t component_id = 0;
        uint16_t application_id = 0;
        uint32_t id = 0;
        uint8_t version = 0;
        uint32_t expiry = 0;
        uint32_t generation_time = 0;
        bool cancelled = false;
        uint8_t priority = 0;
        uint64_t sequence = 0;
        std::string summary;
        std::vector<Field> fields;
        std::vector<uint8_t> data;
    };

    struct Status {
        uint64_t messages_received = 0;
        uint64_t data_crc_errors = 0;
        uint64_t malformed_components = 0;
        uint64_t unsupported_components = 0;
    };

    void reset();

    void process_frame(const TPEGDecoder::Frame &frame);

    std::vector<Service> get_services() const;

    std::vector<Message> get_messages() const;

    Status get_status() const;

private:
    std::vector<Service> services;
    std::vector<Message> messages;
    Status status;

    bool process_sni(const TPEGDecoder::Component &component, Service &service);

    bool process_application(const TPEGDecoder::Component &component, const Application &application, const TPEGDecoder::Frame &frame);
};

#endif //TPEG_CONTENT_DECODER_H
