//
// Created by tarek on 9/30/26.
//

#include "tuner_handler.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <format>
#include <optional>
#include <ranges>
#include <string_view>
#include "../encoder/loas_encoder.h"
#include "../helper/partial.h"

namespace {
struct EnsembleFrequency {
    std::string_view ensemble;
    uint32_t frequency;
};

constexpr std::array ENSEMBLE_FREQUENCIES{
    EnsembleFrequency{"5A", 174928},
    EnsembleFrequency{"5B", 176640},
    EnsembleFrequency{"5C", 178352},
    EnsembleFrequency{"5D", 180064},
    EnsembleFrequency{"6A", 181936},
    EnsembleFrequency{"6B", 183648},
    EnsembleFrequency{"6C", 185360},
    EnsembleFrequency{"6D", 187072},
    EnsembleFrequency{"7A", 188928},
    EnsembleFrequency{"7B", 190640},
    EnsembleFrequency{"7C", 192352},
    EnsembleFrequency{"7D", 194064},
    EnsembleFrequency{"8A", 195936},
    EnsembleFrequency{"8B", 197648},
    EnsembleFrequency{"8C", 199360},
    EnsembleFrequency{"8D", 201072},
    EnsembleFrequency{"9A", 202928},
    EnsembleFrequency{"9B", 204640},
    EnsembleFrequency{"9C", 206352},
    EnsembleFrequency{"9D", 208064},
    EnsembleFrequency{"10A", 209936},
    EnsembleFrequency{"10N", 210096},
    EnsembleFrequency{"10B", 211648},
    EnsembleFrequency{"10C", 213360},
    EnsembleFrequency{"10D", 215072},
    EnsembleFrequency{"11A", 216928},
    EnsembleFrequency{"11N", 217088},
    EnsembleFrequency{"11B", 218640},
    EnsembleFrequency{"11C", 220352},
    EnsembleFrequency{"11D", 222064},
    EnsembleFrequency{"12A", 223936},
    EnsembleFrequency{"12N", 224096},
    EnsembleFrequency{"12B", 225648},
    EnsembleFrequency{"12C", 227360},
    EnsembleFrequency{"12D", 229072},
    EnsembleFrequency{"13A", 230784},
    EnsembleFrequency{"13B", 232496},
    EnsembleFrequency{"13C", 234208},
    EnsembleFrequency{"13D", 235776},
    EnsembleFrequency{"13E", 237488},
    EnsembleFrequency{"13F", 239200}
};

std::optional<uint32_t> get_ensemble_frequency(const std::string_view ensemble) {
    for (const auto &entry: ENSEMBLE_FREQUENCIES) {
        if (entry.ensemble == ensemble) {
            return entry.frequency;
        }
    }

    return std::nullopt;
}

std::string get_ensemble_for_frequency(const uint32_t frequency) {
    for (const auto &entry: ENSEMBLE_FREQUENCIES) {
        if (entry.frequency == frequency) {
            return std::string(entry.ensemble);
        }
    }

    return {};
}
}

TunerHandler::TunerHandler(const int id, libusb_device_handle *device_handle) {
    logger.set_id(id);
    fic_logger.set_id(id);
    msc_logger.set_id(id);
    pad_logger.set_id(id);

    this->device_handle = device_handle;
    this->fic_decoder = new FICDecoder(partial(&TunerHandler::ensemble_data_event, this), partial(&TunerHandler::ensemble_status_data_event, this), partial(&TunerHandler::service_data_event, this), partial(&TunerHandler::service_removed_event, this), partial(&TunerHandler::service_component_data_event, this), partial(&TunerHandler::service_component_removed_event, this), partial(&TunerHandler::alarm_data_event, this), partial(&TunerHandler::date_time_data_event, this));
    this->dynamic_label_decoder = new DynamicLabelDecoder(partial(&TunerHandler::audio_text_event, this));
    this->slideshow_decoder = new SlideShowDecoder(partial(&TunerHandler::slideshow_event, this));
    this->pad_decoder = new PADDecoder(partial(&TunerHandler::dynamic_label_data_event, this), partial(&TunerHandler::mot_data_event, this));
    this->msc_decoder = new MSCDecoder(partial(&TunerHandler::access_unit_event, this), partial(&TunerHandler::pad_data_event, this));
    this->audio_pipe = new AudioPipe(id);
    this->slideshow_file = new SlideShowFile(id);
    this->helper = new TunerHelper(id, device_handle, partial(&TunerHandler::fic_data_event, this), partial(&TunerHandler::msc_data_event, this));

    logger.info("Initializing");

    if (!helper->power_up()) {
        initialization_failed = true;
        logger.error("Initialization failed");
        return;
    }

    initialized = true;
    running = true;
    read_thread = std::thread(&TunerHandler::handle, this);
    logger.info("Ready");
}

