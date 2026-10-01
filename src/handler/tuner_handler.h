//
// Created by tarek on 9/30/26.
//

#ifndef TUNER_HANDLER_H
#define TUNER_HANDLER_H

#include <array>
#include <atomic>
#include <libusb-1.0/libusb.h>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../decoder/dynamic_label_decoder.h"
#include "../decoder/fic_decoder.h"
#include "../decoder/msc_decoder.h"
#include "../decoder/pad_decoder.h"
#include "../decoder/slideshow_decoder.h"
#include "../helper/audio_pipe.h"
#include "../helper/slideshow_file.h"
#include "../helper/tuner_helper.h"
#include "../helper/logger.h"

class TunerHandler {
public:
    struct DynamicLabelPlus {
        std::string title;
        std::string artist;
        std::string album;
        bool item_running = false;
    };

    TunerHandler(int id, libusb_device_handle *device_handle);

    ~TunerHandler();

    std::string get_state() const;

    uint32_t get_frequency() const;

    bool get_alarm() const;

    FICDecoder::DateTime get_date_time() const;

    FICDecoder::EnsembleStatus get_ensemble_status() const;

    FICDecoder::Statistics get_fic_statistics() const;

    FICDecoder::Service get_service() const;

    std::string get_audio_pipe() const;

    std::string get_slideshow() const;

    std::string get_dls() const;

    DynamicLabelPlus get_dlp() const;

    std::vector<std::string> get_ensemble() const;

    std::vector<FICDecoder::Service> get_services() const;

    std::vector<FICDecoder::ServiceComponent> get_service_components() const;

    std::vector<FICDecoder::ServiceLink> get_service_links() const;

    std::vector<FICDecoder::FrequencyInformation> get_frequency_information() const;

    std::vector<FICDecoder::OtherEnsembleService> get_other_ensemble_services() const;

    std::vector<FICDecoder::OEAnnouncementSupport> get_oe_announcement_support() const;

    std::vector<FICDecoder::OEAnnouncementSwitch> get_oe_announcement_switches() const;

    std::vector<FICDecoder::FMAnnouncementSupport> get_fm_announcement_support() const;

    std::vector<FICDecoder::FMAnnouncementSwitch> get_fm_announcement_switches() const;

    bool set_frequency(uint32_t frequency);

    bool set_ensemble(const std::string &ensemble);

    bool set_service(uint16_t service_id);

    bool close_service();

    void open_subchannel(uint8_t subchannel);

private:
    libusb_device_handle *device_handle = nullptr;
    TunerHelper *helper = nullptr;
    FICDecoder *fic_decoder = nullptr;
    MSCDecoder *msc_decoder = nullptr;
    PADDecoder *pad_decoder = nullptr;
    DynamicLabelDecoder *dynamic_label_decoder = nullptr;
    SlideShowDecoder *slideshow_decoder = nullptr;
    AudioPipe *audio_pipe = nullptr;
    SlideShowFile *slideshow_file = nullptr;

    Logger logger{"TUNER"};
    Logger fic_logger{"FIC"};
    Logger msc_logger{"MSC"};
    Logger pad_logger{"PAD"};

    std::atomic_bool running = false;
    std::thread read_thread;
    std::mutex tuner_mutex;
    mutable std::mutex state_mutex;

    bool initialized = false;
    bool initialization_failed = false;
    uint32_t frequency = 0;
    bool alarm = false;
    FICDecoder::DateTime date_time;
    FICDecoder::EnsembleStatus ensemble_status;
    FICDecoder::Statistics fic_statistics;
    std::string ensemble;
    std::string ensemble_label;
    std::string ensemble_short_label;
    std::map<uint16_t, FICDecoder::Service> services;
    std::map<uint64_t, FICDecoder::ServiceComponent> service_components;
    std::vector<FICDecoder::ServiceLink> service_links;
    std::vector<FICDecoder::FrequencyInformation> frequency_information;
    std::vector<FICDecoder::OtherEnsembleService> other_ensemble_services;
    std::vector<FICDecoder::OEAnnouncementSupport> oe_announcement_support;
    std::vector<FICDecoder::OEAnnouncementSwitch> oe_announcement_switches;
    std::vector<FICDecoder::FMAnnouncementSupport> fm_announcement_support;
    std::vector<FICDecoder::FMAnnouncementSwitch> fm_announcement_switches;
    uint16_t current_service_id = 0;
    DynamicLabelDecoder::AudioText audio_text;

    bool fic_data_received = false;
    bool msc_data_received = false;
    bool audio_service_active = false;

    void handle();

    void fic_data_event(const std::vector<uint8_t> &data);

    void msc_data_event(const std::vector<uint8_t> &data);

    void access_unit_event(const MSCDecoder::AccessUnit &access_unit) const;

    void pad_data_event(const std::vector<uint8_t> &xpad_data, const std::array<uint8_t, 2> &fpad_data) const;

    void dynamic_label_data_event(bool start, const std::vector<uint8_t> &data) const;

    void mot_data_event(int type, const std::vector<uint8_t> &data) const;

    void slideshow_event(const std::vector<uint8_t> &data, const std::string &extension) const;

    void audio_text_event(const DynamicLabelDecoder::AudioText &audio_text);

    void ensemble_data_event(const FICDecoder::Ensemble &ensemble);

    void ensemble_status_data_event(const FICDecoder::EnsembleStatus &ensemble_status);

    void service_data_event(const FICDecoder::Service &service);

    void service_removed_event(uint16_t service_id);

    void service_component_data_event(const FICDecoder::ServiceComponent &service_component);

    void service_component_removed_event(uint64_t service_component_key);

    void alarm_data_event(bool alarm);

    void date_time_data_event(const FICDecoder::DateTime &date_time);
};

#endif //TUNER_HANDLER_H
