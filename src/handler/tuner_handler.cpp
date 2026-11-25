#include "tuner_handler.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <format>
#include <iterator>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>
#include <tuple>
#if DAB_ENABLE_SPI
#include "../helper/logo_helper.h"
#include "../helper/spi_group_helper.h"
#endif
#if DAB_ENABLE_AAC
#include "../encoder/loas_encoder.h"
#endif
#include "../helper/partial.h"

namespace {
constexpr auto SIGNAL_STATUS_INTERVAL = std::chrono::milliseconds(500);
#if DAB_ENABLE_AUDIO
constexpr auto AUDIO_SIGNAL_STATUS_INTERVAL = std::chrono::milliseconds(100);
#endif
#if DAB_ENABLE_DIAGNOSTICS && DAB_ENABLE_SLIDESHOW
constexpr auto SLIDESHOW_STATISTICS_INTERVAL = std::chrono::milliseconds(250);
#endif
#if DAB_ENABLE_SCAN
constexpr auto SCAN_SIGNAL_STATUS_INTERVAL = std::chrono::milliseconds(100);
constexpr auto SEEK_NO_SIGNAL_TIMEOUT = std::chrono::milliseconds(1200);
constexpr auto SEEK_NO_ENSEMBLE_TIMEOUT = std::chrono::seconds(4);
constexpr auto SEEK_METADATA_TIMEOUT = std::chrono::milliseconds(1200);
#if DAB_ENABLE_FAST_SCAN
constexpr auto FAST_SCAN_NO_SIGNAL_TIMEOUT = std::chrono::milliseconds(900);
constexpr auto FAST_SCAN_NO_ENSEMBLE_TIMEOUT = std::chrono::milliseconds(1400);
constexpr auto FAST_SCAN_LABEL_TIMEOUT = std::chrono::milliseconds(1200);
#endif
#if DAB_ENABLE_FULL_SCAN
constexpr auto FULL_SCAN_NO_SIGNAL_TIMEOUT = std::chrono::milliseconds(1200);
constexpr auto FULL_SCAN_NO_FIC_TIMEOUT = std::chrono::seconds(3);
constexpr auto FULL_SCAN_INVALID_FIC_TIMEOUT = std::chrono::seconds(5);
constexpr auto FULL_SCAN_NO_ENSEMBLE_TIMEOUT = std::chrono::seconds(12);
constexpr auto FULL_SCAN_ENSEMBLE_TIMEOUT = std::chrono::seconds(12);
constexpr auto FULL_SCAN_MINIMUM_TIME = std::chrono::seconds(2);
constexpr auto FULL_SCAN_FALLBACK_MINIMUM_TIME = std::chrono::seconds(4);
constexpr auto FULL_SCAN_STABLE_TIME = std::chrono::seconds(1);
#endif
constexpr auto SCAN_RESTORE_TIMEOUT = std::chrono::seconds(4);
#endif
constexpr auto FEC_UNLOCK_RECOVERY_DELAY = std::chrono::seconds(5);
constexpr auto RECOVERY_COOLDOWN = std::chrono::seconds(10);
#if DAB_ENABLE_EWS
constexpr auto EMERGENCY_WARNING_TIMEOUT = std::chrono::seconds(10);
#endif
#if DAB_ENABLE_DIAGNOSTICS && DAB_ENABLE_PACKET
constexpr auto DATA_PATH_STATISTICS_INTERVAL = std::chrono::milliseconds(250);
#endif
constexpr unsigned USB_ERROR_RECOVERY_THRESHOLD = 3;
#if DAB_ENABLE_TPEG
constexpr uint16_t TPEG_APPLICATION_TYPE = 0x004;
constexpr size_t TPEG_FRAME_HISTORY = 128;
#endif
#if DAB_ENABLE_SPI
constexpr uint16_t SPI_APPLICATION_TYPE = 0x007;
#endif
#if DAB_ENABLE_JOURNALINE
constexpr uint16_t JOURNALINE_APPLICATION_TYPE = 0x44A;
#endif

#if DAB_ENABLE_DATA_APPLICATIONS
std::string get_data_application_name(const uint16_t application_type) {
#if DAB_ENABLE_SPI
    if (application_type == SPI_APPLICATION_TYPE) {
        return "SPI";
    }
#endif
#if DAB_ENABLE_JOURNALINE
    if (application_type == JOURNALINE_APPLICATION_TYPE) {
        return "Journaline";
    }
#endif
#if DAB_ENABLE_TPEG
    if (application_type == TPEG_APPLICATION_TYPE) {
        return "TPEG";
    }
#endif
    return {};
}
#endif

std::optional<uint32_t> get_ensemble_frequency(const std::string_view ensemble) {
    const DABChannel *channel = get_dab_channel(ensemble);

    if (!channel) {
        return std::nullopt;
    }

    return channel->frequency;
}

std::string get_ensemble_for_frequency(const uint32_t frequency) {
    const DABChannel *channel = get_dab_channel(frequency);
    return channel ? std::string(channel->name) : std::string{};
}

uint64_t get_service_component_key(const uint32_t service_id, const bool data_service, const uint8_t component_id) {
    return static_cast<uint64_t>(data_service) << 36 | static_cast<uint64_t>(service_id) << 4 | component_id;
}

uint32_t difference(const uint32_t first, const uint32_t second) {
    return first > second ? first - second : second - first;
}

bool signal_status_change_is_significant(const TunerHandler::SignalStatus &current, const TunerHandler::SignalStatus &emitted) {
    if (current.ofdm_lock != emitted.ofdm_lock || current.fec_lock != emitted.fec_lock || current.fec_sync != emitted.fec_sync || current.signal_quality_valid != emitted.signal_quality_valid || current.msc_cer_valid != emitted.msc_cer_valid || current.fic_crc_error_rate_valid != emitted.fic_crc_error_rate_valid || current.antenna_level != emitted.antenna_level) {
        return true;
    }

    if (current.signal_quality_valid && difference(current.signal_quality, emitted.signal_quality) >= 2) {
        return true;
    }

    if (current.msc_cer_valid && difference(current.msc_cer, emitted.msc_cer) >= 10) {
        return true;
    }

    return current.fic_crc_error_rate_valid && difference(current.fic_crc_error_rate, emitted.fic_crc_error_rate) >= 50;
}

#if DAB_ENABLE_SPI
std::string get_image_extension(const std::vector<uint8_t> &data, const std::string &mime_type) {
    if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return "jpg";
    }

    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 && data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A) {
        return "png";
    }

    if (data.size() >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a') {
        return "gif";
    }

    if (data.size() >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
        return "webp";
    }

    if (mime_type == "image/jpeg") {
        return "jpg";
    }

    if (mime_type == "image/png") {
        return "png";
    }

    if (mime_type == "image/gif") {
        return "gif";
    }

    if (mime_type == "image/webp") {
        return "webp";
    }

    return {};
}

std::optional<int64_t> parse_spi_time(const std::string &value) {
    if (value.size() != 25 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' || value[16] != ':' || (value[19] != '+' && value[19] != '-') || value[22] != ':') {
        return std::nullopt;
    }

    const auto parse_number = [&value](const size_t offset, const size_t length) -> std::optional<int> {
        int result = 0;

        for (size_t index = 0; index < length; index++) {
            const char character = value[offset + index];

            if (character < '0' || character > '9') {
                return std::nullopt;
            }

            result = result * 10 + character - '0';
        }

        return result;
    };

    const auto year = parse_number(0, 4);
    const auto month = parse_number(5, 2);
    const auto day = parse_number(8, 2);
    const auto hour = parse_number(11, 2);
    const auto minute = parse_number(14, 2);
    const auto second = parse_number(17, 2);
    const auto offset_hour = parse_number(20, 2);
    const auto offset_minute = parse_number(23, 2);

    if (!year || !month || !day || !hour || !minute || !second || !offset_hour || !offset_minute || *hour > 23 || *minute > 59 || *second > 59 || *offset_hour > 23 || *offset_minute > 59) {
        return std::nullopt;
    }

    using namespace std::chrono;
    const year_month_day date{std::chrono::year{*year}, std::chrono::month{static_cast<unsigned>(*month)}, std::chrono::day{static_cast<unsigned>(*day)}};

    if (!date.ok()) {
        return std::nullopt;
    }

    const sys_seconds local = sys_days{date} + hours{*hour} + minutes{*minute} + seconds{*second};
    const int offset = (*offset_hour * 60 + *offset_minute) * (value[19] == '-' ? -1 : 1);
    return (local - minutes{offset}).time_since_epoch().count();
}

bool get_programme_interval(const SPIDecoder::Programme &programme, int64_t &start, int64_t &end) {
    const std::string &time = !programme.actual_time.empty() ? programme.actual_time : programme.start_time;
    const uint32_t duration = !programme.actual_time.empty() && programme.actual_duration != 0 ? programme.actual_duration : programme.duration;
    const auto timestamp = parse_spi_time(time);

    if (!timestamp) {
        return false;
    }

    start = *timestamp;
    end = duration == 0 ? start : start + duration;
    return true;
}

bool is_packet_spi_component(const FICDecoder::ServiceComponent &service_component) {
    if (service_component.subchannel == 0xFF || service_component.packet_address == 0xFFFF || !service_component.data_groups || service_component.conditional_access || service_component.fec_scheme > 1) {
        return false;
    }

    for (const auto &application: service_component.user_applications) {
        if (application.type == 0x007 && !application.xpad && !application.conditional_access) {
            return true;
        }
    }

    return false;
}
#endif

#if DAB_ENABLE_BACKGROUND_DATA
bool is_background_data_component(const FICDecoder::ServiceComponent &component, const uint16_t application_type) {
    bool supported = false;
#if DAB_ENABLE_SPI
    supported = supported || application_type == SPI_APPLICATION_TYPE;
#endif
#if DAB_ENABLE_TPEG
    supported = supported || application_type == TPEG_APPLICATION_TYPE;
#endif

    if (!supported) {
        return false;
    }

    if (component.subchannel == 0xFF || component.packet_address == 0xFFFF || !component.data_groups || component.conditional_access || component.fec_scheme > 1) {
        return false;
    }

#if DAB_ENABLE_TPEG
    if (application_type == TPEG_APPLICATION_TYPE && (component.type != 5 || component.fec_scheme != 1)) {
        return false;
    }
#endif

    return std::ranges::any_of(component.user_applications, [application_type](const FICDecoder::UserApplication &application) {
        return application.type == application_type && !application.xpad && !application.conditional_access;
    });
}
#endif

}

TunerHandler::TunerHandler(const int id, libusb_device_handle *device_handle)
    : ensemble_cache(id) {
    logger.set_id(id);
    fic_logger.set_id(id);
    msc_logger.set_id(id);
    pad_logger.set_id(id);
#if DAB_ENABLE_SPI
    spi_logger.set_id(id);
#endif
#if DAB_ENABLE_JOURNALINE
    journaline_logger.set_id(id);
#endif
#if DAB_ENABLE_TPEG
    tpeg_logger.set_id(id);
#endif

    this->device_handle = device_handle;
    this->fic_decoder = new FICDecoder(partial(&TunerHandler::ensemble_data_event, this), partial(&TunerHandler::ensemble_status_data_event, this), partial(&TunerHandler::service_data_event, this), partial(&TunerHandler::service_removed_event, this), partial(&TunerHandler::service_component_data_event, this), partial(&TunerHandler::service_component_removed_event, this), partial(&TunerHandler::date_time_data_event, this));
    this->dynamic_label_decoder = new DynamicLabelDecoder(partial(&TunerHandler::audio_text_event, this));
#if DAB_ENABLE_SLIDESHOW
    this->slideshow_decoder = new SlideShowDecoder(partial(&TunerHandler::slideshow_object_event, this), partial(&TunerHandler::slideshow_display_event, this), partial(&TunerHandler::slideshow_removed_event, this));
#endif
#if DAB_ENABLE_PACKET
    this->packet_decoder = new PacketDecoder(partial(&TunerHandler::packet_data_event, this));
#endif
#if DAB_ENABLE_SPI
    this->spi_mot_decoder = new MOTDecoder(partial(&TunerHandler::spi_object_event, this), partial(&TunerHandler::spi_object_removed_event, this));
    this->spi_decoder = new SPIDecoder(partial(&TunerHandler::spi_update_event, this));
    this->background_spi_mot_decoder = new MOTDecoder(partial(&TunerHandler::spi_object_event, this), partial(&TunerHandler::spi_object_removed_event, this));
#endif
#if DAB_ENABLE_BACKGROUND_DATA
    this->background_packet_decoder = new PacketDecoder(partial(&TunerHandler::background_packet_event, this));
#endif
#if DAB_ENABLE_JOURNALINE
    this->journaline_decoder = new JournalineDecoder(partial(&TunerHandler::journaline_update_event, this));
#endif
#if DAB_ENABLE_TPEG
    this->tpeg_decoder = new TPEGDecoder(partial(&TunerHandler::tpeg_update_event, this), partial(&TunerHandler::tpeg_frame_event, this));
#endif
    this->pad_decoder = new PADDecoder(partial(&TunerHandler::dynamic_label_data_event, this), partial(&TunerHandler::mot_data_event, this));
#if DAB_ENABLE_AAC
    this->msc_decoder = new MSCDecoder(partial(&TunerHandler::access_unit_event, this), partial(&TunerHandler::pad_data_event, this));
#endif
#if DAB_ENABLE_MP2
    this->mp2_decoder = new MP2Decoder(partial(&TunerHandler::mp2_frame_event, this), partial(&TunerHandler::pad_data_event, this));
#endif
#if DAB_ENABLE_AUDIO
    this->audio_pipe = new AudioPipe(id);
#endif
#if DAB_ENABLE_SLIDESHOW
    this->slideshow_file = new SlideShowFile(id);
#endif
#if DAB_ENABLE_SPI
    this->spi_file = new SPIFile(id);
#endif
#if DAB_ENABLE_SPI && DAB_ENABLE_LOGO_CACHE
    this->logo_cache = new LogoCache(id);
#endif
#if DAB_ENABLE_BACKGROUND_DATA
    this->helper = new TunerHelper(id, device_handle, partial(&TunerHandler::fic_data_event, this), partial(&TunerHandler::msc_data_event, this), partial(&TunerHandler::background_data_event, this));
#else
    this->helper = new TunerHelper(id, device_handle, partial(&TunerHandler::fic_data_event, this), partial(&TunerHandler::msc_data_event, this));
#endif

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
    stop();

    if (read_thread.joinable()) {
        read_thread.join();
    }

    delete helper;
    delete fic_decoder;
#if DAB_ENABLE_AAC
    delete msc_decoder;
#endif
#if DAB_ENABLE_MP2
    delete mp2_decoder;
#endif
    delete pad_decoder;
    delete dynamic_label_decoder;
#if DAB_ENABLE_SLIDESHOW
    delete slideshow_decoder;
#endif
#if DAB_ENABLE_PACKET
    delete packet_decoder;
#endif
#if DAB_ENABLE_SPI
    delete spi_mot_decoder;
    delete spi_decoder;
    delete background_spi_mot_decoder;
#endif
#if DAB_ENABLE_BACKGROUND_DATA
    delete background_packet_decoder;
#endif
#if DAB_ENABLE_JOURNALINE
    delete journaline_decoder;
#endif
#if DAB_ENABLE_TPEG
    delete tpeg_decoder;
#endif
#if DAB_ENABLE_SLIDESHOW
    delete slideshow_file;
#endif
#if DAB_ENABLE_SPI
    delete spi_file;
#endif
#if DAB_ENABLE_SPI && DAB_ENABLE_LOGO_CACHE
    delete logo_cache;
#endif
#if DAB_ENABLE_AUDIO
    delete audio_pipe;
#endif

    if (device_handle) {
        const int release_result = libusb_release_interface(device_handle, 0);

        if (release_result != LIBUSB_SUCCESS && release_result != LIBUSB_ERROR_NOT_FOUND && release_result != LIBUSB_ERROR_NO_DEVICE) {
            logger.warning(std::format("Could not release USB interface 0: {}", libusb_error_name(release_result)));
        }

        libusb_close(device_handle);
    }

    logger.info("Stopped");
}

void TunerHandler::stop() {
    running = false;

    if (helper) {
        helper->cancel_operations();
    }
}

void TunerHandler::set_property_change_callback(const PropertyChangeCallback &callback) {
    const std::lock_guard lock(property_change_mutex);
    property_change_callback = callback;
}

void TunerHandler::notify_property_changes(const std::initializer_list<std::string_view> properties) const {
    const std::lock_guard lock(property_change_mutex);

    if (!property_change_callback || properties.size() == 0) {
        return;
    }

    std::vector<std::string> property_names;
    property_names.reserve(properties.size());
#if DAB_ENABLE_SCAN
    const bool scanning = scan_active.load();
#endif

    for (const std::string_view property: properties) {
#if DAB_ENABLE_SCAN
        if (scanning && property != "State" && property != "Ensembles" && property != "ServicesReady" && property != "Services" && !property.starts_with("Scan")) {
            continue;
        }
#endif

        property_names.emplace_back(property);
#if DAB_ENABLE_AUDIO && DAB_ENABLE_DIAGNOSTICS
        if (property == "AudioCodec") {
            property_names.emplace_back("AudioStatistics");
        }
#endif
#if DAB_ENABLE_TPEG
        if (property == "DataApplication") {
            property_names.emplace_back("TPEGReceiving");
        }
#endif
#if DAB_ENABLE_SPI
        if (property == "SPIRevision") {
            property_names.emplace_back("ServiceLogos");
            property_names.emplace_back("SPIAssets");
            property_names.emplace_back("Services");
            property_names.emplace_back("Service");
        }
#endif
    }

    if (!property_names.empty()) {
        property_change_callback(property_names);
    }
}

