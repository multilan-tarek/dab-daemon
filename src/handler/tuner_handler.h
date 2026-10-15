#ifndef TUNER_HANDLER_H
#define TUNER_HANDLER_H

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <initializer_list>
#include <libusb-1.0/libusb.h>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "../config/features.h"
#include "../decoder/dynamic_label_decoder.h"
#include "../decoder/fic_decoder.h"
#include "../decoder/pad_decoder.h"
#if DAB_ENABLE_AAC
#include "../decoder/msc_decoder.h"
#endif
#if DAB_ENABLE_MP2
#include "../decoder/mp2_decoder.h"
#endif
#if DAB_ENABLE_PACKET
#include "../decoder/packet_decoder.h"
#endif
#if DAB_ENABLE_SPI
#include "../decoder/mot_decoder.h"
#include "../decoder/spi_decoder.h"
#include "../helper/spi_file.h"
#endif
#if DAB_ENABLE_SPI && DAB_ENABLE_LOGO_CACHE
#include "../helper/logo_cache.h"
#endif
#if DAB_ENABLE_JOURNALINE
#include "../decoder/journaline_decoder.h"
#endif
#if DAB_ENABLE_TPEG
#include "../decoder/tpeg_decoder.h"
#endif
#if DAB_ENABLE_SLIDESHOW
#include "../decoder/slideshow_decoder.h"
#include "../helper/slideshow_file.h"
#endif
#if DAB_ENABLE_AUDIO
#include "../helper/audio_pipe.h"
#endif
#include "../helper/dab_channels.h"
#include "../helper/signal_quality_filter.h"
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

#if DAB_ENABLE_SPI
    struct SPIObject {
        uint16_t transport_id = 0;
        uint32_t body_size = 0;
        uint8_t content_type = 0;
        uint16_t content_subtype = 0;
        std::string content_name;
        std::string mime_type;
        int compression_type = -1;
        std::vector<uint8_t> profile_subset;
        std::string decode_status;

        bool operator==(const SPIObject &other) const = default;
    };
#endif


#if DAB_ENABLE_SPI
    struct ServiceLogo {
        uint32_t service_id = 0;
        uint8_t ecc = 0;
        uint16_t ensemble_id = 0;
        std::vector<SPIDecoder::Multimedia> multimedia;
    };

    struct SPIAsset {
        uint16_t transport_id = 0;
        std::string content_name;
        std::string path;
        std::string mime_type;

        bool operator==(const SPIAsset &other) const = default;
    };
#endif


#if DAB_ENABLE_SLIDESHOW
    struct SlideShowItem {
        uint16_t transport_id = 0;
        std::string content_name;
        std::string path;
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
        bool active = false;

        bool operator==(const SlideShowItem &other) const = default;
    };
#endif


    struct SignalStatus {
        bool ofdm_lock = false;
        bool fec_lock = false;
        bool fec_sync = false;
        bool signal_quality_valid = false;
        uint8_t signal_quality = 0;
        uint8_t antenna_level = 0;
        bool msc_cer_valid = false;
        uint32_t msc_cer = 0;
        bool fic_crc_error_rate_valid = false;
        uint32_t fic_crc_error_rate = 0;

        bool operator==(const SignalStatus &other) const = default;
    };

#if DAB_ENABLE_DIAGNOSTICS
    struct HardwareStatus {
        TunerHelper::USBStatus usb;
        int64_t last_data_time = 0;
        uint64_t recovery_attempts = 0;
        uint64_t recovery_successes = 0;
        std::string recovery_reason;
        bool recovering = false;
    };

    struct DataPathStatistics {
        uint64_t msc_bytes = 0;
        uint64_t packets_valid = 0;
        uint64_t packet_crc_errors = 0;
        uint64_t packets_addressed = 0;
        uint64_t data_groups = 0;
        uint64_t packet_continuity_errors = 0;
        uint64_t fec_frames = 0;
        uint64_t fec_errors = 0;
        uint64_t mot_groups_received = 0;
        uint64_t mot_groups_accepted = 0;
        uint64_t mot_crc_errors = 0;
        uint64_t mot_segments = 0;
        uint64_t mot_directories = 0;
        uint64_t mot_objects = 0;
        uint64_t spi_objects = 0;
        uint64_t spi_updates = 0;
    };