TunerHandler::~TunerHandler() {
    logger.debug("Stopping");
    running = false;

    if (read_thread.joinable()) {
        read_thread.join();
    }

    delete helper;
    delete fic_decoder;
    delete msc_decoder;
    delete pad_decoder;
    delete dynamic_label_decoder;
    delete slideshow_decoder;
    delete slideshow_file;
    delete audio_pipe;

    if (device_handle) {
        const int release_result = libusb_release_interface(device_handle, 0);

        if (release_result != LIBUSB_SUCCESS && release_result != LIBUSB_ERROR_NOT_FOUND) {
            logger.warning(std::format("Could not release USB interface 0: {}", libusb_error_name(release_result)));
        }

        libusb_close(device_handle);
    }

    logger.info("Stopped");
}

std::string TunerHandler::get_state() const {
    const std::lock_guard lock(state_mutex);

    if (initialization_failed) {
        return "Error";
    }

    if (!initialized) {
        return "Initializing";
    }

    if (current_service_id != 0) {
        return "Playing";
    }

    if (frequency != 0) {
        return "Tuned";
    }

    return "Idle";
}

uint32_t TunerHandler::get_frequency() const {
    const std::lock_guard lock(state_mutex);
    return frequency;
}

bool TunerHandler::get_alarm() const {
    const std::lock_guard lock(state_mutex);
    return alarm;
}

FICDecoder::DateTime TunerHandler::get_date_time() const {
    const std::lock_guard lock(state_mutex);
    return date_time;
}

FICDecoder::EnsembleStatus TunerHandler::get_ensemble_status() const {
    const std::lock_guard lock(state_mutex);
    return ensemble_status;
}

FICDecoder::Statistics TunerHandler::get_fic_statistics() const {
    const std::lock_guard lock(state_mutex);
    return fic_statistics;
}

FICDecoder::Service TunerHandler::get_service() const {
    const std::lock_guard lock(state_mutex);
    const auto service = services.find(current_service_id);

    if (service == services.end()) {
        return {};
    }

    return service->second;
}

std::string TunerHandler::get_audio_pipe() const {
    return audio_pipe->get_path();
}

std::string TunerHandler::get_slideshow() const {
    return slideshow_file->get_path();
}

std::string TunerHandler::get_dls() const {
    const std::lock_guard lock(state_mutex);
    return audio_text.dynamic_label;
}

TunerHandler::DynamicLabelPlus TunerHandler::get_dlp() const {
    const std::lock_guard lock(state_mutex);
    return {audio_text.title, audio_text.artist, audio_text.album, audio_text.item_running};
}

std::vector<std::string> TunerHandler::get_ensemble() const {
    const std::lock_guard lock(state_mutex);
    return {ensemble, ensemble_label, ensemble_short_label};
}

std::vector<FICDecoder::Service> TunerHandler::get_services() const {
    const std::lock_guard lock(state_mutex);
    std::vector<FICDecoder::Service> result;
    result.reserve(services.size());

    for (const auto &service: services | std::views::values) {
        result.push_back(service);
    }

    return result;
}

std::vector<FICDecoder::ServiceComponent> TunerHandler::get_service_components() const {
    const std::lock_guard lock(state_mutex);
    std::vector<FICDecoder::ServiceComponent> result;
    result.reserve(service_components.size());

    for (const auto &service_component: service_components | std::views::values) {
        result.push_back(service_component);
    }

    return result;
}

std::vector<FICDecoder::ServiceLink> TunerHandler::get_service_links() const {
    const std::lock_guard lock(state_mutex);
    return service_links;
}

std::vector<FICDecoder::FrequencyInformation> TunerHandler::get_frequency_information() const {
    const std::lock_guard lock(state_mutex);
    return frequency_information;
}