std::string TunerHandler::get_state() const {
    const std::lock_guard lock(state_mutex);

    if (device_lost) {
        return "Disconnected";
    }

    if (initialization_failed) {
        return "Error";
    }

    if (!initialized) {
        return "Initializing";
    }

#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        return "Scanning";
    }
#endif

    if (current_service_id != 0) {
        return "Playing";
    }

    if (current_data_component_key != 0) {
        return "Receiving";
    }

    if (frequency != 0) {
        return "Tuned";
    }

    return "Idle";
}

bool TunerHandler::is_ready() const {
    const std::lock_guard lock(state_mutex);
    return initialized && !initialization_failed && !device_lost;
}

uint32_t TunerHandler::get_frequency() const {
    const std::lock_guard lock(state_mutex);
    return frequency;
}

FICDecoder::DateTime TunerHandler::get_date_time() const {
    const std::lock_guard lock(state_mutex);
    return date_time;
}

FICDecoder::EnsembleStatus TunerHandler::get_ensemble_status() const {
    const std::lock_guard lock(state_mutex);
    return ensemble_status;
}

#if DAB_ENABLE_DIAGNOSTICS
FICDecoder::Statistics TunerHandler::get_fic_statistics() const {
    const std::lock_guard lock(state_mutex);
    return fic_statistics;
}

TunerHandler::HardwareStatus TunerHandler::get_hardware_status() const {
    const auto usb_status = helper->get_usb_status();
    const std::lock_guard lock(state_mutex);
    auto status = hardware_status;
    status.usb = usb_status;
    return status;
}

uint64_t TunerHandler::get_fic_revision() const {
    return fic_decoder->get_fic_revision();
}

FICDecoder::FigureHistory TunerHandler::get_fic_figures(const uint64_t since_sequence, const uint16_t limit) const {
    return fic_decoder->get_figures(since_sequence, limit);
}
#endif


TunerHandler::SignalStatus TunerHandler::get_signal_status() const {
    const std::lock_guard lock(state_mutex);
    return signal_status;
}

#if DAB_ENABLE_FIDC
std::vector<FICDecoder::FIDCEntry> TunerHandler::get_fidc_entries() const {
    const std::lock_guard lock(state_mutex);
    return fidc_entries;
}
#endif


#if DAB_ENABLE_EWS
FICDecoder::EmergencyWarningStatus TunerHandler::get_emergency_warning_status() const {
    const std::lock_guard lock(state_mutex);
    return emergency_warning_status;
}
#endif


FICDecoder::Service TunerHandler::get_service() const {
    const std::lock_guard lock(state_mutex);
    const auto service = services.find(current_service_id);

    if (service == services.end()) {
        return {};
    }

    return service->second;
}

#if DAB_ENABLE_AUDIO
std::string TunerHandler::get_audio_pipe() const {
    return audio_pipe->get_path();
}
#endif


#if DAB_ENABLE_AUDIO
std::string TunerHandler::get_audio_codec() const {
    switch (audio_codec.load()) {
        case AudioCodec::AAC:
            return "aac";

        case AudioCodec::MP2:
            return "mp2";

        default:
            return {};
    }
}
#endif

#if DAB_ENABLE_AUDIO && DAB_ENABLE_DIAGNOSTICS
AudioStatistics TunerHandler::get_audio_statistics() const {
    const std::lock_guard lock(state_mutex);

    if (audio_codec.load() != audio_statistics_codec || frequency != audio_statistics_frequency || current_service_id != audio_statistics_service_id) {
        return {};
    }

    return audio_statistics;
}

void TunerHandler::update_audio_statistics() {
    AudioStatistics statistics;
    const auto codec = audio_codec.load();
#if DAB_ENABLE_AAC
    if (codec == AudioCodec::AAC) {
        statistics = msc_decoder->get_statistics();
    }
#endif
#if DAB_ENABLE_MP2
    if (codec == AudioCodec::MP2) {
        statistics = mp2_decoder->get_statistics();
    }
#endif

    const std::lock_guard lock(state_mutex);
    audio_statistics = statistics;
    audio_statistics_codec = codec;
    audio_statistics_frequency = frequency;
    audio_statistics_service_id = current_service_id;
}
#endif


#if DAB_ENABLE_SLIDESHOW
std::string TunerHandler::get_slideshow() const {
    const std::lock_guard lock(state_mutex);
    return current_slideshow_path;
}
#endif


#if DAB_ENABLE_SLIDESHOW
std::vector<TunerHandler::SlideShowItem> TunerHandler::get_slideshow_items() const {
    const std::lock_guard lock(state_mutex);
    std::vector<SlideShowItem> result;
    result.reserve(slideshow_items.size());

    for (const auto &item: slideshow_items | std::views::values) {
        result.push_back(item);
    }

    return result;
}
#endif


#if DAB_ENABLE_SLIDESHOW && DAB_ENABLE_DIAGNOSTICS
SlideShowDecoder::Statistics TunerHandler::get_slideshow_statistics() const {
    const std::lock_guard lock(state_mutex);
    return slideshow_statistics;
}
#endif


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

std::vector<EnsembleCache::Ensemble> TunerHandler::get_ensembles() const {
    return ensemble_cache.get_ensembles();
}

uint64_t TunerHandler::get_ensembles_revision() const {
    return ensemble_cache.get_revision();
}

bool TunerHandler::get_services_ready() const {
    const std::lock_guard lock(state_mutex);
    return services_ready;
}