#endif


#if DAB_ENABLE_SCAN
    struct ScanProgress {
        uint16_t current = 0;
        uint16_t total = 0;
        std::string channel;
        uint32_t frequency = 0;

        bool operator==(const ScanProgress &other) const = default;
    };
#endif


#if DAB_ENABLE_SCAN
    struct ScanResult {
        std::string channel;
        uint32_t frequency = 0;
        uint16_t ensemble_id = 0;
        uint8_t ecc = 0;
        std::string label;
        std::string short_label;
        std::vector<FICDecoder::Service> services;

        bool operator==(const ScanResult &other) const = default;
    };
#endif


    using PropertyChangeCallback = std::function<void(const std::vector<std::string> &)>;

    TunerHandler(int id, libusb_device_handle *device_handle);

    ~TunerHandler();

    void stop();

    std::string get_state() const;

    uint32_t get_frequency() const;

    FICDecoder::DateTime get_date_time() const;

    FICDecoder::EnsembleStatus get_ensemble_status() const;

#if DAB_ENABLE_DIAGNOSTICS
    FICDecoder::Statistics get_fic_statistics() const;

    HardwareStatus get_hardware_status() const;

    uint64_t get_fic_revision() const;

    FICDecoder::FigureHistory get_fic_figures(uint64_t since_sequence, uint16_t limit) const;
#endif

    SignalStatus get_signal_status() const;

#if DAB_ENABLE_FIDC
    std::vector<FICDecoder::FIDCEntry> get_fidc_entries() const;
#endif

#if DAB_ENABLE_EWS
    FICDecoder::EmergencyWarningStatus get_emergency_warning_status() const;
#endif

    FICDecoder::Service get_service() const;

#if DAB_ENABLE_AUDIO
    std::string get_audio_pipe() const;

    std::string get_audio_codec() const;
#if DAB_ENABLE_DIAGNOSTICS
    AudioStatistics get_audio_statistics() const;
#endif
#endif

#if DAB_ENABLE_SLIDESHOW
    std::string get_slideshow() const;

    std::vector<SlideShowItem> get_slideshow_items() const;
#endif

#if DAB_ENABLE_SLIDESHOW && DAB_ENABLE_DIAGNOSTICS
    SlideShowDecoder::Statistics get_slideshow_statistics() const;
#endif

    std::string get_dls() const;

    DynamicLabelPlus get_dlp() const;

    std::vector<std::string> get_ensemble() const;

    std::vector<FICDecoder::Service> get_services() const;

    std::vector<FICDecoder::ServiceComponent> get_service_components() const;

#if DAB_ENABLE_SERVICE_LINKING
    std::vector<FICDecoder::ServiceLink> get_service_links() const;
#endif

#if DAB_ENABLE_FREQUENCY_INFO
    std::vector<FICDecoder::FrequencyInformation> get_frequency_information() const;
#endif

#if DAB_ENABLE_SERVICE_LINKING
    std::vector<FICDecoder::OtherEnsembleService> get_other_ensemble_services() const;
#endif

#if DAB_ENABLE_ANNOUNCEMENTS
    std::vector<FICDecoder::OEAnnouncementSupport> get_oe_announcement_support() const;

    std::vector<FICDecoder::OEAnnouncementSwitch> get_oe_announcement_switches() const;

    std::vector<FICDecoder::FMAnnouncementSupport> get_fm_announcement_support() const;

    std::vector<FICDecoder::FMAnnouncementSwitch> get_fm_announcement_switches() const;
#endif