std::vector<FICDecoder::OtherEnsembleService> TunerHandler::get_other_ensemble_services() const {
    const std::lock_guard lock(state_mutex);
    return other_ensemble_services;
}

std::vector<FICDecoder::OEAnnouncementSupport> TunerHandler::get_oe_announcement_support() const {
    const std::lock_guard lock(state_mutex);
    return oe_announcement_support;
}

std::vector<FICDecoder::OEAnnouncementSwitch> TunerHandler::get_oe_announcement_switches() const {
    const std::lock_guard lock(state_mutex);
    return oe_announcement_switches;
}

std::vector<FICDecoder::FMAnnouncementSupport> TunerHandler::get_fm_announcement_support() const {
    const std::lock_guard lock(state_mutex);
    return fm_announcement_support;
}

std::vector<FICDecoder::FMAnnouncementSwitch> TunerHandler::get_fm_announcement_switches() const {
    const std::lock_guard lock(state_mutex);
    return fm_announcement_switches;
}

bool TunerHandler::set_frequency(const uint32_t frequency) {
    if (!initialized) {
        logger.warning("Cannot set frequency before initialization");
        return false;
    }

    logger.info(std::format("Setting frequency to {} kHz", frequency));

    const std::lock_guard lock(tuner_mutex);
    const bool result = helper->set_frequency(frequency);

    if (result) {
        fic_decoder->reset();
        msc_decoder->reset();
        pad_decoder->reset();
        dynamic_label_decoder->reset();
        slideshow_decoder->reset();
        slideshow_file->clear();
        audio_service_active = false;
        fic_data_received = false;
        msc_data_received = false;

        const std::lock_guard state_lock(state_mutex);
        this->frequency = frequency;
        alarm = false;
        date_time = {};
        ensemble_status = {};
        fic_statistics = {};
        ensemble = get_ensemble_for_frequency(frequency);
        ensemble_label.clear();
        ensemble_short_label.clear();
        services.clear();
        service_components.clear();
        service_links.clear();
        frequency_information.clear();
        other_ensemble_services.clear();
        oe_announcement_support.clear();
        oe_announcement_switches.clear();
        fm_announcement_support.clear();
        fm_announcement_switches.clear();
        current_service_id = 0;
        audio_text = {};
        logger.info(std::format("Frequency set to {} kHz", frequency));
    } else {
        logger.warning(std::format("Could not set frequency to {} kHz", frequency));
    }

    return result;
}

bool TunerHandler::set_ensemble(const std::string &ensemble) {
    std::string normalized_ensemble = ensemble;
    std::ranges::transform(normalized_ensemble, normalized_ensemble.begin(), [](const unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });

    const auto frequency = get_ensemble_frequency(normalized_ensemble);

    if (!frequency.has_value()) {
        logger.warning(std::format("Unknown ensemble {}", ensemble));
        return false;
    }

    return set_frequency(*frequency);
}

bool TunerHandler::set_service(const uint16_t service_id) {
    if (!initialized) {
        logger.warning("Cannot set service before initialization");
        return false;
    }

    const std::lock_guard tuner_lock(tuner_mutex);
    FICDecoder::Service selected_service;

    {
        const std::lock_guard state_lock(state_mutex);
        const auto service = services.find(service_id);

        if (service == services.end()) {
            logger.warning(std::format("Unknown service 0x{:04X}", service_id));
            return false;
        }

        selected_service = service->second;
    }

    msc_decoder->reset();
    pad_decoder->reset();
    dynamic_label_decoder->reset();
    slideshow_decoder->reset();
    slideshow_file->clear();
    audio_service_active = selected_service.dab_plus && selected_service.subchannel.bitrate > 0;

    if (audio_service_active) {
        msc_decoder->set_bitrate(selected_service.subchannel.bitrate);
    }

    helper->open_subchannel(selected_service.subchannel.id);

    {
        const std::lock_guard state_lock(state_mutex);
        current_service_id = service_id;
        audio_text = {};
    }

    logger.info(std::format("Service selected: {} [0x{:04X}], subchannel {}", selected_service.label, selected_service.id, selected_service.subchannel.id));
    return true;
}