std::vector<FICDecoder::Service> TunerHandler::get_services() const {
    const std::lock_guard lock(state_mutex);
    return published_services;
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

#if DAB_ENABLE_SERVICE_LINKING
std::vector<FICDecoder::ServiceLink> TunerHandler::get_service_links() const {
    const std::lock_guard lock(state_mutex);
    return service_links;
}
#endif


#if DAB_ENABLE_FREQUENCY_INFO
std::vector<FICDecoder::FrequencyInformation> TunerHandler::get_frequency_information() const {
    const std::lock_guard lock(state_mutex);
    return frequency_information;
}
#endif


#if DAB_ENABLE_SERVICE_LINKING
std::vector<FICDecoder::OtherEnsembleService> TunerHandler::get_other_ensemble_services() const {
    const std::lock_guard lock(state_mutex);
    return other_ensemble_services;
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
std::vector<FICDecoder::OEAnnouncementSupport> TunerHandler::get_oe_announcement_support() const {
    const std::lock_guard lock(state_mutex);
    return oe_announcement_support;
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
std::vector<FICDecoder::OEAnnouncementSwitch> TunerHandler::get_oe_announcement_switches() const {
    const std::lock_guard lock(state_mutex);
    return oe_announcement_switches;
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
std::vector<FICDecoder::FMAnnouncementSupport> TunerHandler::get_fm_announcement_support() const {
    const std::lock_guard lock(state_mutex);
    return fm_announcement_support;
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
std::vector<FICDecoder::FMAnnouncementSwitch> TunerHandler::get_fm_announcement_switches() const {
    const std::lock_guard lock(state_mutex);
    return fm_announcement_switches;
}
#endif


#if DAB_ENABLE_SPI
uint64_t TunerHandler::get_spi_revision() const {
    const std::lock_guard lock(state_mutex);
    return spi_revision;
}
#endif


#if DAB_ENABLE_SPI
std::vector<SPIDecoder::ServiceInformation> TunerHandler::get_service_information(const uint32_t service_id, const bool data_service, const uint8_t component_id) const {
    const std::lock_guard lock(state_mutex);
    std::vector<SPIDecoder::ServiceInformation> result;

    for (const auto &service: spi_service_information) {
        if (service.service_id != service_id || service.data_service != data_service || (component_id != 0xFF && service.component_id != component_id)) {
            continue;
        }

        auto resolved_service = service;

        for (auto &multimedia: resolved_service.multimedia) {
            for (const auto &asset: spi_assets | std::views::values) {
                if (!multimedia.url.empty() && multimedia.url == asset.content_name) {
                    multimedia.path = asset.path;
                    break;
                }
            }
        }

        result.push_back(std::move(resolved_service));
    }

    return result;
}
#endif


#if DAB_ENABLE_SPI
std::vector<SPIDecoder::Programme> TunerHandler::get_programme(const uint32_t service_id, const bool data_service, const uint8_t component_id, const int64_t timestamp) const {
    const std::lock_guard lock(state_mutex);
    std::vector<SPIDecoder::Programme> result;

    for (const auto &programme: spi_programmes) {
        if (programme.service_id != service_id || programme.data_service != data_service || (component_id != 0xFF && programme.component_id != component_id)) {
            continue;
        }

        int64_t start = 0;
        int64_t end = 0;

        if (!get_programme_interval(programme, start, end)) {
            continue;
        }

        if ((end > start && timestamp >= start && timestamp < end) || (end == start && timestamp == start)) {
            auto resolved_programme = programme;

            for (auto &multimedia: resolved_programme.multimedia) {
                for (const auto &asset: spi_assets | std::views::values) {
                    if (!multimedia.url.empty() && multimedia.url == asset.content_name) {
                        multimedia.path = asset.path;
                        break;
                    }
                }
            }

            result.push_back(std::move(resolved_programme));
        }
    }

    return result;
}
#endif


#if DAB_ENABLE_SPI
std::vector<SPIDecoder::Programme> TunerHandler::get_schedule(const uint32_t service_id, const bool data_service, const uint8_t component_id, const int64_t from, const int64_t to) const {
    if (to <= from) {
        return {};
    }

    const std::lock_guard lock(state_mutex);
    std::vector<SPIDecoder::Programme> result;

    for (const auto &programme: spi_programmes) {
        if (programme.service_id != service_id || programme.data_service != data_service || (component_id != 0xFF && programme.component_id != component_id)) {
            continue;
        }

        int64_t start = 0;
        int64_t end = 0;

        if (!get_programme_interval(programme, start, end)) {
            continue;
        }

        if ((end > start && end > from && start < to) || (end == start && start >= from && start < to)) {
            auto resolved_programme = programme;

            for (auto &multimedia: resolved_programme.multimedia) {
                for (const auto &asset: spi_assets | std::views::values) {
                    if (!multimedia.url.empty() && multimedia.url == asset.content_name) {
                        multimedia.path = asset.path;
                        break;
                    }
                }
            }

            result.push_back(std::move(resolved_programme));
        }
    }

    return result;
}
#endif


#if DAB_ENABLE_SPI
std::vector<SPIDecoder::Programme> TunerHandler::get_group_programmes(const std::string &group_id, const uint32_t short_id) const {
    const std::lock_guard lock(state_mutex);
    auto result = SPIGroupHelper::get_programmes(spi_programmes, spi_programme_groups, group_id, short_id);

    for (auto &programme: result) {
        for (auto &multimedia: programme.multimedia) {
            for (const auto &asset: spi_assets | std::views::values) {
                if (!multimedia.url.empty() && multimedia.url == asset.content_name) {
                    multimedia.path = asset.path;
                    break;
                }
            }
        }
    }

    return result;
}
#endif


#if DAB_ENABLE_SPI
std::vector<SPIDecoder::ProgrammeGroup> TunerHandler::get_programme_groups() const {
    const std::lock_guard lock(state_mutex);
    auto result = spi_programme_groups;

    for (auto &group: result) {
        for (auto &multimedia: group.multimedia) {
            for (const auto &asset: spi_assets | std::views::values) {
                if (!multimedia.url.empty() && multimedia.url == asset.content_name) {
                    multimedia.path = asset.path;
                    break;
                }
            }
        }
    }

    return result;
}
#endif


#if DAB_ENABLE_SPI
std::vector<TunerHandler::SPIAsset> TunerHandler::get_spi_assets() const {
    const std::lock_guard lock(state_mutex);
    std::vector<SPIAsset> result;
    result.reserve(spi_assets.size());

    for (const auto &asset: spi_assets | std::views::values) {
        result.push_back(asset);
    }

    return result;
}
#endif


#if DAB_ENABLE_SPI
std::vector<TunerHandler::ServiceLogo> TunerHandler::get_service_logos() const {
    const std::lock_guard lock(state_mutex);
    std::vector<ServiceLogo> result;

    for (const auto &service: spi_service_information) {
        if (service.data_service || service.multimedia.empty()) {
            continue;
        }

        ServiceLogo logo{service.service_id, service.ecc, service.ensemble_id, service.multimedia};

        for (auto &multimedia: logo.multimedia) {
            for (const auto &asset: spi_assets | std::views::values) {
                if (!multimedia.url.empty() && multimedia.url == asset.content_name) {
                    multimedia.path = asset.path;
                    break;
                }
            }
        }

        result.push_back(std::move(logo));
    }

#if DAB_ENABLE_LOGO_CACHE
    for (const auto &cached: cached_service_logos) {
        result.push_back({cached.service_id, cached.ecc, cached.ensemble_id, {{"", cached.path, cached.mime_type, "logo_unrestricted", cached.size, cached.size}}});
    }
#endif

    return result;
}
#endif


#if DAB_ENABLE_SPI
std::map<uint32_t, std::string> TunerHandler::get_logo_paths() const {
    const auto logos = get_service_logos();
    std::vector<FICDecoder::Service> audio_services;
    std::vector<SPIAsset> assets;
    uint8_t ecc = 0;
    uint16_t ensemble_id = 0;
#if DAB_ENABLE_LOGO_CACHE
    std::vector<LogoCache::Logo> cached;
#endif

    {
        const std::lock_guard lock(state_mutex);
        ecc = ensemble_status.ecc;
        ensemble_id = ensemble_status.ensemble_id;
#if DAB_ENABLE_LOGO_CACHE
        cached = cached_service_logos;
#endif

        for (const auto &service: services | std::views::values) {
            audio_services.push_back(service);
        }

        for (const auto &asset: spi_assets | std::views::values) {
            assets.push_back(asset);
        }
    }

    std::map<uint32_t, std::string> result;

    for (const auto &service: audio_services) {
        const uint8_t service_ecc = service.ecc == 0 ? ecc : service.ecc;
#if DAB_ENABLE_LOGO_CACHE
        const auto cached_logo = std::ranges::find_if(cached, [&](const LogoCache::Logo &logo) {
            return logo.service_id == service.id && logo.ecc == service_ecc;
        });
        std::error_code error;

        if (cached_logo != cached.end() && std::filesystem::is_regular_file(cached_logo->path, error)) {
            result[service.id] = cached_logo->path;
            continue;
        }
#endif
        uint16_t best_size = 0;

        const auto consider = [&](const std::string &path) {
            const auto [size, extension] = LogoHelper::image_info(LogoHelper::read_image(path));

            if (size > best_size) {
                best_size = size;
                result[service.id] = path;
            }
        };

        for (const auto &logo: logos) {
            if (logo.service_id != service.id || (logo.ecc != 0 && logo.ecc != service_ecc) || (logo.ensemble_id != 0 && logo.ensemble_id != ensemble_id)) {
                continue;
            }

            for (const auto &image: logo.multimedia) {
                if (image.type == "logo_colour_square" || (image.type == "logo_unrestricted" && image.width != 0 && image.width == image.height)) {
                    consider(image.path);
                }
            }
        }

        for (const auto &asset: assets) {
            if (LogoHelper::matches_service(asset.content_name, service.id, service.label, service.short_label)) {
                consider(asset.path);
            }
        }
    }

    return result;
}
#endif


#if DAB_ENABLE_SPI
std::vector<TunerHandler::SPIObject> TunerHandler::get_spi_objects() const {
    const std::lock_guard lock(state_mutex);
    std::vector<SPIObject> result;
    result.reserve(spi_objects.size());

    for (const auto &object: spi_objects | std::views::values) {
        result.push_back(object);
    }

    return result;
}
#endif


#if DAB_ENABLE_DATA_APPLICATIONS
std::string TunerHandler::get_data_application() const {
    const std::lock_guard lock(state_mutex);
#if DAB_ENABLE_BACKGROUND_DATA
    if (background_data_active) {
        return get_data_application_name(background_data_application);
    }
#endif
    return get_data_application_name(current_data_application);
}
#endif


#if DAB_ENABLE_DIAGNOSTICS && DAB_ENABLE_PACKET
TunerHandler::DataPathStatistics TunerHandler::get_data_path_statistics() const {
#if DAB_ENABLE_BACKGROUND_DATA
    const bool background = background_data_active.load();
    const PacketDecoder::Statistics packet_statistics = background ? background_packet_decoder->get_statistics() : packet_decoder->get_statistics();
#else
    const PacketDecoder::Statistics packet_statistics = packet_decoder->get_statistics();
#endif
    DataPathStatistics result;
    result.msc_bytes = packet_statistics.input_bytes;
    result.packets_valid = packet_statistics.packets_valid;
    result.packet_crc_errors = packet_statistics.packet_crc_errors;
    result.packets_addressed = packet_statistics.packets_addressed;
    result.data_groups = packet_statistics.data_groups;
    result.packet_continuity_errors = packet_statistics.continuity_errors;
    result.fec_frames = packet_statistics.fec_frames;
    result.fec_errors = packet_statistics.fec_errors;

#if DAB_ENABLE_SPI
    const MOTDecoder::Statistics mot_statistics = background ? background_spi_mot_decoder->get_statistics() : spi_mot_decoder->get_statistics();
    result.mot_groups_received = mot_statistics.groups_received;
    result.mot_groups_accepted = mot_statistics.groups_accepted;
    result.mot_crc_errors = mot_statistics.group_crc_errors;
    result.mot_segments = mot_statistics.segments;
    result.mot_directories = mot_statistics.directories;
    result.mot_objects = mot_statistics.objects;

    {
        const std::lock_guard lock(state_mutex);
        result.spi_objects = spi_data_objects;
        result.spi_updates = spi_data_updates;
    }
#endif

    return result;
}
#endif


#if DAB_ENABLE_JOURNALINE
uint64_t TunerHandler::get_journaline_revision() const {
    const std::lock_guard lock(state_mutex);
    return journaline_revision;
}
#endif


#if DAB_ENABLE_JOURNALINE
JournalineDecoder::Status TunerHandler::get_journaline_status() const {
    const std::lock_guard lock(state_mutex);
    return journaline_status;
}
#endif


#if DAB_ENABLE_JOURNALINE
std::vector<JournalineDecoder::IndexEntry> TunerHandler::get_journaline_index() const {
    const std::lock_guard lock(state_mutex);
    std::vector<JournalineDecoder::IndexEntry> result;
    result.reserve(journaline_objects.size());

    for (const auto &object: journaline_objects) {
        result.push_back({object.id, object.type, object.static_content, object.revision, object.title});
    }

    return result;
}
#endif


#if DAB_ENABLE_JOURNALINE
std::vector<JournalineDecoder::Object> TunerHandler::get_journaline_object(const uint16_t object_id) const {
    const std::lock_guard lock(state_mutex);

    for (const auto &object: journaline_objects) {
        if (object.id == object_id) {
            return {object};
        }
    }

    return {};
}
#endif


#if DAB_ENABLE_TPEG
bool TunerHandler::get_tpeg_receiving() const {
    const std::lock_guard lock(state_mutex);
    return (background_data_active && background_data_application == TPEG_APPLICATION_TYPE) || current_data_application == TPEG_APPLICATION_TYPE;
}

bool TunerHandler::stop_tpeg() {
    if (!initialized) {
        return false;
    }
#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        return false;
    }
#endif
    const std::lock_guard tuner_lock(tuner_mutex);
    bool background = false;
    {
        const std::lock_guard state_lock(state_mutex);
        background = background_data_active && background_data_application == TPEG_APPLICATION_TYPE;
        if (!background && (!data_service_active || current_data_application != TPEG_APPLICATION_TYPE)) {
            return false;
        }
        tpeg_suppressed = true;
    }

    const auto operation = helper->begin_operation(TunerHelper::TUNING_TIMEOUT);
    if (background) {
        stop_background_data();
    } else {
        packet_decoder->reset();
        data_service_active = false;
        const std::lock_guard state_lock(state_mutex);
        current_data_component_key = 0;
        current_data_application = 0;
    }
    notify_property_changes({"DataApplication", "DataPathStatistics", "TPEGReceiving"});
#if DAB_ENABLE_SPI
    if (!audio_service_active) {
        queue_spi_service();
    }
#endif
    return operation.succeeded();
}
#endif


#if DAB_ENABLE_TPEG
uint64_t TunerHandler::get_tpeg_revision() const {
    const std::lock_guard lock(state_mutex);
    return tpeg_revision;
}
#endif


#if DAB_ENABLE_TPEG
TPEGDecoder::Status TunerHandler::get_tpeg_status() const {
    const std::lock_guard lock(state_mutex);
    return tpeg_status;
}
#endif


#if DAB_ENABLE_TPEG
TPEGContentDecoder::Status TunerHandler::get_tpeg_content_status() const {
    const std::lock_guard lock(state_mutex);
    return tpeg_content_decoder.get_status();
}

std::vector<TPEGContentDecoder::Service> TunerHandler::get_tpeg_service_information() const {
    const std::lock_guard lock(state_mutex);
    return tpeg_content_decoder.get_services();
}

std::vector<TPEGContentDecoder::Message> TunerHandler::get_tpeg_messages() const {
    const std::lock_guard lock(state_mutex);
    return tpeg_content_decoder.get_messages();
}

std::vector<TPEGDecoder::ServiceIdentifier> TunerHandler::get_tpeg_services() const {
    const std::lock_guard lock(state_mutex);
    return tpeg_services;
}
#endif


#if DAB_ENABLE_TPEG
std::vector<TPEGDecoder::Frame> TunerHandler::get_tpeg_frames(const uint64_t since_sequence, const uint16_t limit) const {
    const std::lock_guard lock(state_mutex);
    std::vector<TPEGDecoder::Frame> result;
    const size_t maximum = std::min<size_t>(limit == 0 ? 32 : limit, 128);

    for (const auto &frame: tpeg_frames) {
        if (frame.sequence <= since_sequence) {
            continue;
        }

        result.push_back(frame);

        if (result.size() >= maximum) {
            break;
        }
    }

    return result;
}
#endif


std::vector<DABChannel> TunerHandler::get_band_iii_channels() const {
    return {DAB_CHANNELS.begin(), DAB_CHANNELS.end()};
}

#if DAB_ENABLE_SCAN
std::string TunerHandler::get_scan_state() const {
    const std::lock_guard lock(state_mutex);
    return scan_state;
}
#endif


#if DAB_ENABLE_SCAN
std::string TunerHandler::get_scan_mode() const {
    const std::lock_guard lock(state_mutex);

    switch (scan_mode) {
#if DAB_ENABLE_FAST_SCAN
        case ScanMode::Fast:
            return "fast";
#endif

#if DAB_ENABLE_FULL_SCAN
        case ScanMode::Full:
            return "full";
#endif

        case ScanMode::SeekUp:
            return "seek_up";

        case ScanMode::SeekDown:
            return "seek_down";

        default:
            return {};
    }
}
#endif


#if DAB_ENABLE_SCAN
std::vector<std::string> TunerHandler::get_scan_modes() const {
    std::vector<std::string> modes;
#if DAB_ENABLE_FAST_SCAN
    modes.emplace_back("fast");
#endif
#if DAB_ENABLE_FULL_SCAN
    modes.emplace_back("full");
#endif
#if DAB_ENABLE_SEEK
    modes.emplace_back("seek_up");
    modes.emplace_back("seek_down");
#endif
    return modes;
}
#endif


#if DAB_ENABLE_SCAN
TunerHandler::ScanProgress TunerHandler::get_scan_progress() const {
    const std::lock_guard lock(state_mutex);
    return scan_progress;
}
#endif


#if DAB_ENABLE_SCAN
std::vector<TunerHandler::ScanResult> TunerHandler::get_scan_results() const {
    const std::lock_guard lock(state_mutex);
    return scan_results;
}
#endif


#if DAB_ENABLE_SCAN
bool TunerHandler::start_scan(const std::string &mode) {
    if (!initialized) {
        logger.warning("Cannot start scan before initialization");
        return false;
    }

    std::string normalized_mode = mode;
    std::ranges::transform(normalized_mode, normalized_mode.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    ScanMode requested_mode = ScanMode::None;

#if DAB_ENABLE_FAST_SCAN
    if (normalized_mode == "fast") {
        requested_mode = ScanMode::Fast;
    }
#endif
#if DAB_ENABLE_FULL_SCAN
    if (normalized_mode == "full") {
        requested_mode = ScanMode::Full;
    }
#endif

    if (requested_mode == ScanMode::None) {
        logger.warning(std::format("Scan mode {} was not compiled in", mode));
        return false;
    }

    return start_scan_mode(requested_mode);
}
#endif


#if DAB_ENABLE_SCAN
bool TunerHandler::seek_up() {
    return start_scan_mode(ScanMode::SeekUp);
}
#endif


#if DAB_ENABLE_SCAN
bool TunerHandler::seek_down() {
    return start_scan_mode(ScanMode::SeekDown);
}
#endif


#if DAB_ENABLE_SCAN
bool TunerHandler::start_scan_mode(const ScanMode requested_mode) {
    const bool seeking = requested_mode == ScanMode::SeekUp || requested_mode == ScanMode::SeekDown;

    {
        const std::lock_guard tuner_lock(tuner_mutex);

        {
            const std::lock_guard state_lock(state_mutex);

            if (!initialized || scan_active.load()) {
                logger.warning("Tuner unavailable or scan already active");
                return false;
            }

            scan_stop_requested = false;
            scan_mode = requested_mode;
            scan_phase = ScanPhase::Tune;
            scan_state = "Scanning";
            scan_progress = {0, static_cast<uint16_t>(DAB_CHANNELS.size()), {}, 0};

            if (!seeking) {
                scan_results.clear();
            }

            scan_channel_index = 0;
            scan_channels_checked = 0;

            if (seeking) {
                const auto current = get_dab_channel_index(frequency);
                const size_t step = requested_mode == ScanMode::SeekUp ? 1 : DAB_CHANNELS.size() - 1;
                scan_channel_index = current ? (*current + step) % DAB_CHANNELS.size() : requested_mode == ScanMode::SeekUp ? 0 : DAB_CHANNELS.size() - 1;
            }

            scan_saved_frequency = frequency;
            scan_saved_service_id = current_service_id;
            scan_restore_service_id = 0;
            scan_ensemble_seen_valid = false;
            scan_active = true;
#if DAB_ENABLE_FULL_SCAN
            scan_tune_retried = false;
#endif
        }

#if DAB_ENABLE_AUDIO
        audio_mute.reset();
        audio_pipe->discard_buffered();
#endif
#if DAB_ENABLE_AAC
        msc_decoder->reset();
#endif
#if DAB_ENABLE_MP2
        mp2_decoder->reset();
#endif
        pad_decoder->reset();
        dynamic_label_decoder->reset();
#if DAB_ENABLE_SLIDESHOW
        reset_slideshow();
#endif
#if DAB_ENABLE_DATA_APPLICATIONS
        reset_data_service();
#endif
#if DAB_ENABLE_AUDIO
        audio_pipe->close();
#endif
        audio_service_active = false;
        audio_codec = AudioCodec::None;
        msc_data_received = false;

        {
            const std::lock_guard state_lock(state_mutex);
            current_service_id = 0;
            audio_text = {};
        }
    }

    logger.info(std::format("{} started", get_scan_mode()));
    notify_property_changes({"State", "AudioCodec", "DataApplication", "DataPathStatistics", "SlideShow", "SlideShowItems", "SlideShowStatistics", "DLS", "DLP", "SPIRevision", "JournalineRevision", "JournalineStatus", "TPEGRevision", "TPEGStatus", "ScanState", "ScanMode", "ScanProgress", "ScanResults"});
    return true;
}
#endif


#if DAB_ENABLE_SCAN
bool TunerHandler::stop_seek() {
    return stop_scan_mode(true);
}
#endif


#if DAB_ENABLE_SCAN
bool TunerHandler::stop_scan() {
    return stop_scan_mode(false);
}
#endif


#if DAB_ENABLE_SCAN
bool TunerHandler::stop_scan_mode(const bool seek_only) {
    {
        const std::lock_guard lock(state_mutex);
        const bool seeking = scan_mode == ScanMode::SeekUp || scan_mode == ScanMode::SeekDown;

        if (!scan_active.load() || (seek_only && !seeking)) {
            return false;
        }

        scan_stop_requested = true;
        scan_state = "Stopping";
    }

    notify_property_changes({"ScanState"});
    return true;
}
#endif


bool TunerHandler::set_frequency(const uint32_t frequency) {
#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        logger.warning("Cannot set frequency while scan is active");
        return false;
    }
#endif

    return tune_frequency(frequency, true);
}

bool TunerHandler::tune_frequency(const uint32_t frequency, const bool notify_changes) {
    if (!initialized) {
        logger.warning("Cannot set frequency before initialization");
        return false;
    }

    logger.info(std::format("Setting frequency to {} kHz", frequency));

    const std::lock_guard lock(tuner_mutex);
#if DAB_ENABLE_AUDIO
    audio_mute.reset();
    audio_pipe->discard_buffered();
#endif
    const bool result = helper->set_frequency(frequency);

    if (result) {
        fic_decoder->reset();
#if DAB_ENABLE_AAC
        msc_decoder->reset();
#endif
#if DAB_ENABLE_MP2
        mp2_decoder->reset();
#endif
        pad_decoder->reset();
        dynamic_label_decoder->reset();
#if DAB_ENABLE_SLIDESHOW
        reset_slideshow();
#endif
#if DAB_ENABLE_DATA_APPLICATIONS
        reset_data_service();
#endif
        audio_service_active = false;
        audio_codec = AudioCodec::None;
        fic_data_received = false;
        msc_data_received = false;

        const std::lock_guard state_lock(state_mutex);
        this->frequency = frequency;
        date_time = {};
        ensemble_status = {};
        fic_statistics = {};
        signal_status = {};
        emitted_signal_status = {};
        emitted_signal_status_valid = false;
        signal_quality_filter.reset();
        signal_fic_received = 0;
        signal_fic_crc_errors = 0;
#if DAB_ENABLE_FIDC
        fidc_entries.clear();
#endif
#if DAB_ENABLE_EWS
        emergency_warning_status = {};
        emergency_warning_sequence = 0;
        emergency_warning_seen_valid = false;
#endif
        ensemble = get_ensemble_for_frequency(frequency);
        ensemble_label.clear();
        ensemble_short_label.clear();
        services.clear();
        published_services.clear();
        services_ready = false;
        ensemble_acquired = {};
        last_service_change = std::chrono::steady_clock::now();
        service_components.clear();
#if DAB_ENABLE_FULL_SCAN
        scan_service_information_complete = false;
#endif
#if DAB_ENABLE_SCAN
        scan_restore_service_id = 0;
#endif
#if DAB_ENABLE_SERVICE_LINKING
        service_links.clear();
        other_ensemble_services.clear();
#endif
#if DAB_ENABLE_FREQUENCY_INFO
        frequency_information.clear();
#endif
#if DAB_ENABLE_ANNOUNCEMENTS
        oe_announcement_support.clear();
        oe_announcement_switches.clear();
        fm_announcement_support.clear();
        fm_announcement_switches.clear();
#endif
        current_service_id = 0;
        audio_text = {};
        logger.info(std::format("Frequency set to {} kHz", frequency));
    } else {
        logger.warning(std::format("Could not set frequency to {} kHz", frequency));
    }

    if (result && notify_changes) {
        notify_property_changes({"State", "Frequency", "SignalStatus", "DateTime", "Ensemble", "EnsembleStatus", "Services", "ServicesReady", "ServiceComponents", "SPIRevision", "DataApplication", "DataPathStatistics", "JournalineRevision", "JournalineStatus", "TPEGRevision", "TPEGStatus", "ServiceLinks", "FrequencyInformation", "OtherEnsembleServices", "OEAnnouncementSupport", "OEAnnouncementSwitches", "FMAnnouncementSupport", "FMAnnouncementSwitches", "FIDC", "EmergencyWarning", "Service", "AudioCodec", "SlideShow", "SlideShowItems", "SlideShowStatistics", "DLS", "DLP"});
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

#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        logger.warning("Cannot set service while scan is active");
        return false;
    }
#endif

    const std::lock_guard tuner_lock(tuner_mutex);
    FICDecoder::Service selected_service;
    const auto operation = helper->begin_operation(TunerHelper::TUNING_TIMEOUT);
#if DAB_ENABLE_SPI
    uint8_t spi_xpad_type = 0xFF;
#endif
#if DAB_ENABLE_JOURNALINE
    uint8_t journaline_xpad_type = 0xFF;
    std::vector<uint8_t> journaline_parameters;
#endif

    {
        const std::lock_guard state_lock(state_mutex);
        const auto service = services.find(service_id);

        if (service == services.end()) {
            logger.warning(std::format("Unknown service 0x{:04X}", service_id));
            return false;
        }

        selected_service = service->second;

#if DAB_ENABLE_SPI || DAB_ENABLE_JOURNALINE
        for (const auto &service_component: service_components | std::views::values) {
            if (service_component.data_service || service_component.service_id != service_id || service_component.subchannel != selected_service.subchannel.id) {
                continue;
            }

            for (const auto &application: service_component.user_applications) {
                if (!application.xpad || application.xpad_type == 0xFF) {
                    continue;
                }

#if DAB_ENABLE_SPI
                if (application.type == SPI_APPLICATION_TYPE) {
                    spi_xpad_type = application.xpad_type;
                    continue;
                }
#endif
#if DAB_ENABLE_JOURNALINE
                if (application.type == JOURNALINE_APPLICATION_TYPE) {
                    journaline_xpad_type = application.xpad_type;
                    journaline_parameters = application.parameters;
                }
#endif
            }
        }
#endif
    }

    const bool has_audio = selected_service.subchannel.bitrate > 0;

#if !DAB_ENABLE_AAC
    if (has_audio && selected_service.dab_plus) {
        logger.warning("Cannot select DAB+ service: AAC support was compiled out");
        return false;
    }
#endif
#if !DAB_ENABLE_MP2
    if (has_audio && !selected_service.dab_plus) {
        logger.warning("Cannot select DAB service: MP2 support was compiled out");
        return false;
    }
#endif

#if DAB_ENABLE_AUDIO
    audio_mute.reset();
    audio_pipe->discard_buffered();
#endif
#if DAB_ENABLE_AAC
    msc_decoder->reset();
#endif
#if DAB_ENABLE_MP2
    mp2_decoder->reset();
#endif
    pad_decoder->reset();
    dynamic_label_decoder->reset();
#if DAB_ENABLE_SLIDESHOW
    reset_slideshow();
#endif
#if DAB_ENABLE_DATA_APPLICATIONS
    reset_data_service(true);
#endif
    audio_service_active = has_audio;
    audio_codec = AudioCodec::None;

    if (audio_service_active) {
        if (selected_service.dab_plus) {
#if DAB_ENABLE_AAC
            msc_decoder->set_bitrate(selected_service.subchannel.bitrate);
            audio_codec = AudioCodec::AAC;
#endif
        } else {
#if DAB_ENABLE_MP2
            mp2_decoder->set_bitrate(selected_service.subchannel.bitrate);
            audio_codec = AudioCodec::MP2;
#endif
        }
    }

#if DAB_ENABLE_SPI
    if (spi_xpad_type != 0xFF) {
        spi_mot_decoder->configure_xpad(spi_xpad_type);
        spi_logger.info(std::format("SPI X-PAD AppTy {} enabled", spi_xpad_type));
    }
#endif

#if DAB_ENABLE_JOURNALINE
    if (journaline_xpad_type != 0xFF) {
        if (journaline_decoder->configure_xpad(journaline_xpad_type, journaline_parameters)) {
            journaline_logger.info(std::format("Journaline X-PAD AppTy {} enabled", journaline_xpad_type));
        } else {
            journaline_logger.warning("Could not configure Journaline X-PAD");
        }
    }
#endif

    helper->open_subchannel(selected_service.subchannel.id);

    if (!operation.succeeded()) {
        audio_service_active = false;
        audio_codec = AudioCodec::None;
#if DAB_ENABLE_AUDIO
        audio_pipe->close();
#endif
        {
            const std::lock_guard state_lock(state_mutex);
            current_service_id = 0;
            audio_text = {};
        }

        notify_property_changes({"State", "Service", "AudioCodec", "HardwareStatus"});
        return false;
    }

    {
        const std::lock_guard state_lock(state_mutex);
        current_service_id = service_id;
        audio_text = {};
    }

    logger.info(std::format("Service selected: {} [0x{:04X}], subchannel {}", selected_service.label, selected_service.id, selected_service.subchannel.id));
#if DAB_ENABLE_SLIDESHOW
    select_slideshow_service(service_id);
#endif
    notify_property_changes({"State", "Service", "AudioCodec", "DataApplication", "DataPathStatistics", "SlideShow", "SlideShowItems", "SlideShowStatistics", "DLS", "DLP", "SPIRevision", "JournalineRevision", "JournalineStatus", "TPEGRevision", "TPEGStatus"});
    return true;
}

#if DAB_ENABLE_DATA_APPLICATIONS
bool TunerHandler::set_data_service(const uint32_t service_id, const bool data_service, const uint8_t component_id, const uint16_t application_type) {
#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        logger.warning("Cannot set data service while scan is active");
        return false;
    }
#endif

    return select_data_service(service_id, data_service, component_id, application_type, false);
}
#endif


#if DAB_ENABLE_DATA_APPLICATIONS
bool TunerHandler::select_data_service(const uint32_t service_id, const bool data_service, const uint8_t component_id, const uint16_t application_type, const bool automatic) {
    if (!initialized) {
        if (!automatic) {
            logger.warning("Cannot set data service before initialization");
        }

        return false;
    }

    if (get_data_application_name(application_type).empty()) {
        logger.warning(std::format("Unsupported data application 0x{:03X}", application_type));
        return false;
    }

    const std::lock_guard tuner_lock(tuner_mutex);
    const uint64_t service_component_key = get_service_component_key(service_id, data_service, component_id);
    FICDecoder::ServiceComponent selected_component;
    const auto operation = helper->begin_operation(TunerHelper::TUNING_TIMEOUT);
    FICDecoder::UserApplication selected_application;
    bool application_found = false;

    {
        const std::lock_guard state_lock(state_mutex);

        if (automatic && (current_service_id != 0 || current_data_component_key != 0)) {
            return false;
        }

        const auto component = service_components.find(service_component_key);

        if (component == service_components.end()) {
            if (!automatic) {
                logger.warning(std::format("Unknown service component 0x{:X}/{}", service_id, component_id));
            }

            return false;
        }

        selected_component = component->second;

        for (const auto &application: selected_component.user_applications) {
            if (application.type == application_type && !application.xpad) {
                selected_application = application;
                application_found = true;
                break;
            }
        }
    }

    const std::string application_name = get_data_application_name(application_type);

    if (!application_found) {
        if (!automatic) {
            logger.warning(std::format("Service component 0x{:X}/{} does not carry packet-mode {}", service_id, component_id, application_name));
        }

        return false;
    }

    if (selected_component.subchannel == 0xFF || selected_component.packet_address == 0xFFFF) {
        if (!automatic) {
            logger.warning(std::format("Service component 0x{:X}/{} has incomplete packet-mode parameters", service_id, component_id));
        }

        return false;
    }

    bool requires_data_groups = false;
#if DAB_ENABLE_SPI
    requires_data_groups = requires_data_groups || application_type == SPI_APPLICATION_TYPE;
#endif
#if DAB_ENABLE_JOURNALINE
    requires_data_groups = requires_data_groups || application_type == JOURNALINE_APPLICATION_TYPE;
#endif

    if (requires_data_groups && !selected_component.data_groups) {
        if (!automatic) {
            logger.warning(std::format("{} component 0x{:X}/{} requires MSC data groups", application_name, service_id, component_id));
        }

        return false;
    }

#if DAB_ENABLE_TPEG
    if (application_type == TPEG_APPLICATION_TYPE && (!selected_component.data_groups || selected_component.type != 5 || selected_component.fec_scheme != 1)) {
        if (!automatic) {
            tpeg_logger.warning(std::format("TPEG component 0x{:X}/{} requires TDC, MSC data groups and packet FEC scheme 1", service_id, component_id));
        }

        return false;
    }
#endif

#if DAB_ENABLE_JOURNALINE
    if (application_type == JOURNALINE_APPLICATION_TYPE && selected_application.parameters.size() < 2) {
        return false;
    }
#endif

    if (selected_component.conditional_access || selected_application.conditional_access) {
        if (!automatic) {
            logger.warning(std::format("Service component 0x{:X}/{} uses conditional access", service_id, component_id));
        }

        return false;
    }

    if (selected_component.fec_scheme > 1) {
        logger.warning(std::format("Service component 0x{:X}/{} uses unsupported packet FEC scheme {}", service_id, component_id, selected_component.fec_scheme));
        return false;
    }

#if DAB_ENABLE_BACKGROUND_DATA
    if (audio_service_active && is_background_data_component(selected_component, application_type)) {
        return select_background_data(selected_component, application_type, true);
    }
#endif

#if DAB_ENABLE_AUDIO
    audio_mute.reset();
    audio_pipe->discard_buffered();
#endif
#if DAB_ENABLE_AAC
    msc_decoder->reset();
#endif
#if DAB_ENABLE_MP2
    mp2_decoder->reset();
#endif
    pad_decoder->reset();
    dynamic_label_decoder->reset();
#if DAB_ENABLE_SLIDESHOW
    reset_slideshow();
#endif
#if DAB_ENABLE_AUDIO
    audio_pipe->close();
#endif
    audio_service_active = false;
    audio_codec = AudioCodec::None;
    reset_data_service(true);

#if DAB_ENABLE_JOURNALINE
    if (application_type == JOURNALINE_APPLICATION_TYPE && !journaline_decoder->configure(selected_application.parameters)) {
        journaline_logger.warning(std::format("Could not configure Journaline component 0x{:X}/{}", service_id, component_id));
        return false;
    }
#endif

    packet_decoder->configure(selected_component.packet_address, selected_component.data_groups, selected_component.fec_scheme);
    helper->open_subchannel(selected_component.subchannel);

    if (!operation.succeeded()) {
        {
            const std::lock_guard state_lock(state_mutex);
            current_service_id = 0;
            audio_text = {};
        }

        notify_property_changes({"State", "Service", "AudioCodec", "HardwareStatus"});
        return false;
    }

    data_service_active = true;

    {
        const std::lock_guard state_lock(state_mutex);
        current_service_id = 0;
        current_data_component_key = service_component_key;
        current_data_application = application_type;
#if DAB_ENABLE_TPEG
        if (application_type == TPEG_APPLICATION_TYPE) {
            tpeg_suppressed = false;
        }
#endif
        pending_spi_component_key = 0;
        audio_text = {};
    }

#if DAB_ENABLE_SPI
    if (application_type == SPI_APPLICATION_TYPE) {
        spi_logger.info(std::format("SPI service selected{}: 0x{:X}/{} [packet {}, subchannel {}]", automatic ? " automatically" : "", service_id, component_id, selected_component.packet_address, selected_component.subchannel));
    }
#endif
#if DAB_ENABLE_JOURNALINE
    if (application_type == JOURNALINE_APPLICATION_TYPE) {
        journaline_logger.info(std::format("Journaline service selected: 0x{:X}/{} [packet {}, subchannel {}]", service_id, component_id, selected_component.packet_address, selected_component.subchannel));
    }
#endif
#if DAB_ENABLE_TPEG
    if (application_type == TPEG_APPLICATION_TYPE) {
        tpeg_logger.info(std::format("TPEG service selected: 0x{:X}/{} [packet {}, subchannel {}]", service_id, component_id, selected_component.packet_address, selected_component.subchannel));
    }
#endif

    notify_property_changes({"State", "Service", "AudioCodec", "DataApplication", "DataPathStatistics", "SlideShow", "SlideShowItems", "SlideShowStatistics", "DLS", "DLP", "SPIRevision", "JournalineRevision", "JournalineStatus", "TPEGRevision", "TPEGStatus"});
    return true;
}
#endif


bool TunerHandler::close_service() {
    if (!initialized) {
        logger.warning("Cannot close service before initialization");
        return false;
    }

#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        logger.warning("Cannot close service while scan is active");
        return false;
    }
#endif

    const std::lock_guard tuner_lock(tuner_mutex);
#if DAB_ENABLE_AUDIO
    audio_mute.reset();
    audio_pipe->discard_buffered();
#endif
#if DAB_ENABLE_SPI
    bool audio_service_selected = false;

    {
        const std::lock_guard state_lock(state_mutex);
        audio_service_selected = current_service_id != 0;
    }
#endif

#if DAB_ENABLE_AAC
    msc_decoder->reset();
#endif
#if DAB_ENABLE_MP2
    mp2_decoder->reset();
#endif
    pad_decoder->reset();
    dynamic_label_decoder->reset();
#if DAB_ENABLE_SLIDESHOW
    reset_slideshow();
#endif
#if DAB_ENABLE_DATA_APPLICATIONS
    reset_data_service(true);
#endif
    audio_service_active = false;
    audio_codec = AudioCodec::None;
#if DAB_ENABLE_AUDIO
    audio_pipe->close();
#endif

    {
        const std::lock_guard state_lock(state_mutex);
        current_service_id = 0;
        audio_text = {};
    }

    logger.info("Service closed");
    notify_property_changes({"State", "Service", "AudioCodec", "DataApplication", "DataPathStatistics", "SlideShow", "SlideShowItems", "SlideShowStatistics", "DLS", "DLP", "SPIRevision", "JournalineRevision", "JournalineStatus", "TPEGRevision", "TPEGStatus"});

#if DAB_ENABLE_SPI
    if (audio_service_selected) {
        queue_spi_service();
    }
#endif

    return true;
}

void TunerHandler::open_subchannel(const uint8_t subchannel) {
    if (!initialized) {
        return;
    }
#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        return;
    }
#endif

    const std::lock_guard lock(tuner_mutex);
#if DAB_ENABLE_AAC
    msc_decoder->reset();
#endif
#if DAB_ENABLE_MP2
    mp2_decoder->reset();
#endif
    pad_decoder->reset();
    dynamic_label_decoder->reset();
#if DAB_ENABLE_SLIDESHOW
    reset_slideshow();
#endif
#if DAB_ENABLE_DATA_APPLICATIONS
    reset_data_service(true);
#endif
    audio_service_active = false;
    audio_codec = AudioCodec::None;

    {
        const std::lock_guard state_lock(state_mutex);
        audio_text = {};
    }

    helper->open_subchannel(subchannel);
    logger.debug(std::format("Subchannel {} opened", subchannel));
    notify_property_changes({"AudioCodec", "DataApplication", "DataPathStatistics", "SlideShow", "SlideShowItems", "SlideShowStatistics", "DLS", "DLP", "SPIRevision", "JournalineRevision", "JournalineStatus", "TPEGRevision", "TPEGStatus"});
}

#if DAB_ENABLE_SCAN
void TunerHandler::capture_scan_result() {
    ScanResult result;

    {
        const std::lock_guard lock(state_mutex);

        if (ensemble_status.ensemble_id == 0 || scan_channel_index >= DAB_CHANNELS.size()) {
            return;
        }

        const auto &channel = DAB_CHANNELS[scan_channel_index];
        result.channel = std::string(channel.name);
        result.frequency = channel.frequency;
        result.ensemble_id = ensemble_status.ensemble_id;
        result.ecc = ensemble_status.ecc;
        result.label = ensemble_label;
        result.short_label = ensemble_short_label;

#if DAB_ENABLE_FULL_SCAN
        if (scan_mode == ScanMode::Full) {
            result.services.reserve(services.size());

            for (const auto &service: services | std::views::values) {
                result.services.push_back(service);
            }
        }
#endif

        scan_results.push_back(result);
    }

    logger.info(std::format("Scan found {} / {} kHz: 0x{:04X} {}{}", result.channel, result.frequency, result.ensemble_id, result.label, result.services.empty() ? "" : std::format(" ({} services)", result.services.size())));
    notify_property_changes({"ScanResults"});
}
#endif


#if DAB_ENABLE_SCAN
void TunerHandler::finish_scan(ScanOutcome outcome) {
    uint32_t restore_frequency = 0;
    uint16_t restore_service_id = 0;
    ScanMode completed_mode = ScanMode::None;

    {
        const std::lock_guard lock(state_mutex);
        restore_frequency = scan_saved_frequency;
        restore_service_id = scan_saved_service_id;
        completed_mode = scan_mode;
    }

    const bool seeking = completed_mode == ScanMode::SeekUp || completed_mode == ScanMode::SeekDown;
    bool restored = false;

    if (!seeking && restore_frequency != 0) {
        restored = tune_frequency(restore_frequency, false);
    }

    {
        const std::lock_guard lock(state_mutex);
        scan_phase = ScanPhase::Idle;

        if (scan_stop_requested.load()) {
            outcome = ScanOutcome::Cancelled;
        }

        switch (outcome) {
            case ScanOutcome::Completed:
                scan_state = "Completed";
                break;
            case ScanOutcome::Cancelled:
                scan_state = "Cancelled";
                break;
            case ScanOutcome::NotFound:
                scan_state = "NotFound";
                break;
            case ScanOutcome::Failed:
                scan_state = "Failed";
                break;
        }

        if (!seeking && outcome == ScanOutcome::Completed) {
            scan_progress.current = scan_progress.total;
        }

        scan_saved_frequency = 0;
        scan_saved_service_id = 0;
        scan_ensemble_seen_valid = false;

        if (restored && restore_service_id != 0) {
            scan_restore_service_id = restore_service_id;
            scan_restore_deadline = std::chrono::steady_clock::now() + SCAN_RESTORE_TIMEOUT;
        } else {
            scan_restore_service_id = 0;
        }

        scan_stop_requested = false;
        scan_active = false;
    }

    logger.info(std::format("{} {} at {} kHz", get_scan_mode(), get_scan_state(), get_frequency()));
    notify_property_changes({"State", "Frequency", "SignalStatus", "DateTime", "Ensemble", "EnsembleStatus", "Services", "ServicesReady", "ServiceComponents", "SPIRevision", "DataApplication", "DataPathStatistics", "JournalineRevision", "JournalineStatus", "TPEGRevision", "TPEGStatus", "ServiceLinks", "FrequencyInformation", "OtherEnsembleServices", "OEAnnouncementSupport", "OEAnnouncementSwitches", "FMAnnouncementSupport", "FMAnnouncementSwitches", "FIDC", "EmergencyWarning", "Service", "AudioCodec", "SlideShow", "SlideShowItems", "SlideShowStatistics", "DLS", "DLP", "ScanState", "ScanMode", "ScanProgress", "ScanResults"});
}
#endif


#if DAB_ENABLE_SCAN
void TunerHandler::scan_step() {
    if (!scan_active.load()) {
        return;
    }

    if (scan_stop_requested.load()) {
        finish_scan(ScanOutcome::Cancelled);
        return;
    }

    ScanPhase phase = ScanPhase::Idle;
    ScanMode current_mode = ScanMode::None;
    size_t channel_index = 0;
    size_t channels_checked = 0;

    {
        const std::lock_guard lock(state_mutex);
        phase = scan_phase;
        current_mode = scan_mode;
        channel_index = scan_channel_index;
        channels_checked = scan_channels_checked;
    }

    const bool seeking = current_mode == ScanMode::SeekUp || current_mode == ScanMode::SeekDown;

    if (phase == ScanPhase::Tune) {
        if (channel_index >= DAB_CHANNELS.size() || (seeking && channels_checked >= DAB_CHANNELS.size())) {
            finish_scan(seeking ? ScanOutcome::NotFound : ScanOutcome::Completed);
            return;
        }

        const auto &channel = DAB_CHANNELS[channel_index];

        {
            const std::lock_guard lock(state_mutex);
            scan_channels_checked++;
            scan_progress = {
                static_cast<uint16_t>(seeking ? scan_channels_checked : channel_index + 1),
                static_cast<uint16_t>(DAB_CHANNELS.size()),
                std::string(channel.name),
                channel.frequency
            };
        }

        notify_property_changes({"ScanProgress"});
        logger.debug(std::format("Scanning {} / {} kHz", channel.name, channel.frequency));

        if (!tune_frequency(channel.frequency, false)) {
            if (seeking) {
                finish_scan(ScanOutcome::Failed);
                return;
            }

            {
                const std::lock_guard lock(state_mutex);
                scan_channel_index++;
#if DAB_ENABLE_FULL_SCAN
                scan_tune_retried = false;
#endif
            }

            return;
        }

        const auto now = std::chrono::steady_clock::now();

        {
            const std::lock_guard lock(state_mutex);
            scan_frequency_started = now;
            scan_ensemble_seen = {};
            scan_ensemble_seen_valid = false;
            scan_last_service_change = now;
            scan_phase = ScanPhase::Receive;
        }

        return;
    }

    if (phase != ScanPhase::Receive) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    ScanMode mode = ScanMode::None;
    FICDecoder::EnsembleStatus status;
    SignalStatus current_signal_status;
    std::string current_ensemble_label;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point ensemble_seen;
    std::chrono::steady_clock::time_point last_service_change;
    bool ensemble_seen_valid = false;
#if DAB_ENABLE_FULL_SCAN
    bool service_information_complete = false;
    uint32_t fibs_received = 0;
    uint32_t fibs_valid = 0;
    bool tune_retried = false;
#endif

    {
        const std::lock_guard lock(state_mutex);
        mode = scan_mode;
        status = ensemble_status;
        current_signal_status = signal_status;
        current_ensemble_label = ensemble_label;
        started = scan_frequency_started;
        ensemble_seen = scan_ensemble_seen;
        ensemble_seen_valid = scan_ensemble_seen_valid;
        last_service_change = scan_last_service_change;
#if DAB_ENABLE_FULL_SCAN
        service_information_complete = scan_service_information_complete;
        fibs_received = fic_statistics.fibs_received;
        fibs_valid = fic_statistics.fibs_valid;
        tune_retried = scan_tune_retried;
#endif
    }

    const auto elapsed = now - started;
    const bool ensemble_found = status.ensemble_id != 0;

    if (seeking) {
        const bool synced = current_signal_status.ofdm_lock && current_signal_status.fec_lock && current_signal_status.fec_sync;
        const bool metadata_received = !current_ensemble_label.empty() && status.services_found >= status.service_count;
        const bool metadata_timeout = ensemble_seen_valid && now - ensemble_seen >= SEEK_METADATA_TIMEOUT;

        if (ensemble_found && synced && (metadata_received || metadata_timeout)) {
            finish_scan(ScanOutcome::Completed);
        } else if ((!ensemble_found && !current_signal_status.ofdm_lock && elapsed >= SEEK_NO_SIGNAL_TIMEOUT) || elapsed >= SEEK_NO_ENSEMBLE_TIMEOUT) {
            const std::lock_guard lock(state_mutex);
            const size_t step = mode == ScanMode::SeekUp ? 1 : DAB_CHANNELS.size() - 1;
            scan_channel_index = (scan_channel_index + step) % DAB_CHANNELS.size();
            scan_phase = ScanPhase::Tune;
        }

        return;
    }

    bool channel_finished = false;
    std::string_view finish_reason;
#if DAB_ENABLE_FULL_SCAN
    const bool valid_fic_received = fibs_valid >= 2;
#endif

#if DAB_ENABLE_FAST_SCAN
    if (mode == ScanMode::Fast) {
        const bool no_signal = !ensemble_found && elapsed >= FAST_SCAN_NO_SIGNAL_TIMEOUT && !current_signal_status.ofdm_lock;
        const bool label_received = !current_ensemble_label.empty();
        const bool label_timeout = ensemble_found && ensemble_seen_valid && now - ensemble_seen >= FAST_SCAN_LABEL_TIMEOUT;
        const bool no_ensemble_timeout = !ensemble_found && elapsed >= FAST_SCAN_NO_ENSEMBLE_TIMEOUT;

        if (no_signal) {
            channel_finished = true;
            finish_reason = "no signal";
        } else if (ensemble_found && label_received) {
            channel_finished = true;
            finish_reason = "ensemble label received";
        } else if (label_timeout) {
            channel_finished = true;
            finish_reason = "ensemble label timeout";
        } else if (no_ensemble_timeout) {
            channel_finished = true;
            finish_reason = "ensemble timeout";
        }
    }
#endif
#if DAB_ENABLE_FULL_SCAN
    if (mode == ScanMode::Full) {
        const bool no_signal = !ensemble_found && elapsed >= FULL_SCAN_NO_SIGNAL_TIMEOUT && !current_signal_status.ofdm_lock && fibs_received == 0;
        const auto unconfirmed_timeout = fibs_received != 0 ? FULL_SCAN_INVALID_FIC_TIMEOUT : FULL_SCAN_NO_FIC_TIMEOUT;
        const auto acquisition_timeout = valid_fic_received ? FULL_SCAN_NO_ENSEMBLE_TIMEOUT : unconfirmed_timeout;
        const auto minimum_time = status.service_count == 0 ? FULL_SCAN_FALLBACK_MINIMUM_TIME : FULL_SCAN_MINIMUM_TIME;
        const bool minimum_time_elapsed = ensemble_seen_valid && now - ensemble_seen >= minimum_time;
        const bool service_information_stable = now - last_service_change >= FULL_SCAN_STABLE_TIME;
        const bool ensemble_timeout = ensemble_found && ensemble_seen_valid && now - ensemble_seen >= FULL_SCAN_ENSEMBLE_TIMEOUT;
        const bool no_ensemble_timeout = !ensemble_found && elapsed >= acquisition_timeout;

        if (no_signal) {
            channel_finished = true;
            finish_reason = "no signal";
        } else if (ensemble_found && service_information_complete && service_information_stable && minimum_time_elapsed) {
            channel_finished = true;
            finish_reason = "service information complete";
        } else if (ensemble_timeout) {
            channel_finished = true;
            finish_reason = "service information timeout";
        } else if (no_ensemble_timeout) {
            channel_finished = true;
            finish_reason = "ensemble timeout";
        }
    }
#endif

    if (!channel_finished) {
        return;
    }

#if DAB_ENABLE_FULL_SCAN
    if (mode == ScanMode::Full && !ensemble_found && valid_fic_received && !tune_retried) {
        logger.debug(std::format("Retrying scan channel {} after {}", DAB_CHANNELS[channel_index].name, finish_reason));
        const std::lock_guard lock(state_mutex);
        scan_tune_retried = true;
        scan_phase = ScanPhase::Tune;
        return;
    }
#endif

    logger.debug(std::format("Scan channel {} finished: {}", DAB_CHANNELS[channel_index].name, finish_reason));

    if (ensemble_found) {
        capture_scan_result();
    }

    bool complete = false;

    {
        const std::lock_guard lock(state_mutex);
        scan_channel_index++;
        scan_phase = ScanPhase::Tune;
#if DAB_ENABLE_FULL_SCAN
        scan_tune_retried = false;
#endif
        complete = scan_channel_index >= DAB_CHANNELS.size();
    }

    if (complete) {
        finish_scan(ScanOutcome::Completed);
    }
}
#endif


#if DAB_ENABLE_SCAN
void TunerHandler::try_restore_scan_service() {
    if (scan_active.load()) {
        return;
    }

    uint16_t service_id = 0;
    bool expired = false;

    {
        const std::lock_guard lock(state_mutex);

        if (scan_restore_service_id == 0) {
            return;
        }

        if (services.contains(scan_restore_service_id)) {
            service_id = scan_restore_service_id;
            scan_restore_service_id = 0;
        } else if (std::chrono::steady_clock::now() >= scan_restore_deadline) {
            service_id = scan_restore_service_id;
            scan_restore_service_id = 0;
            expired = true;
        } else {
            return;
        }
    }

    if (expired) {
        logger.warning(std::format("Could not restore service 0x{:04X} after scan", service_id));
#if DAB_ENABLE_SPI
        queue_spi_service();
#endif
        return;
    }

    if (!set_service(service_id)) {
        logger.warning(std::format("Could not restore service 0x{:04X} after scan", service_id));
#if DAB_ENABLE_SPI
        queue_spi_service();
#endif
    }
}
#endif


void TunerHandler::handle() {
    auto next_signal_status = std::chrono::steady_clock::now();
#if DAB_ENABLE_DIAGNOSTICS
    auto next_hardware_status = next_signal_status;
#endif

    while (running) {
        const auto now = std::chrono::steady_clock::now();
        auto signal_status_interval = SIGNAL_STATUS_INTERVAL;
#if DAB_ENABLE_AUDIO
        if (audio_codec.load() != AudioCodec::None) {
            signal_status_interval = AUDIO_SIGNAL_STATUS_INTERVAL;
        }
#endif
#if DAB_ENABLE_SCAN
        if (scan_active.load()) {
            signal_status_interval = SCAN_SIGNAL_STATUS_INTERVAL;
        }
#endif
        bool read_signal_status = false;
        TunerHelper::SignalStatus hardware_signal_status;
        int usb_error = LIBUSB_SUCCESS;

        {
            const std::lock_guard lock(tuner_mutex);
            const auto operation = helper->begin_operation(TunerHelper::RECEIVE_TIMEOUT);
            const auto signal_time = std::chrono::steady_clock::now();
            read_signal_status = signal_time >= next_signal_status || next_signal_status > signal_time + signal_status_interval;

            if (read_signal_status) {
                hardware_signal_status = helper->get_signal_status();
#if DAB_ENABLE_AUDIO
                const bool locked = hardware_signal_status.ofdm_lock && hardware_signal_status.fec_lock && hardware_signal_status.fec_sync;
                audio_mute.update(locked, hardware_signal_status.msc_cer_valid, hardware_signal_status.msc_cer, std::chrono::steady_clock::now());
#endif
            }

            if (operation.succeeded()) {
                helper->read_data();
            }

            usb_error = helper->take_usb_error();
#if DAB_ENABLE_AUDIO
            if (usb_error != LIBUSB_SUCCESS) {
                audio_mute.reset();
            }
#endif
#if DAB_ENABLE_AUDIO && DAB_ENABLE_DIAGNOSTICS
            update_audio_statistics();
#endif
        }

        if (!running) {
            break;
        }

        if (usb_error != LIBUSB_SUCCESS) {
            handle_usb_error(usb_error);

            if (!running) {
                break;
            }
        } else {
            transient_usb_errors = 0;
        }

        if (read_signal_status && usb_error == LIBUSB_SUCCESS) {
            signal_status_event(hardware_signal_status);
            next_signal_status = now + signal_status_interval;
        }

#if DAB_ENABLE_DIAGNOSTICS
        if (now >= next_hardware_status) {
            notify_property_changes({"HardwareStatus", "FICRevision"});
#if DAB_ENABLE_AUDIO
            notify_property_changes({"AudioStatistics"});
#endif
            next_hardware_status = now + std::chrono::seconds(1);
        }
#endif

#if DAB_ENABLE_SCAN
        scan_step();
        try_restore_scan_service();
#endif
#if DAB_ENABLE_EWS
        expire_emergency_warning();
#endif
#if DAB_ENABLE_SPI
        try_select_pending_spi_service();
#endif
#if DAB_ENABLE_BACKGROUND_DATA
        update_background_data();
#endif

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

#if DAB_ENABLE_DIAGNOSTICS
void TunerHandler::record_data_received() {
    const std::lock_guard lock(state_mutex);
    hardware_status.last_data_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
#endif

void TunerHandler::update_ensemble_database() {
    EnsembleCache::Ensemble entry;
    const auto now = std::chrono::steady_clock::now();
    const bool complete = fic_decoder->is_service_information_complete();
    bool ready = false;
    bool published_changed = false;

    {
        const std::lock_guard lock(state_mutex);
        if (ensemble_status.ensemble_id == 0 || frequency == 0) {
            return;
        }
        entry = {ensemble, frequency, ensemble_status.ensemble_id, ensemble_status.ecc, ensemble_label, ensemble_short_label, {}};
        const auto minimum_time = ensemble_status.service_count == 0 ? std::chrono::seconds(4) : std::chrono::seconds(2);
        ready = complete && ensemble_status.ecc != 0 && now - ensemble_acquired >= minimum_time && now - last_service_change >= std::chrono::seconds(1);
        if (ready) {
            for (const auto &service: services | std::views::values) {
                entry.services.push_back(service);
            }
        }
    }

    const bool database_changed = ensemble_cache.update(entry, ready);
    {
        const std::lock_guard lock(state_mutex);
        published_changed = services_ready != ready || (ready && published_services != entry.services);
        services_ready = ready;
        if (ready) {
            published_services = std::move(entry.services);
        }
    }
    if (published_changed) {
        notify_property_changes({"Ensembles", "Services", "ServicesReady"});
    } else if (database_changed) {
        notify_property_changes({"Ensembles"});
    }
}

void TunerHandler::fic_data_event(const std::vector<uint8_t> &data) {
#if DAB_ENABLE_DIAGNOSTICS
    if (!data.empty()) {
        record_data_received();
    }
#endif
    if (!fic_data_received) {
        fic_data_received = true;
        fic_logger.debug(std::format("Receiving data ({} bytes)", data.size()));
    }

    fic_decoder->process(data);
    update_ensemble_database();
    const auto new_statistics = fic_decoder->get_statistics();
#if DAB_ENABLE_FULL_SCAN
    const bool new_service_information_complete = fic_decoder->is_service_information_complete();
#endif
#if DAB_ENABLE_SERVICE_LINKING
    const auto new_service_links = fic_decoder->get_service_links();
    const auto new_other_ensemble_services = fic_decoder->get_other_ensemble_services();
    bool service_links_changed = false;
    bool other_ensemble_services_changed = false;
#endif
#if DAB_ENABLE_FREQUENCY_INFO
    const auto new_frequency_information = fic_decoder->get_frequency_information();
    bool frequency_information_changed = false;
#endif
#if DAB_ENABLE_ANNOUNCEMENTS
    const auto new_oe_announcement_support = fic_decoder->get_oe_announcement_support();
    const auto new_oe_announcement_switches = fic_decoder->get_oe_announcement_switches();
    const auto new_fm_announcement_support = fic_decoder->get_fm_announcement_support();
    const auto new_fm_announcement_switches = fic_decoder->get_fm_announcement_switches();
    bool oe_announcement_support_changed = false;
    bool oe_announcement_switches_changed = false;
    bool fm_announcement_support_changed = false;
    bool fm_announcement_switches_changed = false;
#endif
#if DAB_ENABLE_FIDC
    const auto new_fidc_entries = fic_decoder->get_fidc_entries();
    bool fidc_entries_changed = false;
#endif
#if DAB_ENABLE_EWS
    const auto new_emergency_warning_sequence = fic_decoder->get_emergency_warning_sequence();
    bool emergency_warning_changed = false;
#endif

    {
        const std::lock_guard lock(state_mutex);
        fic_statistics = new_statistics;
#if DAB_ENABLE_FULL_SCAN
        if (scan_active.load() && scan_service_information_complete != new_service_information_complete) {
            scan_last_service_change = std::chrono::steady_clock::now();
        }

        scan_service_information_complete = new_service_information_complete;
#endif
#if DAB_ENABLE_SERVICE_LINKING
        service_links_changed = service_links != new_service_links;
        other_ensemble_services_changed = other_ensemble_services != new_other_ensemble_services;
        service_links = new_service_links;
        other_ensemble_services = new_other_ensemble_services;
#endif
#if DAB_ENABLE_FREQUENCY_INFO
        frequency_information_changed = frequency_information != new_frequency_information;
        frequency_information = new_frequency_information;
#endif
#if DAB_ENABLE_ANNOUNCEMENTS
        oe_announcement_support_changed = oe_announcement_support != new_oe_announcement_support;
        oe_announcement_switches_changed = oe_announcement_switches != new_oe_announcement_switches;
        fm_announcement_support_changed = fm_announcement_support != new_fm_announcement_support;
        fm_announcement_switches_changed = fm_announcement_switches != new_fm_announcement_switches;
        oe_announcement_support = new_oe_announcement_support;
        oe_announcement_switches = new_oe_announcement_switches;
        fm_announcement_support = new_fm_announcement_support;
        fm_announcement_switches = new_fm_announcement_switches;
#endif
#if DAB_ENABLE_FIDC
        fidc_entries_changed = fidc_entries != new_fidc_entries;
        fidc_entries = new_fidc_entries;
#endif
#if DAB_ENABLE_EWS
        if (emergency_warning_sequence != new_emergency_warning_sequence) {
            const auto new_emergency_warning_status = fic_decoder->get_emergency_warning_status();
            emergency_warning_changed = emergency_warning_status != new_emergency_warning_status;
            emergency_warning_status = new_emergency_warning_status;
            emergency_warning_sequence = new_emergency_warning_sequence;
            emergency_warning_seen = std::chrono::steady_clock::now();
            emergency_warning_seen_valid = true;
        }
#endif
    }

#if DAB_ENABLE_SERVICE_LINKING
    if (service_links_changed) {
        notify_property_changes({"ServiceLinks"});
    }

    if (other_ensemble_services_changed) {
        notify_property_changes({"OtherEnsembleServices"});
    }
#endif
#if DAB_ENABLE_FREQUENCY_INFO
    if (frequency_information_changed) {
        notify_property_changes({"FrequencyInformation"});
    }
#endif
#if DAB_ENABLE_ANNOUNCEMENTS
    if (oe_announcement_support_changed) {
        notify_property_changes({"OEAnnouncementSupport"});
    }

    if (oe_announcement_switches_changed) {
        notify_property_changes({"OEAnnouncementSwitches"});
    }

    if (fm_announcement_support_changed) {
        notify_property_changes({"FMAnnouncementSupport"});
    }

    if (fm_announcement_switches_changed) {
        notify_property_changes({"FMAnnouncementSwitches"});
    }
#endif
#if DAB_ENABLE_FIDC
    if (fidc_entries_changed) {
        notify_property_changes({"FIDC"});
    }
#endif
#if DAB_ENABLE_EWS
    if (emergency_warning_changed) {
        notify_property_changes({"EmergencyWarning"});
    }
#endif
}

void TunerHandler::msc_data_event(const std::vector<uint8_t> &data) {
#if DAB_ENABLE_DIAGNOSTICS
    if (!data.empty()) {
        record_data_received();
    }
#endif
#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        return;
    }
#endif

    if (!msc_data_received) {
        msc_data_received = true;
        msc_logger.debug(std::format("Receiving data ({} bytes)", data.size()));
    }

    if (audio_service_active) {
#if DAB_ENABLE_AAC
        if (audio_codec.load() == AudioCodec::AAC) {
            msc_decoder->process(data);
            return;
        }
#endif
#if DAB_ENABLE_MP2
        if (audio_codec.load() == AudioCodec::MP2) {
            mp2_decoder->process(data);
            return;
        }
#endif
    }
#if DAB_ENABLE_PACKET
    else if (data_service_active) {
        packet_decoder->process(data);
#if DAB_ENABLE_DIAGNOSTICS
        maybe_notify_data_path_statistics();
#endif
    }
#endif
}

#if DAB_ENABLE_AAC
void TunerHandler::access_unit_event(const MSCDecoder::AccessUnit &access_unit) const {
    if (!audio_service_active || audio_codec.load() != AudioCodec::AAC || audio_mute.is_muted()) {
        return;
    }

    audio_pipe->write(LOASEncoder::encode(access_unit.data, access_unit.sampling_rate, access_unit.channels, access_unit.sbr));
}
#endif


#if DAB_ENABLE_MP2
void TunerHandler::mp2_frame_event(const MP2Decoder::Frame &frame) const {
    if (!audio_service_active || audio_codec.load() != AudioCodec::MP2 || audio_mute.is_muted()) {
        return;
    }

    audio_pipe->write(frame.data);
}
#endif


void TunerHandler::pad_data_event(const std::vector<uint8_t> &xpad_data, const std::array<uint8_t, 2> &fpad_data) {
    pad_decoder->process(xpad_data, fpad_data);
#if DAB_ENABLE_SLIDESHOW && DAB_ENABLE_DIAGNOSTICS
    maybe_notify_slideshow_statistics();
#endif
}

void TunerHandler::dynamic_label_data_event(const bool start, const std::vector<uint8_t> &data) const {
    dynamic_label_decoder->process(start, data);
}

void TunerHandler::mot_data_event(const int type, const std::vector<uint8_t> &data) const {
#if !DAB_ENABLE_XPAD_DATA
    (void)type;
    (void)data;
#endif
#if DAB_ENABLE_SLIDESHOW
    slideshow_decoder->process(type, data);
#endif
#if DAB_ENABLE_SPI
    spi_mot_decoder->process_xpad(type, data);
#endif
#if DAB_ENABLE_JOURNALINE
    journaline_decoder->process_xpad(type, data);
#endif
}

#if DAB_ENABLE_PACKET
void TunerHandler::packet_data_event(const std::vector<uint8_t> &data) const {
    uint16_t application_type = 0;

    {
        const std::lock_guard lock(state_mutex);
        application_type = current_data_application;
    }

#if DAB_ENABLE_SPI
    if (application_type == SPI_APPLICATION_TYPE) {
        spi_mot_decoder->process_group(data);
        return;
    }
#endif
#if DAB_ENABLE_JOURNALINE
    if (application_type == JOURNALINE_APPLICATION_TYPE) {
        journaline_decoder->process_group(data);
        return;
    }
#endif
#if DAB_ENABLE_TPEG
    if (application_type == TPEG_APPLICATION_TYPE) {
        tpeg_decoder->process_group(data);
    }
#endif
}
#endif


#if DAB_ENABLE_BACKGROUND_DATA
void TunerHandler::background_data_event(const std::vector<uint8_t> &data) {
#if DAB_ENABLE_DIAGNOSTICS
    if (!data.empty()) {
        record_data_received();
    }
#endif
    if (!background_data_active) {
        return;
    }

    background_packet_decoder->process(data);
#if DAB_ENABLE_DIAGNOSTICS
    maybe_notify_data_path_statistics();
#endif
}
#endif

#if DAB_ENABLE_BACKGROUND_DATA
void TunerHandler::background_packet_event(const std::vector<uint8_t> &data) {
#if DAB_ENABLE_SPI
    if (background_data_application == SPI_APPLICATION_TYPE) {
        background_spi_mot_decoder->process_group(data);
        return;
    }
#endif
#if DAB_ENABLE_TPEG
    if (background_data_application == TPEG_APPLICATION_TYPE) {
        tpeg_decoder->process_group(data);
    }
#endif
}
#endif


#if DAB_ENABLE_SPI
void TunerHandler::spi_object_event(const MOTDecoder::Object &object) {
    const bool spi_object = object.content_type == 7 && object.content_subtype <= 2;
    SPIDecoder::ProcessResult process_result;
    std::string decode_status = "MOT object";

    if (spi_object) {
#if DAB_ENABLE_DIAGNOSTICS
        {
            const std::lock_guard lock(state_mutex);
            spi_data_objects++;
        }
#endif

        process_result = spi_decoder->process(object.transport_id, object.content_name, object.content_subtype, object.body, object.scope_id, object.scope_end);

        if (!process_result.accepted) {
            decode_status = std::format("Rejected: {}", process_result.error);
        } else if (object.content_subtype == 0) {
            decode_status = std::format("SI: {} services", process_result.services);
        } else if (object.content_subtype == 1) {
            decode_status = std::format("PI: {} programmes", process_result.programmes);
        } else {
            decode_status = std::format("GI: {} groups", process_result.programme_groups);
        }
    }

    const std::string extension = get_image_extension(object.body, object.mime_type);

    if (!extension.empty() && !spi_object) {
        decode_status = "Asset";
    }

    const SPIObject new_object = {
        object.transport_id,
        object.body_size,
        object.content_type,
        object.content_subtype,
        object.content_name,
        object.mime_type,
        object.compression_type,
        object.profile_subset,
        decode_status
    };
    bool object_changed = false;

    {
        const std::lock_guard lock(state_mutex);
        const auto iterator = spi_objects.find(object.transport_id);
        object_changed = iterator == spi_objects.end() || iterator->second != new_object;

        if (object_changed) {
            spi_objects[object.transport_id] = new_object;
        }
    }

    if (extension.empty()) {
        if (object_changed) {
            const std::lock_guard lock(state_mutex);
            spi_revision++;
        }

        if (object_changed) {
            notify_property_changes({"SPIRevision"});
        }
        return;
    }

    const std::string path = spi_file->write(object.transport_id, object.body, extension);

    if (path.empty()) {
        if (object_changed) {
            const std::lock_guard lock(state_mutex);
            spi_revision++;
        }

        if (object_changed) {
            notify_property_changes({"SPIRevision"});
        }
        return;
    }

    const SPIAsset new_asset = {object.transport_id, object.content_name, path, object.mime_type};
    std::vector<std::string> old_paths;
    bool asset_changed = false;

    {
        const std::lock_guard lock(state_mutex);
        bool same_asset_present = false;

        for (auto iterator = spi_assets.begin(); iterator != spi_assets.end();) {
            const bool same_transport = iterator->first == object.transport_id;
            const bool same_name = !object.content_name.empty() && iterator->second.content_name == object.content_name;

            if (!same_transport && !same_name) {
                ++iterator;
                continue;
            }

            if (same_transport && iterator->second == new_asset) {
                same_asset_present = true;
            } else {
                asset_changed = true;
            }

            if (iterator->second.path != path) {
                old_paths.push_back(iterator->second.path);
            }

            iterator = spi_assets.erase(iterator);
        }

        if (!same_asset_present) {
            asset_changed = true;
        }

        spi_assets[object.transport_id] = new_asset;

        if (object_changed || asset_changed) {
            spi_revision++;
        }
    }

    for (const auto &old_path: old_paths) {
        spi_file->remove(old_path);
    }

#if DAB_ENABLE_LOGO_CACHE
    cache_service_logos(object.content_name);
#endif
    spi_logger.info(std::format("Image: {}{} ({} bytes)", path, object.content_name.empty() ? "" : std::format(" [{}]", object.content_name), object.body.size()));
    if (object_changed || asset_changed) {
        notify_property_changes({"SPIRevision"});
    }
}
#endif


#if DAB_ENABLE_SPI
void TunerHandler::spi_object_removed_event(const uint16_t transport_id) {
    spi_decoder->remove(transport_id);
    std::string path;
    bool changed = false;

    {
        const std::lock_guard lock(state_mutex);
        changed = spi_objects.erase(transport_id) != 0;
        const auto asset = spi_assets.find(transport_id);

        if (asset != spi_assets.end()) {
            path = asset->second.path;
            spi_assets.erase(asset);
            changed = true;
        }

        if (changed) {
            spi_revision++;
        }
    }

    if (!path.empty()) {
        spi_file->remove(path);
        spi_logger.debug(std::format("Removed image: {}", path));
    }

    if (changed) {
        notify_property_changes({"SPIRevision"});
    }
}
#endif


#if DAB_ENABLE_SPI
void TunerHandler::spi_update_event(const std::vector<SPIDecoder::ServiceInformation> &service_information, const std::vector<SPIDecoder::Programme> &programmes, const std::vector<SPIDecoder::ProgrammeGroup> &programme_groups) {
    bool changed = false;

    {
        const std::lock_guard lock(state_mutex);
#if DAB_ENABLE_DIAGNOSTICS
        spi_data_updates++;
#endif
        changed = spi_service_information != service_information || spi_programmes != programmes || spi_programme_groups != programme_groups;
        spi_service_information = service_information;
        spi_programmes = programmes;
        spi_programme_groups = programme_groups;

        if (changed) {
            spi_revision++;
        }
    }

#if DAB_ENABLE_LOGO_CACHE
    if (changed) {
        cache_service_logos();
    }
#endif
    if (changed) {
        notify_property_changes({"SPIRevision"});
    }
}
#endif


#if DAB_ENABLE_JOURNALINE
void TunerHandler::journaline_update_event(const JournalineDecoder::Status &status, const std::vector<JournalineDecoder::Object> &objects) {
    bool changed = false;
    bool status_changed = false;

    {
        const std::lock_guard lock(state_mutex);
        status_changed = journaline_status != status;
        changed = status_changed || journaline_objects != objects;
        journaline_status = status;
        journaline_objects = objects;

        if (changed) {
            journaline_revision++;
        }
    }

    if (changed && status_changed) {
        notify_property_changes({"JournalineRevision", "JournalineStatus"});
    } else if (changed) {
        notify_property_changes({"JournalineRevision"});
    }
}
#endif


#if DAB_ENABLE_TPEG
void TunerHandler::tpeg_update_event(const TPEGDecoder::Status &status, const std::vector<TPEGDecoder::ServiceIdentifier> &services) {
    bool revision_changed = false;
    bool status_changed = false;

    {
        const std::lock_guard lock(state_mutex);
        status_changed = tpeg_status != status;
        const bool services_changed = tpeg_services != services;
        tpeg_status = status;
        tpeg_services = services;
        revision_changed = tpeg_frame_dirty || services_changed;
        tpeg_frame_dirty = false;

        if (revision_changed) {
            tpeg_revision++;
        }
    }

    if (revision_changed && status_changed) {
        notify_property_changes({"TPEGRevision", "TPEGStatus"});
    } else if (revision_changed) {
        notify_property_changes({"TPEGRevision"});
    } else if (status_changed) {
        notify_property_changes({"TPEGStatus"});
    }
}
#endif


#if DAB_ENABLE_TPEG
void TunerHandler::tpeg_frame_event(const TPEGDecoder::Frame &frame) {
    const std::lock_guard lock(state_mutex);
    tpeg_content_decoder.process_frame(frame);
    tpeg_frames.push_back(frame);

    if (tpeg_frames.size() > TPEG_FRAME_HISTORY) {
        tpeg_frames.erase(tpeg_frames.begin(), tpeg_frames.begin() + static_cast<std::ptrdiff_t>(tpeg_frames.size() - TPEG_FRAME_HISTORY));
    }

    tpeg_frame_dirty = true;
}
#endif


#if DAB_ENABLE_SLIDESHOW
void TunerHandler::slideshow_object_event(const SlideShowDecoder::Slide &slide) {
    SlideShowItem item;
    item.transport_id = slide.transport_id;
    item.content_name = slide.content_name;
    item.trigger_time_present = slide.trigger_time_present;
    item.trigger_now = slide.trigger_now;
    item.trigger_time = slide.trigger_time;
    item.expire_time_present = slide.expire_time_present;
    item.expire_time = slide.expire_time;
    item.category_id = slide.category_id;
    item.slide_id = slide.slide_id;
    item.category_title = slide.category_title;
    item.click_through_url = slide.click_through_url;
    item.alternative_location_url = slide.alternative_location_url;
    item.alert = slide.alert;

    std::string previous_path;
    bool changed = false;

    {
        const std::lock_guard lock(state_mutex);
        const auto existing = slideshow_items.find(slide.content_name);

        if (slide.metadata_update && existing != slideshow_items.end()) {
            item.path = existing->second.path;
        }
    }

    if (item.path.empty()) {
        item.path = slideshow_file->write(slide.transport_id, slide.data, slide.extension);

        if (item.path.empty()) {
            return;
        }
    }

    {
        const std::lock_guard lock(state_mutex);
        const auto existing = slideshow_items.find(slide.content_name);

        if (existing != slideshow_items.end()) {
            previous_path = existing->second.path;
        }

        item.active = current_slideshow_content_name == slide.content_name && current_slideshow_path == item.path;
        changed = existing == slideshow_items.end() || existing->second != item;
        slideshow_items[slide.content_name] = item;
    }

    bool previous_path_active = false;

    {
        const std::lock_guard lock(state_mutex);
        previous_path_active = previous_path == current_slideshow_path;
    }

    if (!previous_path.empty() && previous_path != item.path && !previous_path_active) {
        slideshow_file->remove(previous_path);
    }

    if (changed) {
        notify_property_changes({"SlideShowItems"});
    }

    if (!slide.metadata_update) {
        pad_logger.info(std::format("SlideShow object: {} [{}] ({} bytes)", item.path, slide.content_name, slide.data.size()));
    }
}
#endif


#if DAB_ENABLE_SLIDESHOW
void TunerHandler::slideshow_display_event(const SlideShowDecoder::Slide &slide) {
    std::string previous_path;
    std::string path;
    bool slideshow_changed = false;
    bool items_changed = false;

    {
        const std::lock_guard lock(state_mutex);
        const auto item = slideshow_items.find(slide.content_name);

        if (item == slideshow_items.end()) {
            return;
        }

        previous_path = current_slideshow_path;
        slideshow_changed = current_slideshow_content_name != slide.content_name || current_slideshow_path != item->second.path;

        for (auto &[content_name, slideshow_item]: slideshow_items) {
            const bool active = content_name == slide.content_name;
            items_changed |= slideshow_item.active != active;
            slideshow_item.active = active;
        }

        current_slideshow_content_name = slide.content_name;
        current_slideshow_path = item->second.path;
        path = current_slideshow_path;
    }

    if (slideshow_changed && !previous_path.empty() && previous_path != path) {
        slideshow_file->remove(previous_path);
    }

    if (slideshow_changed) {
        pad_logger.info(std::format("SlideShow display: {} [{}]", path, slide.content_name));
    }

    if (slideshow_changed && items_changed) {
        notify_property_changes({"SlideShow", "SlideShowItems"});
    } else if (slideshow_changed) {
        notify_property_changes({"SlideShow"});
    } else if (items_changed) {
        notify_property_changes({"SlideShowItems"});
    }
}
#endif


#if DAB_ENABLE_SLIDESHOW
void TunerHandler::slideshow_removed_event(const std::string &content_name) {
    std::string path;
    std::string current_path;
    bool current_removed = false;

    {
        const std::lock_guard lock(state_mutex);
        const auto item = slideshow_items.find(content_name);

        if (item == slideshow_items.end()) {
            return;
        }

        path = item->second.path;
        slideshow_items.erase(item);

        if (current_slideshow_content_name == content_name) {
            current_path = current_slideshow_path;
            current_slideshow_content_name.clear();
            current_slideshow_path.clear();
            current_removed = true;
        }
    }

    slideshow_file->remove(path);

    if (!current_path.empty() && current_path != path) {
        slideshow_file->remove(current_path);
    }

    pad_logger.debug(std::format("Removed SlideShow object: {} [{}]", path, content_name));

    if (current_removed) {
        notify_property_changes({"SlideShow", "SlideShowItems"});
    } else {
        notify_property_changes({"SlideShowItems"});
    }
}
#endif


#if DAB_ENABLE_SLIDESHOW
void TunerHandler::reset_slideshow() {
    slideshow_decoder->reset();
    slideshow_file->clear();

    const std::lock_guard lock(state_mutex);
    slideshow_items.clear();
    current_slideshow_content_name.clear();
    current_slideshow_path.clear();
#if DAB_ENABLE_DIAGNOSTICS
    slideshow_statistics = slideshow_decoder->get_statistics();
    slideshow_statistics_emitted_valid = false;
#endif
}
#endif


#if DAB_ENABLE_SLIDESHOW
void TunerHandler::select_slideshow_service(const uint16_t service_id) {
    uint32_t service_frequency = 0;
    uint16_t ensemble_id = 0;

    {
        const std::lock_guard lock(state_mutex);
        service_frequency = frequency;
        ensemble_id = ensemble_status.ensemble_id;
    }

    slideshow_decoder->select_service(service_frequency, ensemble_id, service_id);
#if DAB_ENABLE_DIAGNOSTICS
    maybe_notify_slideshow_statistics();
#endif
}
#endif


#if DAB_ENABLE_SLIDESHOW && DAB_ENABLE_DIAGNOSTICS
void TunerHandler::maybe_notify_slideshow_statistics() {
    const auto now = std::chrono::steady_clock::now();
    bool changed = false;

    {
        const std::lock_guard lock(state_mutex);

        if (slideshow_statistics_emitted_valid && now - slideshow_statistics_emitted < SLIDESHOW_STATISTICS_INTERVAL) {
            return;
        }

        const auto statistics = slideshow_decoder->get_statistics();
        changed = !slideshow_statistics_emitted_valid || slideshow_statistics != statistics;
        slideshow_statistics = statistics;
        slideshow_statistics_emitted = now;
        slideshow_statistics_emitted_valid = true;
    }

    if (changed) {
        notify_property_changes({"SlideShowStatistics"});
    }
}
#endif


void TunerHandler::audio_text_event(const DynamicLabelDecoder::AudioText &audio_text) {
    {
        const std::lock_guard lock(state_mutex);
        this->audio_text = audio_text;
    }

    notify_property_changes({"DLS", "DLP"});

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
        last_service_change = std::chrono::steady_clock::now();
#if DAB_ENABLE_SCAN
        if (scan_active.load()) {
            scan_last_service_change = std::chrono::steady_clock::now();
        }
#endif
    }

    fic_logger.info(std::format("Ensemble 0x{:04X}: {}", ensemble.id, ensemble.label));
    notify_property_changes({"Ensemble"});
}

void TunerHandler::ensemble_status_data_event(const FICDecoder::EnsembleStatus &ensemble_status) {
    FICDecoder::EnsembleStatus previous_status;

    {
        const std::lock_guard lock(state_mutex);
        previous_status = this->ensemble_status;
        this->ensemble_status = ensemble_status;
        if (previous_status.ensemble_id != ensemble_status.ensemble_id || previous_status.ecc != ensemble_status.ecc ||
            previous_status.service_count != ensemble_status.service_count || previous_status.services_found != ensemble_status.services_found ||
            previous_status.reconfiguration_counter != ensemble_status.reconfiguration_counter) {
            last_service_change = std::chrono::steady_clock::now();
        }
        if (ensemble_status.ensemble_id != 0 && ensemble_acquired == std::chrono::steady_clock::time_point{}) {
            ensemble_acquired = std::chrono::steady_clock::now();
        }

#if DAB_ENABLE_SCAN
        if (scan_active.load()) {
            const auto now = std::chrono::steady_clock::now();

            if (ensemble_status.ensemble_id != 0 && !scan_ensemble_seen_valid) {
                scan_ensemble_seen = now;
                scan_ensemble_seen_valid = true;
            }

            if (previous_status.service_count != ensemble_status.service_count || previous_status.services_found != ensemble_status.services_found) {
                scan_last_service_change = now;
            }
        }
#endif
    }

    notify_property_changes({"EnsembleStatus"});

#if DAB_ENABLE_SPI && DAB_ENABLE_LOGO_CACHE
    if (previous_status.ecc != ensemble_status.ecc || previous_status.ensemble_id != ensemble_status.ensemble_id) {
        load_cached_service_logos();
        cache_service_logos();
    }
#endif

    if (previous_status.alarm_supported != ensemble_status.alarm_supported) {
        fic_logger.info(std::format("Alarm support {}", ensemble_status.alarm_supported ? "enabled" : "disabled"));
    }

    if (previous_status.alarm_active != ensemble_status.alarm_active) {
        if (ensemble_status.alarm_active) {
            fic_logger.warning("Alarm active");
        } else {
            fic_logger.info("Alarm ended");
        }
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
    bool selected_service = false;

    {
        const std::lock_guard lock(state_mutex);
        new_service = !services.contains(service.id);
        selected_service = current_service_id == service.id;
        if (new_service || services.at(service.id).label != service.label || services.at(service.id).short_label != service.short_label ||
            services.at(service.id).ecc != service.ecc || services.at(service.id).subchannel != service.subchannel || services.at(service.id).dab_plus != service.dab_plus) {
            last_service_change = std::chrono::steady_clock::now();
        }
        services[service.id] = service;

#if DAB_ENABLE_SCAN
        if (scan_active.load()) {
            scan_last_service_change = std::chrono::steady_clock::now();
        }
#endif
    }

#if DAB_ENABLE_SPI && DAB_ENABLE_LOGO_CACHE
    if (service.ecc != 0 && service.ecc != ensemble_status.ecc) {
        load_cached_service_logos();
    }
    cache_service_logos();
#endif

    if (selected_service) {
        notify_property_changes({"Service"});
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
        last_service_change = std::chrono::steady_clock::now();

#if DAB_ENABLE_SCAN
        if (scan_active.load()) {
            scan_last_service_change = std::chrono::steady_clock::now();
        }
#endif

        if (current_service_id == service_id) {
            current_service_id = 0;
            audio_text = {};
            selected = true;
        }
    }

    if (selected) {
        audio_service_active = false;
        audio_codec = AudioCodec::None;
#if DAB_ENABLE_AUDIO
        audio_pipe->close();
#endif
        notify_property_changes({"State", "Service", "AudioCodec", "DLS", "DLP"});
#if DAB_ENABLE_SPI
        queue_spi_service();
#endif
    }

    fic_logger.info(std::format("Service 0x{:04X} removed", service_id));
}

void TunerHandler::service_component_data_event(const FICDecoder::ServiceComponent &service_component) {
    const uint64_t service_component_key = get_service_component_key(service_component.service_id, service_component.data_service, service_component.id);
#if DAB_ENABLE_SPI
    bool queue_spi = false;
#endif
    bool new_service_component = false;

    {
        const std::lock_guard lock(state_mutex);
        new_service_component = !service_components.contains(service_component_key);
        service_components[service_component_key] = service_component;

#if DAB_ENABLE_SPI
        bool scan_allows_selection = true;
#if DAB_ENABLE_SCAN
        scan_allows_selection = !scan_active.load() && scan_restore_service_id == 0;
#endif
        if (scan_allows_selection && current_service_id == 0 && current_data_component_key == 0 && pending_spi_component_key == 0 && is_packet_spi_component(service_component)) {
            pending_spi_component_key = service_component_key;
            queue_spi = true;
        }
#endif
    }

    notify_property_changes({"ServiceComponents"});

    if (new_service_component) {
        fic_logger.debug(std::format("Service component 0x{:X}/{} found", service_component.service_id, service_component.id));
    }

#if DAB_ENABLE_SPI
    if (queue_spi) {
        spi_logger.debug(std::format("Packet-mode SPI component 0x{:X}/{} queued", service_component.service_id, service_component.id));
    }
#endif
}

void TunerHandler::service_component_removed_event(const uint64_t service_component_key) {
    bool selected = false;

    {
        const std::lock_guard lock(state_mutex);
        service_components.erase(service_component_key);

        if (pending_spi_component_key == service_component_key) {
            pending_spi_component_key = 0;
        }

        if (current_data_component_key == service_component_key) {
            current_data_component_key = 0;
            selected = true;
        }
    }

#if DAB_ENABLE_DATA_APPLICATIONS
    if (selected) {
        reset_data_service();
        notify_property_changes({"State", "DataApplication", "DataPathStatistics", "SPIRevision", "JournalineRevision", "JournalineStatus", "TPEGRevision", "TPEGStatus"});
        logger.info("Selected data service component removed");
#if DAB_ENABLE_SPI
        queue_spi_service();
#endif
    }
#else
    (void) selected;
#endif

    notify_property_changes({"ServiceComponents"});
}

void TunerHandler::signal_status_event(const TunerHelper::SignalStatus &hardware_signal_status) {
    SignalStatus new_signal_status;
    new_signal_status.ofdm_lock = hardware_signal_status.ofdm_lock;
    new_signal_status.fec_lock = hardware_signal_status.fec_lock;
    new_signal_status.fec_sync = hardware_signal_status.fec_sync;
    const auto now = std::chrono::steady_clock::now();
    bool changed = false;
    bool recover = false;

    {
        const std::lock_guard lock(state_mutex);
        bool fic_crc_error_rate_valid = false;
        uint32_t fic_crc_error_rate = 0;
        const uint32_t fibs_received = fic_statistics.fibs_received;
        const uint32_t fibs_crc_errors = fic_statistics.fibs_crc_errors;

        if (fibs_received >= signal_fic_received && fibs_crc_errors >= signal_fic_crc_errors) {
            const uint32_t received = fibs_received - signal_fic_received;
            const uint32_t errors = fibs_crc_errors - signal_fic_crc_errors;

            if (received != 0) {
                fic_crc_error_rate_valid = true;
                fic_crc_error_rate = static_cast<uint32_t>(static_cast<uint64_t>(errors) * 10000 / received);
            }
        }

        signal_fic_received = fibs_received;
        signal_fic_crc_errors = fibs_crc_errors;

        const auto filtered = signal_quality_filter.update(
            hardware_signal_status.ofdm_lock,
            hardware_signal_status.fec_lock,
            hardware_signal_status.fec_sync,
            hardware_signal_status.msc_cer_valid,
            hardware_signal_status.antenna_level,
            hardware_signal_status.msc_cer,
            fic_crc_error_rate_valid,
            fic_crc_error_rate
        );

        new_signal_status.signal_quality_valid = filtered.signal_quality_valid;
        new_signal_status.signal_quality = filtered.signal_quality;
        new_signal_status.antenna_level = filtered.antenna_level;
        new_signal_status.msc_cer_valid = filtered.msc_cer_valid;
        new_signal_status.msc_cer = filtered.msc_cer;
        new_signal_status.fic_crc_error_rate_valid = filtered.fic_crc_error_rate_valid;
        new_signal_status.fic_crc_error_rate = filtered.fic_crc_error_rate;
        signal_status = new_signal_status;

        changed = !emitted_signal_status_valid || signal_status_change_is_significant(new_signal_status, emitted_signal_status);

        if (changed) {
            emitted_signal_status = new_signal_status;
            emitted_signal_status_valid = true;
        }

        const bool service_selected = current_service_id != 0 || current_data_component_key != 0;
        const bool fec_stalled = service_selected && new_signal_status.ofdm_lock && (!new_signal_status.fec_lock || !new_signal_status.fec_sync);

#if DAB_ENABLE_SCAN
        if (!scan_active.load() && fec_stalled) {
#else
        if (fec_stalled) {
#endif
            if (!fec_unlock_started_valid) {
                fec_unlock_started = now;
                fec_unlock_started_valid = true;
            } else if (now - fec_unlock_started >= FEC_UNLOCK_RECOVERY_DELAY && now - last_recovery >= RECOVERY_COOLDOWN) {
                recover = true;
                fec_unlock_started_valid = false;
            }
        } else {
            fec_unlock_started_valid = false;
        }
    }

    if (changed) {
        notify_property_changes({"SignalStatus"});
    }

    if (recover) {
        recover_hardware("FEC synchronization lost");
    }
}

#if DAB_ENABLE_EWS
void TunerHandler::expire_emergency_warning() {
    bool changed = false;

    {
        const std::lock_guard lock(state_mutex);

        if (emergency_warning_seen_valid && emergency_warning_status.supported && std::chrono::steady_clock::now() - emergency_warning_seen >= EMERGENCY_WARNING_TIMEOUT) {
            emergency_warning_status = {};
            emergency_warning_seen_valid = false;
            changed = true;
        }
    }

    if (changed) {
        notify_property_changes({"EmergencyWarning"});
    }
}
#endif


void TunerHandler::date_time_data_event(const FICDecoder::DateTime &date_time) {
    {
        const std::lock_guard lock(state_mutex);
        this->date_time = date_time;
    }

#if DAB_ENABLE_SPI || DAB_ENABLE_SLIDESHOW || DAB_ENABLE_JOURNALINE
    using namespace std::chrono;
    const year_month_day day{year{date_time.year}, month{date_time.month}, std::chrono::day{date_time.day}};

    if (day.ok() && date_time.hour < 24 && date_time.minute < 60 && date_time.second < 60) {
        const sys_seconds timestamp = sys_days{day} + hours{date_time.hour} + minutes{date_time.minute} + seconds{date_time.second};
        const int64_t unix_time = timestamp.time_since_epoch().count();
#if DAB_ENABLE_SPI
        spi_decoder->expire(unix_time);
#endif
#if DAB_ENABLE_SLIDESHOW
        slideshow_decoder->update_time(unix_time);
#endif
#if DAB_ENABLE_JOURNALINE
        journaline_decoder->update_time(unix_time);
#endif
    }
#endif

    notify_property_changes({"DateTime"});
}

bool TunerHandler::recover_hardware(const std::string &reason) {
#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        return false;
    }
#endif

    uint32_t restore_frequency = 0;
    uint16_t restore_service_id = 0;
    FICDecoder::Service restore_service;
    bool restore_service_valid = false;
#if DAB_ENABLE_DATA_APPLICATIONS
    uint64_t restore_data_component_key = 0;
    uint16_t restore_data_application = 0;
    FICDecoder::ServiceComponent restore_data_component;
    FICDecoder::UserApplication restore_data_user_application;
    bool restore_data_component_valid = false;
#endif
#if DAB_ENABLE_SPI
    uint8_t spi_xpad_type = 0xFF;
#endif
#if DAB_ENABLE_JOURNALINE
    uint8_t journaline_xpad_type = 0xFF;
    std::vector<uint8_t> journaline_xpad_parameters;
#endif

    {
        const std::lock_guard lock(state_mutex);
        const auto now = std::chrono::steady_clock::now();

        if (!initialized || device_lost || now - last_recovery < RECOVERY_COOLDOWN) {
            return false;
        }

        restore_frequency = frequency;
        restore_service_id = current_service_id;
#if DAB_ENABLE_DATA_APPLICATIONS
        restore_data_component_key = current_data_component_key;
        restore_data_application = current_data_application;
#endif

        if (restore_service_id != 0) {
            const auto service = services.find(restore_service_id);

            if (service != services.end()) {
                restore_service = service->second;
                restore_service_valid = true;
            }

#if DAB_ENABLE_SPI || DAB_ENABLE_JOURNALINE
            if (restore_service_valid) {
                for (const auto &service_component: service_components | std::views::values) {
                    if (service_component.data_service || service_component.service_id != restore_service_id || service_component.subchannel != restore_service.subchannel.id) {
                        continue;
                    }

                    for (const auto &application: service_component.user_applications) {
                        if (!application.xpad || application.xpad_type == 0xFF) {
                            continue;
                        }

#if DAB_ENABLE_SPI
                        if (application.type == SPI_APPLICATION_TYPE) {
                            spi_xpad_type = application.xpad_type;
                            continue;
                        }
#endif
#if DAB_ENABLE_JOURNALINE
                        if (application.type == JOURNALINE_APPLICATION_TYPE) {
                            journaline_xpad_type = application.xpad_type;
                            journaline_xpad_parameters = application.parameters;
                        }
#endif
                    }
                }
            }
#endif
        }

#if DAB_ENABLE_DATA_APPLICATIONS
        if (restore_data_component_key != 0) {
            const auto component = service_components.find(restore_data_component_key);

            if (component != service_components.end()) {
                restore_data_component = component->second;

                for (const auto &application: restore_data_component.user_applications) {
                    if (application.type == restore_data_application && !application.xpad) {
                        restore_data_user_application = application;
                        restore_data_component_valid = true;
                        break;
                    }
                }
            }
        }
#endif

        last_recovery = now;
#if DAB_ENABLE_DIAGNOSTICS
        hardware_status.recovery_attempts++;
        hardware_status.recovery_reason = reason;
        hardware_status.recovering = true;
#endif
    }

#if DAB_ENABLE_DIAGNOSTICS
    notify_property_changes({"HardwareStatus"});
#endif
    logger.warning(std::format("Recovering tuner: {}", reason));
    bool success = false;
    bool stream_state_reset = false;
    bool audio_restored = false;
#if DAB_ENABLE_DATA_APPLICATIONS
    bool data_restored = false;
#endif

    {
        const std::lock_guard lock(tuner_mutex);
        const auto operation = helper->begin_operation(TunerHelper::RECOVERY_TIMEOUT);
#if DAB_ENABLE_AUDIO
        audio_mute.reset();
#endif

#if DAB_ENABLE_BACKGROUND_DATA
        stop_background_data();
#endif
        if (helper->power_up() && (restore_frequency == 0 || helper->set_frequency(restore_frequency))) {
#if DAB_ENABLE_AAC
            msc_decoder->reset();
#endif
#if DAB_ENABLE_MP2
            mp2_decoder->reset();
#endif
            pad_decoder->reset();
            dynamic_label_decoder->reset();
#if DAB_ENABLE_SLIDESHOW
            reset_slideshow();
#endif
            stream_state_reset = true;
#if DAB_ENABLE_AUDIO
            audio_pipe->close();
#endif
            audio_service_active = false;
            audio_codec = AudioCodec::None;
#if DAB_ENABLE_DATA_APPLICATIONS
            data_service_active = false;
            packet_decoder->reset();
#endif
#if DAB_ENABLE_SPI
            spi_mot_decoder->reset();
#endif

            if (restore_service_id != 0 && restore_service_valid) {
                const bool has_audio = restore_service.subchannel.bitrate > 0;
                bool codec_available = true;

#if !DAB_ENABLE_AAC
                if (has_audio && restore_service.dab_plus) {
                    codec_available = false;
                }
#endif
#if !DAB_ENABLE_MP2
                if (has_audio && !restore_service.dab_plus) {
                    codec_available = false;
                }
#endif

                if (codec_available) {
                    audio_service_active = has_audio;

                    if (audio_service_active) {
                        if (restore_service.dab_plus) {
#if DAB_ENABLE_AAC
                            msc_decoder->set_bitrate(restore_service.subchannel.bitrate);
                            audio_codec = AudioCodec::AAC;
#endif
                        } else {
#if DAB_ENABLE_MP2
                            mp2_decoder->set_bitrate(restore_service.subchannel.bitrate);
                            audio_codec = AudioCodec::MP2;
#endif
                        }
                    }

#if DAB_ENABLE_SPI
                    if (spi_xpad_type != 0xFF) {
                        spi_mot_decoder->configure_xpad(spi_xpad_type);
                    }
#endif
#if DAB_ENABLE_JOURNALINE
                    if (journaline_xpad_type != 0xFF) {
                        journaline_decoder->configure_xpad(journaline_xpad_type, journaline_xpad_parameters);
                    }
#endif

                    helper->open_subchannel(restore_service.subchannel.id);
#if DAB_ENABLE_SLIDESHOW
                    select_slideshow_service(restore_service_id);
#endif
                    audio_restored = true;
                }
            }
#if DAB_ENABLE_DATA_APPLICATIONS
            else if (restore_data_component_key != 0 && restore_data_component_valid && restore_data_component.subchannel != 0xFF && restore_data_component.packet_address != 0xFFFF) {
                bool application_ready = true;
#if DAB_ENABLE_JOURNALINE
                if (restore_data_application == JOURNALINE_APPLICATION_TYPE) {
                    application_ready = journaline_decoder->configure(restore_data_user_application.parameters);
                }
#endif

                if (application_ready) {
                    packet_decoder->configure(restore_data_component.packet_address, restore_data_component.data_groups, restore_data_component.fec_scheme);
                    data_service_active = true;
                    helper->open_subchannel(restore_data_component.subchannel);
                    data_restored = true;
                }
            }
#endif

            success = operation.succeeded() && helper->take_usb_error() == LIBUSB_SUCCESS;
        }
    }

    {
        const std::lock_guard lock(state_mutex);
        initialization_failed = !success;
        fec_unlock_started_valid = false;

        if (success) {
            signal_status = {};
            emitted_signal_status = {};
            emitted_signal_status_valid = false;
            signal_quality_filter.reset();
            signal_fic_received = fic_statistics.fibs_received;
            signal_fic_crc_errors = fic_statistics.fibs_crc_errors;

            if (restore_service_id != 0 && !audio_restored) {
                current_service_id = 0;
            }

#if DAB_ENABLE_DATA_APPLICATIONS
            if (restore_data_component_key != 0 && !data_restored) {
                current_data_component_key = 0;
                current_data_application = 0;
                pending_spi_component_key = 0;
            }
#endif
        } else if (stream_state_reset) {
            current_service_id = 0;
#if DAB_ENABLE_DATA_APPLICATIONS
            current_data_component_key = 0;
            current_data_application = 0;
            pending_spi_component_key = 0;
#endif
        }
    }

#if DAB_ENABLE_DATA_APPLICATIONS
    if (!success && stream_state_reset) {
        data_service_active = false;
    }
#endif

#if DAB_ENABLE_DIAGNOSTICS
    {
        const std::lock_guard lock(state_mutex);
        hardware_status.recovering = false;

        if (success) {
            hardware_status.recovery_successes++;
        }
    }

    notify_property_changes({"HardwareStatus"});
#endif

    if (success) {
        transient_usb_errors = 0;
        logger.info("Tuner recovery completed");
        notify_property_changes({"State", "SignalStatus", "AudioCodec", "DataApplication", "DataPathStatistics", "SlideShow", "SlideShowItems", "SlideShowStatistics", "DLS", "DLP", "JournalineRevision", "JournalineStatus", "TPEGRevision", "TPEGStatus"});
    } else {
        logger.error("Tuner recovery failed");

        if (stream_state_reset) {
            notify_property_changes({"State", "AudioCodec", "DataApplication", "DataPathStatistics", "SlideShow", "SlideShowItems", "SlideShowStatistics", "DLS", "DLP"});
        } else {
            notify_property_changes({"State"});
        }
    }

    return success;
}

void TunerHandler::handle_usb_error(const int error) {
    if (error == LIBUSB_ERROR_INTERRUPTED) {
        return;
    }

    if (error == LIBUSB_ERROR_NO_DEVICE) {
        {
            const std::lock_guard lock(state_mutex);
            device_lost = true;
            initialized = false;
            signal_status = {};
            emitted_signal_status = {};
            emitted_signal_status_valid = false;
            signal_quality_filter.reset();
            signal_fic_received = 0;
            signal_fic_crc_errors = 0;
            fec_unlock_started_valid = false;
        }

        running = false;
        audio_service_active = false;
        audio_codec = AudioCodec::None;
#if DAB_ENABLE_AUDIO
        audio_pipe->close();
#endif
        logger.warning("USB tuner disconnected");
        notify_property_changes({"State", "SignalStatus", "AudioCodec", "HardwareStatus"});
        return;
    }

    transient_usb_errors++;

    if (transient_usb_errors < USB_ERROR_RECOVERY_THRESHOLD) {
        return;
    }

    transient_usb_errors = 0;

#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        logger.warning(std::format("Repeated USB errors during scan: {}", libusb_error_name(error)));
        bool seeking = false;

        {
            const std::lock_guard lock(state_mutex);
            seeking = scan_mode == ScanMode::SeekUp || scan_mode == ScanMode::SeekDown;
        }

        if (seeking) {
            finish_scan(ScanOutcome::Failed);
        } else {
            scan_stop_requested = true;
        }

        return;
    }
#endif

    recover_hardware(std::format("repeated USB error ({})", libusb_error_name(error)));
}

#if DAB_ENABLE_SPI
void TunerHandler::queue_spi_service() {
#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        return;
    }
#endif

    const std::lock_guard lock(state_mutex);

#if DAB_ENABLE_SCAN
    if (scan_restore_service_id != 0) {
        return;
    }
#endif
    if (current_service_id != 0 || current_data_component_key != 0 || pending_spi_component_key != 0) {
        return;
    }

    for (const auto &[service_component_key, service_component]: service_components) {
        if (!is_packet_spi_component(service_component)) {
            continue;
        }

        pending_spi_component_key = service_component_key;
        return;
    }
}
#endif


#if DAB_ENABLE_SPI
void TunerHandler::try_select_pending_spi_service() {
#if DAB_ENABLE_SCAN
    if (scan_active.load()) {
        return;
    }
#endif

    FICDecoder::ServiceComponent service_component;

    {
        const std::lock_guard lock(state_mutex);

#if DAB_ENABLE_SCAN
        if (scan_restore_service_id != 0) {
            return;
        }
#endif
        if (pending_spi_component_key == 0 || current_service_id != 0 || current_data_component_key != 0) {
            return;
        }

        const auto component = service_components.find(pending_spi_component_key);

        if (component == service_components.end() || !is_packet_spi_component(component->second)) {
            pending_spi_component_key = 0;
            return;
        }

        service_component = component->second;
    }

    select_data_service(service_component.service_id, service_component.data_service, service_component.id, SPI_APPLICATION_TYPE, true);
}
#endif


#if DAB_ENABLE_BACKGROUND_DATA
void TunerHandler::update_background_data() {
    const std::lock_guard tuner_lock(tuner_mutex);
    FICDecoder::ServiceComponent component;
    uint16_t application_type = 0;
    bool manual = false;
    bool available = audio_service_active;
#if DAB_ENABLE_SCAN
    available = available && !scan_active.load();
#endif

    if (available) {
        const std::lock_guard state_lock(state_mutex);

        if (background_data_active && background_data_manual) {
            const auto current = service_components.find(get_service_component_key(background_data_component.service_id, background_data_component.data_service, background_data_component.id));

            if (current != service_components.end() && is_background_data_component(current->second, background_data_application)) {
                component = current->second;
                application_type = background_data_application;
                manual = true;
            }
        }

        static constexpr uint16_t APPLICATIONS[] = {
#if DAB_ENABLE_SPI
            SPI_APPLICATION_TYPE,
#endif
#if DAB_ENABLE_TPEG
            TPEG_APPLICATION_TYPE,
#endif
        };

        for (const uint16_t type: APPLICATIONS) {
#if DAB_ENABLE_TPEG
            if (type == TPEG_APPLICATION_TYPE && tpeg_suppressed) {
                continue;
            }
#endif
            if (application_type != 0) {
                break;
            }

            for (const auto &candidate: service_components | std::views::values) {
                if (is_background_data_component(candidate, type)) {
                    component = candidate;
                    application_type = type;
                    break;
                }
            }
        }
    }

    if (!available || application_type == 0) {
        stop_background_data();
        return;
    }

    select_background_data(component, application_type, manual);
}
#endif


#if DAB_ENABLE_BACKGROUND_DATA
bool TunerHandler::select_background_data(const FICDecoder::ServiceComponent &component, const uint16_t application_type, const bool manual) {
    if (!is_background_data_component(component, application_type)) {
        return false;
    }

    const auto operation = helper->begin_operation(TunerHelper::RECEIVE_TIMEOUT);

    if (background_data_active && application_type == background_data_application && component.service_id == background_data_component.service_id && component.data_service == background_data_component.data_service && component.id == background_data_component.id && component.subchannel == background_data_component.subchannel && component.packet_address == background_data_component.packet_address && component.fec_scheme == background_data_component.fec_scheme) {
        background_data_manual = manual;
        return true;
    }

    stop_background_data();
#if DAB_ENABLE_TPEG
    if (application_type == TPEG_APPLICATION_TYPE) {
        reset_tpeg();
    }
#endif
    background_packet_decoder->configure(component.packet_address, component.data_groups, component.fec_scheme);
    helper->open_background_subchannel(component.subchannel);

    if (!operation.succeeded()) {
        return false;
    }

    background_data_component = component;
    background_data_manual = manual;

    {
        const std::lock_guard state_lock(state_mutex);
        background_data_application = application_type;
        background_data_active = true;
#if DAB_ENABLE_TPEG
        if (application_type == TPEG_APPLICATION_TYPE) {
            tpeg_suppressed = false;
        }
#endif
    }

    logger.info(std::format("Background {} started: 0x{:X}/{} [packet {}, subchannel {}]", get_data_application_name(application_type), component.service_id, component.id, component.packet_address, component.subchannel));
    notify_property_changes({"DataApplication", "DataPathStatistics"});
    return true;
}
#endif


#if DAB_ENABLE_BACKGROUND_DATA
void TunerHandler::stop_background_data() {
    if (!background_data_active.exchange(false)) {
        return;
    }

    helper->close_background_subchannel();
    background_packet_decoder->reset();
#if DAB_ENABLE_SPI
    background_spi_mot_decoder->reset();
#endif
    background_data_component = {};
    background_data_manual = false;

    {
        const std::lock_guard state_lock(state_mutex);
        background_data_application = 0;
    }

    logger.debug("Background data reception stopped");
    notify_property_changes({"DataApplication", "DataPathStatistics"});
}
#endif


#if DAB_ENABLE_DATA_APPLICATIONS
void TunerHandler::reset_data_service(const bool preserve_spi) {
#if DAB_ENABLE_BACKGROUND_DATA
    if (!preserve_spi || !audio_service_active) {
        stop_background_data();
    }
#endif
    packet_decoder->reset();
    data_service_active = false;

    {
        const std::lock_guard lock(state_mutex);
        current_data_component_key = 0;
        current_data_application = 0;
        pending_spi_component_key = 0;
#if DAB_ENABLE_TPEG
        if (!preserve_spi) {
            tpeg_suppressed = false;
        }
#endif
#if DAB_ENABLE_SPI && DAB_ENABLE_DIAGNOSTICS
        spi_data_objects = 0;
        spi_data_updates = 0;
#endif
#if DAB_ENABLE_DIAGNOSTICS && DAB_ENABLE_PACKET
        data_path_statistics_emitted_valid = false;
#endif
    }

#if DAB_ENABLE_SPI
    if (preserve_spi) {
        spi_mot_decoder->reset();
    } else {
        reset_spi();
    }
#else
    (void)preserve_spi;
#endif
#if DAB_ENABLE_JOURNALINE
    reset_journaline();
#endif
#if DAB_ENABLE_TPEG
    if (!background_data_active || background_data_application != TPEG_APPLICATION_TYPE) {
        reset_tpeg();
    }
#endif
}
#endif


#if DAB_ENABLE_DIAGNOSTICS && DAB_ENABLE_PACKET
void TunerHandler::maybe_notify_data_path_statistics() {
    bool notify = false;

    {
        const std::lock_guard lock(state_mutex);
        const auto now = std::chrono::steady_clock::now();

        if (!data_path_statistics_emitted_valid || now - data_path_statistics_emitted >= DATA_PATH_STATISTICS_INTERVAL) {
            data_path_statistics_emitted = now;
            data_path_statistics_emitted_valid = true;
            notify = true;
        }
    }

    if (notify) {
        notify_property_changes({"DataPathStatistics"});
    }
}
#endif


#if DAB_ENABLE_SPI
void TunerHandler::reset_spi() {
    spi_mot_decoder->reset();
    bool changed = false;

    {
        const std::lock_guard lock(state_mutex);
        changed = !spi_service_information.empty() || !spi_programmes.empty() || !spi_programme_groups.empty() || !spi_assets.empty() || !spi_objects.empty();
        spi_service_information.clear();
        spi_programmes.clear();
        spi_programme_groups.clear();
        spi_assets.clear();
        spi_objects.clear();
#if DAB_ENABLE_LOGO_CACHE
        changed = changed || !cached_service_logos.empty();
        cached_service_logos.clear();
#endif

        if (changed) {
            spi_revision++;
        }
    }

    spi_decoder->reset();
    spi_file->clear();
}
#endif


#if DAB_ENABLE_SPI && DAB_ENABLE_LOGO_CACHE
void TunerHandler::load_cached_service_logos() {
    std::set<uint8_t> countries;
    uint16_t ensemble_id = 0;

    {
        const std::lock_guard lock(state_mutex);
        countries.insert(ensemble_status.ecc);
        ensemble_id = ensemble_status.ensemble_id;

        for (const auto &service: services | std::views::values) {
            countries.insert(service.ecc);
        }
    }

    std::vector<LogoCache::Logo> logos;

    for (const uint8_t ecc: countries) {
        auto cached = logo_cache->load(ecc, ensemble_id);
        logos.insert(logos.end(), std::make_move_iterator(cached.begin()), std::make_move_iterator(cached.end()));
    }

    bool changed = false;

    {
        const std::lock_guard lock(state_mutex);
        changed = cached_service_logos != logos;
        cached_service_logos = std::move(logos);

        if (changed) {
            spi_revision++;
        }
    }

    if (changed) {
        notify_property_changes({"SPIRevision"});
    }
}
#endif


#if DAB_ENABLE_SPI && DAB_ENABLE_LOGO_CACHE
void TunerHandler::cache_service_logos(const std::string &content_name) {
    std::vector<SPIAsset> assets;
    std::vector<SPIDecoder::ServiceInformation> information;
    std::vector<FICDecoder::Service> audio_services;
    uint8_t ecc = 0;
    uint16_t ensemble_id = 0;

    {
        const std::lock_guard lock(state_mutex);
        ecc = ensemble_status.ecc;
        ensemble_id = ensemble_status.ensemble_id;

        if (ecc == 0 || ensemble_id == 0) {
            return;
        }

        information = spi_service_information;

        for (const auto &asset: spi_assets | std::views::values) {
            if (content_name.empty() || asset.content_name == content_name) {
                assets.push_back(asset);
            }
        }

        for (const auto &service: services | std::views::values) {
            audio_services.push_back(service);
        }
    }

    bool changed = false;

    for (const auto &asset: assets) {
        std::vector<std::tuple<uint32_t, uint8_t, uint16_t>> identities;

        for (const auto &service: information) {
            if (service.data_service) {
                continue;
            }

            for (const auto &image: service.multimedia) {
                const bool square = image.type == "logo_colour_square" || (image.type == "logo_unrestricted" && image.width != 0 && image.width == image.height);

                if (square && image.url == asset.content_name && !image.url.empty()) {
                    identities.emplace_back(service.service_id, service.ecc == 0 ? ecc : service.ecc, service.ensemble_id == 0 ? ensemble_id : service.ensemble_id);
                    break;
                }
            }
        }

        if (identities.empty()) {
            for (const auto &service: audio_services) {
                if (LogoHelper::matches_service(asset.content_name, service.id, service.label, service.short_label)) {
                    identities.emplace_back(service.id, service.ecc == 0 ? ecc : service.ecc, ensemble_id);
                }
            }
        }

        for (const auto &[service_id, service_ecc, service_ensemble_id]: identities) {
            const auto cached = logo_cache->store(service_id, service_ecc, service_ensemble_id, asset.path);

            if (!cached || service_ensemble_id != ensemble_id) {
                continue;
            }

            const std::lock_guard lock(state_mutex);
            const auto existing = std::ranges::find_if(cached_service_logos, [&](const LogoCache::Logo &logo) {
                return logo.service_id == service_id && logo.ecc == service_ecc;
            });

            if (existing == cached_service_logos.end()) {
                cached_service_logos.push_back(*cached);
                changed = true;
            } else if (*existing != *cached) {
                *existing = *cached;
                changed = true;
            }
        }
    }

    if (changed) {
        {
            const std::lock_guard lock(state_mutex);
            spi_revision++;
        }

        notify_property_changes({"SPIRevision"});
    }
}
#endif


#if DAB_ENABLE_JOURNALINE
void TunerHandler::reset_journaline() {
    journaline_decoder->reset();
}
#endif


#if DAB_ENABLE_TPEG
void TunerHandler::reset_tpeg() {
    tpeg_decoder->reset();

    const std::lock_guard lock(state_mutex);
    tpeg_frame_dirty = false;
    tpeg_content_decoder.reset();

    if (!tpeg_frames.empty()) {
        tpeg_frames.clear();
        tpeg_revision++;
    }
}
#endif