#if DAB_ENABLE_SPI
    uint64_t get_spi_revision() const;

    std::vector<SPIDecoder::ServiceInformation> get_service_information(uint32_t service_id, bool data_service, uint8_t component_id) const;

    std::vector<SPIDecoder::Programme> get_programme(uint32_t service_id, bool data_service, uint8_t component_id, int64_t timestamp) const;

    std::vector<SPIDecoder::Programme> get_schedule(uint32_t service_id, bool data_service, uint8_t component_id, int64_t from, int64_t to) const;

    std::vector<SPIDecoder::ProgrammeGroup> get_programme_groups() const;

    std::vector<SPIAsset> get_spi_assets() const;

    std::vector<ServiceLogo> get_service_logos() const;

    std::map<uint32_t, std::string> get_logo_paths() const;

    std::vector<SPIObject> get_spi_objects() const;
#endif

#if DAB_ENABLE_DATA_APPLICATIONS
    std::string get_data_application() const;
#endif

#if DAB_ENABLE_DIAGNOSTICS && DAB_ENABLE_PACKET
    DataPathStatistics get_data_path_statistics() const;
#endif

#if DAB_ENABLE_JOURNALINE
    uint64_t get_journaline_revision() const;

    JournalineDecoder::Status get_journaline_status() const;

    std::vector<JournalineDecoder::IndexEntry> get_journaline_index() const;

    std::vector<JournalineDecoder::Object> get_journaline_object(uint16_t object_id) const;
#endif

#if DAB_ENABLE_TPEG
    bool get_tpeg_receiving() const;

    bool stop_tpeg();

    uint64_t get_tpeg_revision() const;

    TPEGDecoder::Status get_tpeg_status() const;

    std::vector<TPEGDecoder::ServiceIdentifier> get_tpeg_services() const;

    std::vector<TPEGDecoder::Frame> get_tpeg_frames(uint64_t since_sequence, uint16_t limit) const;
#endif

    std::vector<DABChannel> get_band_iii_channels() const;

#if DAB_ENABLE_SCAN
    std::string get_scan_state() const;

    std::string get_scan_mode() const;

    ScanProgress get_scan_progress() const;

    std::vector<ScanResult> get_scan_results() const;

    bool start_scan(const std::string &mode);

    bool stop_scan();
#endif

    bool is_ready() const;

    bool set_frequency(uint32_t frequency);

    bool set_ensemble(const std::string &ensemble);

    bool set_service(uint16_t service_id);

#if DAB_ENABLE_DATA_APPLICATIONS
    bool set_data_service(uint32_t service_id, bool data_service, uint8_t component_id, uint16_t application_type);
#endif

    bool close_service();

    void open_subchannel(uint8_t subchannel);

    void set_property_change_callback(const PropertyChangeCallback &callback);

private:
    enum class AudioCodec {
        None,
        AAC,
        MP2
    };

#if DAB_ENABLE_SCAN
    enum class ScanMode {
        None,
        Fast,
        Full
    };

    enum class ScanPhase {
        Idle,
        Tune,
        Receive
    };
#endif

    libusb_device_handle *device_handle = nullptr;
    TunerHelper *helper = nullptr;
    FICDecoder *fic_decoder = nullptr;
#if DAB_ENABLE_AAC
    MSCDecoder *msc_decoder = nullptr;
#endif
#if DAB_ENABLE_MP2
    MP2Decoder *mp2_decoder = nullptr;
#endif
    PADDecoder *pad_decoder = nullptr;
    DynamicLabelDecoder *dynamic_label_decoder = nullptr;
#if DAB_ENABLE_SLIDESHOW
    SlideShowDecoder *slideshow_decoder = nullptr;
#endif
#if DAB_ENABLE_PACKET
    PacketDecoder *packet_decoder = nullptr;
#endif
#if DAB_ENABLE_SPI
    MOTDecoder *spi_mot_decoder = nullptr;
    MOTDecoder *background_spi_mot_decoder = nullptr;
    SPIDecoder *spi_decoder = nullptr;
#endif
#if DAB_ENABLE_BACKGROUND_DATA
    PacketDecoder *background_packet_decoder = nullptr;
    std::atomic_bool background_data_active = false;
    FICDecoder::ServiceComponent background_data_component;
    uint16_t background_data_application = 0;
    bool background_data_manual = false;
#endif
#if DAB_ENABLE_JOURNALINE
    JournalineDecoder *journaline_decoder = nullptr;