bool TunerHandler::close_service() {
    if (!initialized) {
        logger.warning("Cannot close service before initialization");
        return false;
    }

    const std::lock_guard tuner_lock(tuner_mutex);
    msc_decoder->reset();
    pad_decoder->reset();
    dynamic_label_decoder->reset();
    slideshow_decoder->reset();
    slideshow_file->clear();
    audio_service_active = false;
    audio_pipe->close();

    const std::lock_guard state_lock(state_mutex);
    current_service_id = 0;
    audio_text = {};
    logger.info("Service closed");
    return true;
}

void TunerHandler::open_subchannel(const uint8_t subchannel) {
    if (!initialized) {
        return;
    }

    const std::lock_guard lock(tuner_mutex);
    msc_decoder->reset();
    pad_decoder->reset();
    dynamic_label_decoder->reset();
    slideshow_decoder->reset();
    slideshow_file->clear();
    audio_service_active = false;

    {
        const std::lock_guard state_lock(state_mutex);
        audio_text = {};
    }

    helper->open_subchannel(subchannel);
    logger.debug(std::format("Subchannel {} opened", subchannel));
}

void TunerHandler::handle() {
    while (running) {
        {
            const std::lock_guard lock(tuner_mutex);
            helper->read_data();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void TunerHandler::fic_data_event(const std::vector<uint8_t> &data) {
    if (!fic_data_received) {
        fic_data_received = true;
        fic_logger.debug(std::format("Receiving data ({} bytes)", data.size()));
    }

    fic_decoder->process(data);

    {
        const std::lock_guard lock(state_mutex);
        fic_statistics = fic_decoder->get_statistics();
        service_links = fic_decoder->get_service_links();
        frequency_information = fic_decoder->get_frequency_information();
        other_ensemble_services = fic_decoder->get_other_ensemble_services();
        oe_announcement_support = fic_decoder->get_oe_announcement_support();
        oe_announcement_switches = fic_decoder->get_oe_announcement_switches();
        fm_announcement_support = fic_decoder->get_fm_announcement_support();
        fm_announcement_switches = fic_decoder->get_fm_announcement_switches();
    }
}

void TunerHandler::msc_data_event(const std::vector<uint8_t> &data) {
    if (!msc_data_received) {
        msc_data_received = true;
        msc_logger.debug(std::format("Receiving data ({} bytes)", data.size()));
    }

    if (audio_service_active) {
        msc_decoder->process(data);
    }
}

void TunerHandler::access_unit_event(const MSCDecoder::AccessUnit &access_unit) const {
    audio_pipe->write(LOASEncoder::encode(access_unit.data, access_unit.sampling_rate, access_unit.channels, access_unit.sbr));
}

void TunerHandler::pad_data_event(const std::vector<uint8_t> &xpad_data, const std::array<uint8_t, 2> &fpad_data) const {
    pad_decoder->process(xpad_data, fpad_data);
}

void TunerHandler::dynamic_label_data_event(const bool start, const std::vector<uint8_t> &data) const {
    dynamic_label_decoder->process(start, data);
}

void TunerHandler::mot_data_event(const int type, const std::vector<uint8_t> &data) const {
    slideshow_decoder->process(type, data);
}

void TunerHandler::slideshow_event(const std::vector<uint8_t> &data, const std::string &extension) const {
    slideshow_file->write(data, extension);
    pad_logger.info(std::format("SlideShow: {} ({} bytes)", slideshow_file->get_path(), data.size()));
}

void TunerHandler::audio_text_event(const DynamicLabelDecoder::AudioText &audio_text) {
    {
        const std::lock_guard lock(state_mutex);
        this->audio_text = audio_text;
    }

    if (!audio_text.artist.empty() || !audio_text.title.empty()) {
        pad_logger.debug(std::format("DL+: {}{}{}", audio_text.artist, !audio_text.artist.empty() && !audio_text.title.empty() ? " - " : "", audio_text.title));
    } else if (!audio_text.dynamic_label.empty()) {
        pad_logger.debug(std::format("DLS: {}", audio_text.dynamic_label));
    }
}

void TunerHandler::ensemble_data_event(const FICDecoder::Ensemble &ensemble) {
    {
        const std::lock_guard lock(state_mutex);
        ensemble_label = ensemble.label;
        ensemble_short_label = ensemble.short_label;
    }

    fic_logger.info(std::format("Ensemble 0x{:04X}: {}", ensemble.id, ensemble.label));
}

void TunerHandler::ensemble_status_data_event(const FICDecoder::EnsembleStatus &ensemble_status) {
    FICDecoder::EnsembleStatus previous_status;

    {
        const std::lock_guard lock(state_mutex);
        previous_status = this->ensemble_status;
        this->ensemble_status = ensemble_status;
    }

    if (previous_status.alarm_supported != ensemble_status.alarm_supported) {
        fic_logger.info(std::format("Alarm support {}", ensemble_status.alarm_supported ? "enabled" : "disabled"));
    }

    if (previous_status.reconfiguration_counter != ensemble_status.reconfiguration_counter && previous_status.service_count != 0) {
        fic_logger.info(std::format("Multiplex reconfiguration counter changed: {} -> {}", previous_status.reconfiguration_counter, ensemble_status.reconfiguration_counter));
    }

    if (ensemble_status.change_flags != 0 && previous_status.change_flags != ensemble_status.change_flags) {
        auto change = "subchannel and service organization";

        if (ensemble_status.change_flags == 1) {
            change = "subchannel organization";
        } else if (ensemble_status.change_flags == 2) {
            change = "service organization";
        }

        fic_logger.info(std::format("Multiplex change announced: {}", change));
    }

    const bool was_complete = previous_status.service_count != 0 && previous_status.services_found >= previous_status.service_count;
    const bool is_complete = ensemble_status.service_count != 0 && ensemble_status.services_found >= ensemble_status.service_count;

    if (is_complete && !was_complete) {
        fic_logger.info(std::format("Service information complete: {}/{}", ensemble_status.services_found, ensemble_status.service_count));
    } else if (previous_status.services_found != ensemble_status.services_found || previous_status.service_count != ensemble_status.service_count) {
        fic_logger.debug(std::format("Service information: {}/{}", ensemble_status.services_found, ensemble_status.service_count));
    }
}

void TunerHandler::service_data_event(const FICDecoder::Service &service) {
    bool new_service = false;

    {
        const std::lock_guard lock(state_mutex);
        new_service = !services.contains(service.id);
        services[service.id] = service;
    }

    if (new_service) {
        fic_logger.info(std::format("Service 0x{:04X}: {} (subchannel {}, {}, {} kbit/s, {})", service.id, service.label, service.subchannel.id, service.dab_plus ? "DAB+" : "DAB", service.subchannel.bitrate, service.subchannel.protection));
    } else {
        fic_logger.debug(std::format("Service 0x{:04X} updated: language='{}', PTY='{}', announcements={}", service.id, service.language, service.pty, service.announcements.size()));
    }
}

void TunerHandler::service_removed_event(const uint16_t service_id) {
    bool selected = false;

    {
        const std::lock_guard lock(state_mutex);
        services.erase(service_id);

        if (current_service_id == service_id) {
            current_service_id = 0;
            audio_text = {};
            selected = true;
        }
    }

    if (selected) {
        audio_service_active = false;
        audio_pipe->close();
    }

    fic_logger.info(std::format("Service 0x{:04X} removed", service_id));
}

void TunerHandler::service_component_data_event(const FICDecoder::ServiceComponent &service_component) {
    const uint64_t service_component_key = static_cast<uint64_t>(service_component.data_service) << 36 | static_cast<uint64_t>(service_component.service_id) << 4 | service_component.id;
    bool new_service_component = false;

    {
        const std::lock_guard lock(state_mutex);
        new_service_component = !service_components.contains(service_component_key);
        service_components[service_component_key] = service_component;
    }

    if (new_service_component) {
        fic_logger.debug(std::format("Service component 0x{:X}/{} found", service_component.service_id, service_component.id));
    }
}

void TunerHandler::service_component_removed_event(const uint64_t service_component_key) {
    const std::lock_guard lock(state_mutex);
    service_components.erase(service_component_key);
}

void TunerHandler::alarm_data_event(const bool alarm) {
    {
        const std::lock_guard lock(state_mutex);
        this->alarm = alarm;
    }

    if (alarm) {
        fic_logger.warning("Alarm active");
    } else {
        fic_logger.info("Alarm ended");
    }
}

void TunerHandler::date_time_data_event(const FICDecoder::DateTime &date_time) {
    const std::lock_guard lock(state_mutex);
    this->date_time = date_time;
}