#endif
#if DAB_ENABLE_TPEG
    TPEGDecoder *tpeg_decoder = nullptr;
#endif
#if DAB_ENABLE_AUDIO
    AudioPipe *audio_pipe = nullptr;
#endif
#if DAB_ENABLE_SLIDESHOW
    SlideShowFile *slideshow_file = nullptr;
#endif
#if DAB_ENABLE_SPI
    SPIFile *spi_file = nullptr;
#endif
#if DAB_ENABLE_SPI && DAB_ENABLE_LOGO_CACHE
    LogoCache *logo_cache = nullptr;
    std::vector<LogoCache::Logo> cached_service_logos;
#endif

    Logger logger{"TUNER"};
    Logger fic_logger{"FIC"};
    Logger msc_logger{"MSC"};
    Logger pad_logger{"PAD"};
#if DAB_ENABLE_SPI
    Logger spi_logger{"SPI"};
#endif
#if DAB_ENABLE_JOURNALINE
    Logger journaline_logger{"JOURNALINE"};
#endif
#if DAB_ENABLE_TPEG
    Logger tpeg_logger{"TPEG"};
#endif

    std::atomic_bool running = false;
#if DAB_ENABLE_SCAN
    std::atomic_bool scan_active = false;
    std::atomic_bool scan_stop_requested = false;
#endif
    std::thread read_thread;
    std::mutex tuner_mutex;
    mutable std::mutex state_mutex;
    mutable std::mutex property_change_mutex;
    PropertyChangeCallback property_change_callback = nullptr;

    bool initialized = false;
    bool initialization_failed = false;
    bool device_lost = false;
    uint32_t frequency = 0;
    FICDecoder::DateTime date_time;
    FICDecoder::EnsembleStatus ensemble_status;
    FICDecoder::Statistics fic_statistics;
#if DAB_ENABLE_DIAGNOSTICS
    HardwareStatus hardware_status;
#endif
    SignalStatus signal_status;
    SignalStatus emitted_signal_status;
    SignalQualityFilter signal_quality_filter;
    bool emitted_signal_status_valid = false;
    uint32_t signal_fic_received = 0;
    uint32_t signal_fic_crc_errors = 0;
#if DAB_ENABLE_FIDC
    std::vector<FICDecoder::FIDCEntry> fidc_entries;
#endif
#if DAB_ENABLE_EWS
    FICDecoder::EmergencyWarningStatus emergency_warning_status;
    uint64_t emergency_warning_sequence = 0;
    std::chrono::steady_clock::time_point emergency_warning_seen;
    bool emergency_warning_seen_valid = false;
#endif

    std::string ensemble;
    std::string ensemble_label;
    std::string ensemble_short_label;
    std::map<uint16_t, FICDecoder::Service> services;
    std::map<uint64_t, FICDecoder::ServiceComponent> service_components;
#if DAB_ENABLE_SERVICE_LINKING
    std::vector<FICDecoder::ServiceLink> service_links;
    std::vector<FICDecoder::OtherEnsembleService> other_ensemble_services;
#endif
#if DAB_ENABLE_FREQUENCY_INFO
    std::vector<FICDecoder::FrequencyInformation> frequency_information;
#endif
#if DAB_ENABLE_ANNOUNCEMENTS
    std::vector<FICDecoder::OEAnnouncementSupport> oe_announcement_support;
    std::vector<FICDecoder::OEAnnouncementSwitch> oe_announcement_switches;
    std::vector<FICDecoder::FMAnnouncementSupport> fm_announcement_support;
    std::vector<FICDecoder::FMAnnouncementSwitch> fm_announcement_switches;
#endif

    uint16_t current_service_id = 0;
    uint64_t current_data_component_key = 0;
    uint16_t current_data_application = 0;
    uint64_t pending_spi_component_key = 0;
    DynamicLabelDecoder::AudioText audio_text;
#if DAB_ENABLE_SPI
    std::vector<SPIDecoder::ServiceInformation> spi_service_information;
    std::vector<SPIDecoder::Programme> spi_programmes;
    std::vector<SPIDecoder::ProgrammeGroup> spi_programme_groups;
    uint64_t spi_revision = 0;
    std::map<uint16_t, SPIAsset> spi_assets;
    std::map<uint16_t, SPIObject> spi_objects;
#if DAB_ENABLE_DIAGNOSTICS
    uint64_t spi_data_objects = 0;
    uint64_t spi_data_updates = 0;
#endif
#endif
#if DAB_ENABLE_JOURNALINE
    JournalineDecoder::Status journaline_status;
    std::vector<JournalineDecoder::Object> journaline_objects;
    uint64_t journaline_revision = 0;
#endif
#if DAB_ENABLE_TPEG
    TPEGDecoder::Status tpeg_status;
    bool tpeg_suppressed = false;
    std::vector<TPEGDecoder::ServiceIdentifier> tpeg_services;
    std::vector<TPEGDecoder::Frame> tpeg_frames;
    uint64_t tpeg_revision = 0;
    bool tpeg_frame_dirty = false;
#endif
#if DAB_ENABLE_DIAGNOSTICS && DAB_ENABLE_PACKET
    std::chrono::steady_clock::time_point data_path_statistics_emitted;
    bool data_path_statistics_emitted_valid = false;
#endif
#if DAB_ENABLE_SLIDESHOW
    std::map<std::string, SlideShowItem> slideshow_items;
    std::string current_slideshow_content_name;
    std::string current_slideshow_path;
#if DAB_ENABLE_DIAGNOSTICS
    SlideShowDecoder::Statistics slideshow_statistics;
    std::chrono::steady_clock::time_point slideshow_statistics_emitted;
    bool slideshow_statistics_emitted_valid = false;
#endif
#endif
#if DAB_ENABLE_SCAN
    ScanMode scan_mode = ScanMode::None;
    ScanPhase scan_phase = ScanPhase::Idle;
    std::string scan_state = "Idle";
    ScanProgress scan_progress;
    std::vector<ScanResult> scan_results;
    size_t scan_channel_index = 0;
    uint32_t scan_saved_frequency = 0;
    uint16_t scan_saved_service_id = 0;
    uint16_t scan_restore_service_id = 0;
    std::chrono::steady_clock::time_point scan_frequency_started;
    std::chrono::steady_clock::time_point scan_ensemble_seen;
    std::chrono::steady_clock::time_point scan_last_service_change;
    std::chrono::steady_clock::time_point scan_restore_deadline;
    bool scan_ensemble_seen_valid = false;
#endif

    bool fic_data_received = false;
    bool msc_data_received = false;
    bool audio_service_active = false;
    std::atomic<AudioCodec> audio_codec = AudioCodec::None;
#if DAB_ENABLE_AUDIO && DAB_ENABLE_DIAGNOSTICS
    AudioStatistics audio_statistics;
    AudioCodec audio_statistics_codec = AudioCodec::None;
    uint32_t audio_statistics_frequency = 0;
    uint16_t audio_statistics_service_id = 0;

    void update_audio_statistics();
#endif
    bool data_service_active = false;
    unsigned transient_usb_errors = 0;
    std::chrono::steady_clock::time_point fec_unlock_started;
    std::chrono::steady_clock::time_point last_recovery;
    bool fec_unlock_started_valid = false;

    bool tune_frequency(uint32_t frequency, bool notify_changes);

#if DAB_ENABLE_SCAN
    void scan_step();

    void finish_scan(bool cancelled);

    void capture_scan_result();

    void try_restore_scan_service();
#endif

    void handle();

#if DAB_ENABLE_DIAGNOSTICS
    void record_data_received();
#endif

    void fic_data_event(const std::vector<uint8_t> &data);

    void msc_data_event(const std::vector<uint8_t> &data);

#if DAB_ENABLE_AAC
    void access_unit_event(const MSCDecoder::AccessUnit &access_unit) const;
#endif

#if DAB_ENABLE_MP2
    void mp2_frame_event(const MP2Decoder::Frame &frame) const;
#endif

    void pad_data_event(const std::vector<uint8_t> &xpad_data, const std::array<uint8_t, 2> &fpad_data);

    void dynamic_label_data_event(bool start, const std::vector<uint8_t> &data) const;

    void mot_data_event(int type, const std::vector<uint8_t> &data) const;

#if DAB_ENABLE_PACKET
    void packet_data_event(const std::vector<uint8_t> &data) const;
#endif

#if DAB_ENABLE_BACKGROUND_DATA
    void background_data_event(const std::vector<uint8_t> &data);

    void background_packet_event(const std::vector<uint8_t> &data);
#endif

#if DAB_ENABLE_SPI
    void spi_object_event(const MOTDecoder::Object &object);

    void spi_object_removed_event(uint16_t transport_id);

    void spi_update_event(const std::vector<SPIDecoder::ServiceInformation> &service_information, const std::vector<SPIDecoder::Programme> &programmes, const std::vector<SPIDecoder::ProgrammeGroup> &programme_groups);
#endif

#if DAB_ENABLE_JOURNALINE
    void journaline_update_event(const JournalineDecoder::Status &status, const std::vector<JournalineDecoder::Object> &objects);
#endif

#if DAB_ENABLE_TPEG
    void tpeg_update_event(const TPEGDecoder::Status &status, const std::vector<TPEGDecoder::ServiceIdentifier> &services);

    void tpeg_frame_event(const TPEGDecoder::Frame &frame);
#endif

#if DAB_ENABLE_SLIDESHOW
    void slideshow_object_event(const SlideShowDecoder::Slide &slide);

    void slideshow_display_event(const SlideShowDecoder::Slide &slide);

    void slideshow_removed_event(const std::string &content_name);
#endif

    void audio_text_event(const DynamicLabelDecoder::AudioText &audio_text);

    void ensemble_data_event(const FICDecoder::Ensemble &ensemble);

    void ensemble_status_data_event(const FICDecoder::EnsembleStatus &ensemble_status);

    void service_data_event(const FICDecoder::Service &service);

    void service_removed_event(uint16_t service_id);

    void service_component_data_event(const FICDecoder::ServiceComponent &service_component);

    void service_component_removed_event(uint64_t service_component_key);

    void date_time_data_event(const FICDecoder::DateTime &date_time);

    void signal_status_event(const TunerHelper::SignalStatus &signal_status);

#if DAB_ENABLE_EWS
    void expire_emergency_warning();
#endif

    bool recover_hardware(const std::string &reason);

    void handle_usb_error(int error);

#if DAB_ENABLE_SLIDESHOW
    void reset_slideshow();

    void select_slideshow_service(uint16_t service_id);
#endif

#if DAB_ENABLE_SLIDESHOW && DAB_ENABLE_DIAGNOSTICS
    void maybe_notify_slideshow_statistics();
#endif

    void notify_property_changes(std::initializer_list<std::string_view> properties) const;

#if DAB_ENABLE_DATA_APPLICATIONS
    bool select_data_service(uint32_t service_id, bool data_service, uint8_t component_id, uint16_t application_type, bool automatic);
#endif

#if DAB_ENABLE_SPI
    void queue_spi_service();

    void try_select_pending_spi_service();
#endif

#if DAB_ENABLE_BACKGROUND_DATA
    void update_background_data();

    void stop_background_data();

    bool select_background_data(const FICDecoder::ServiceComponent &component, uint16_t application_type, bool manual);
#endif

#if DAB_ENABLE_SPI && DAB_ENABLE_LOGO_CACHE
    void load_cached_service_logos();

    void cache_service_logos(const std::string &content_name = "");
#endif

#if DAB_ENABLE_DATA_APPLICATIONS
    void reset_data_service(bool preserve_spi = false);
#endif

#if DAB_ENABLE_DIAGNOSTICS && DAB_ENABLE_PACKET
    void maybe_notify_data_path_statistics();
#endif

#if DAB_ENABLE_SPI
    void reset_spi();
#endif

#if DAB_ENABLE_JOURNALINE
    void reset_journaline();
#endif

#if DAB_ENABLE_TPEG
    void reset_tpeg();
#endif
};

#endif //TUNER_HANDLER_H
