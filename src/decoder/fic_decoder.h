#ifndef FIC_DECODER_H
#define FIC_DECODER_H

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../config/features.h"
#if DAB_ENABLE_DIAGNOSTICS
#include <deque>
#include <mutex>
#include <utility>
#endif

class FICDecoder {
public:
    struct Ensemble {
        uint16_t id = 0;
        std::string label;
        std::string short_label;

        bool operator==(const Ensemble &other) const {
            return id == other.id && label == other.label && short_label == other.short_label;
        }

        bool operator!=(const Ensemble &other) const {
            return !(*this == other);
        }
    };

    struct EnsembleStatus {
        uint16_t ensemble_id = 0;
        uint8_t service_count = 0;
        uint8_t services_found = 0;
        uint8_t change_flags = 0;
        bool alarm_supported = false;
        bool alarm_active = false;
        uint8_t occurrence_change = 0;
        uint16_t reconfiguration_counter = 0;
        uint16_t cif_counter = 0;
        uint8_t ecc = 0;
        int16_t local_time_offset = 0;
        bool lto_unique = false;
        uint8_t international_table_id = 0;

        bool operator==(const EnsembleStatus &other) const {
            return ensemble_id == other.ensemble_id && service_count == other.service_count && services_found == other.services_found && change_flags == other.change_flags && alarm_supported == other.alarm_supported && alarm_active == other.alarm_active && occurrence_change == other.occurrence_change && reconfiguration_counter == other.reconfiguration_counter && cif_counter == other.cif_counter && ecc == other.ecc && local_time_offset == other.local_time_offset && lto_unique == other.lto_unique && international_table_id == other.international_table_id;
        }

        bool operator!=(const EnsembleStatus &other) const {
            return !(*this == other);
        }
    };

    struct DateTime {
        uint16_t year = 0;
        uint8_t month = 0;
        uint8_t day = 0;
        uint8_t hour = 0;
        uint8_t minute = 0;
        uint8_t second = 0;
        uint16_t milliseconds = 0;

        bool operator==(const DateTime &other) const {
            return year == other.year && month == other.month && day == other.day && hour == other.hour && minute == other.minute && second == other.second && milliseconds == other.milliseconds;
        }

        bool operator!=(const DateTime &other) const {
            return !(*this == other);
        }
    };

    struct Subchannel {
        uint8_t id = 0;
        uint16_t start = 0;
        uint16_t size = 0;
        uint16_t bitrate = 0;
        std::string protection;

        bool operator==(const Subchannel &other) const {
            return id == other.id && start == other.start && size == other.size && bitrate == other.bitrate && protection == other.protection;
        }

        bool operator!=(const Subchannel &other) const {
            return !(*this == other);
        }
    };

    struct Service {
        uint16_t id = 0;
        std::string label;
        std::string short_label;
        uint8_t ecc = 0;
        Subchannel subchannel;
        bool dab_plus = false;
        std::string language;
        std::string pty;
        std::vector<std::string> announcement_support;
        std::vector<std::string> announcements;

        bool operator==(const Service &other) const {
            return id == other.id && label == other.label && short_label == other.short_label && ecc == other.ecc && subchannel == other.subchannel && dab_plus == other.dab_plus && language == other.language && pty == other.pty && announcement_support == other.announcement_support && announcements == other.announcements;
        }

        bool operator!=(const Service &other) const {
            return !(*this == other);
        }
    };

    struct UserApplication {
        uint16_t type = 0;
        std::string name;
        std::vector<uint8_t> data;
        bool xpad = false;
        uint8_t xpad_type = 0xFF;
        bool conditional_access = false;
        bool conditional_access_organization_present = false;
        uint16_t conditional_access_organization = 0;
        bool data_groups = false;
        uint8_t data_type = 0;
        std::vector<uint8_t> parameters;

        bool operator==(const UserApplication &other) const {
            return type == other.type && name == other.name && data == other.data && xpad == other.xpad && xpad_type == other.xpad_type && conditional_access == other.conditional_access && conditional_access_organization_present == other.conditional_access_organization_present && conditional_access_organization == other.conditional_access_organization && data_groups == other.data_groups && data_type == other.data_type && parameters == other.parameters;
        }

        bool operator!=(const UserApplication &other) const {
            return !(*this == other);
        }
    };

    struct ServiceComponent {
        uint32_t service_id = 0;
        bool data_service = false;
        uint8_t id = 0;
        std::string label;
        std::string short_label;
        uint8_t subchannel = 0xFF;
        uint16_t component_id = 0xFFFF;
        bool data = false;
        uint8_t type = 0;
        bool conditional_access = false;
        uint16_t conditional_access_organization = 0;
        uint8_t fec_scheme = 0;
        std::string language;
        bool part_time = false;
        uint8_t change_flags = 0;
        uint8_t change_day = 0x1F;
        uint8_t change_hour = 0x1F;
        uint8_t change_minute = 0x3F;
        uint8_t change_second = 0x3F;
        uint32_t transfer_service_id = 0;
        uint16_t transfer_ensemble_id = 0;
        std::vector<std::string> applications;
        std::vector<std::string> application_labels;
        uint16_t packet_address = 0xFFFF;
        bool data_groups = false;
        bool conditional_access_organization_present = false;
        std::vector<UserApplication> user_applications;

        bool operator==(const ServiceComponent &other) const {
            return service_id == other.service_id && data_service == other.data_service && id == other.id && label == other.label && short_label == other.short_label && subchannel == other.subchannel && component_id == other.component_id && data == other.data && type == other.type && conditional_access == other.conditional_access && conditional_access_organization == other.conditional_access_organization && fec_scheme == other.fec_scheme && language == other.language && part_time == other.part_time && change_flags == other.change_flags && change_day == other.change_day && change_hour == other.change_hour && change_minute == other.change_minute && change_second == other.change_second && transfer_service_id == other.transfer_service_id && transfer_ensemble_id == other.transfer_ensemble_id && applications == other.applications && application_labels == other.application_labels && packet_address == other.packet_address && data_groups == other.data_groups && conditional_access_organization_present == other.conditional_access_organization_present && user_applications == other.user_applications;
        }

        bool operator!=(const ServiceComponent &other) const {
            return !(*this == other);
        }
    };

    struct ServiceLink {
        uint16_t linkage_set = 0;
        bool active = false;
        bool hard = false;
        bool shared = false;
        bool international = false;
        std::string type;
        uint32_t id = 0;
        uint8_t ecc = 0;

        bool operator==(const ServiceLink &other) const = default;
    };

    struct FrequencyInformation {
        bool other_ensemble = false;
        uint16_t id = 0;
        std::string type;
        bool continuous = false;
        uint32_t frequency = 0;
        uint8_t control = 0;

        bool operator==(const FrequencyInformation &other) const = default;
    };

    struct OtherEnsembleService {
        uint32_t service_id = 0;
        bool data_service = false;
        uint8_t conditional_access_id = 0;
        std::vector<uint16_t> ensemble_ids;

        bool operator==(const OtherEnsembleService &other) const = default;
    };

    struct OEAnnouncementSupport {
        uint16_t service_id = 0;
        std::vector<std::string> announcements;
        uint8_t cluster_id = 0;
        uint16_t ensemble_id = 0;

        bool operator==(const OEAnnouncementSupport &other) const = default;
    };

    struct OEAnnouncementSwitch {
        uint8_t cluster_id = 0;
        std::vector<std::string> announcements;
        bool newly_introduced = false;
        uint8_t component_id = 0;
        uint16_t service_id = 0;

        bool operator==(const OEAnnouncementSwitch &other) const = default;
    };

    struct FMAnnouncementSupport {
        uint16_t service_id = 0;
        std::vector<uint16_t> pi_codes;

        bool operator==(const FMAnnouncementSupport &other) const = default;
    };

    struct FMAnnouncementSwitch {
        uint8_t cluster_id = 0;
        bool newly_introduced = false;
        uint8_t region_id = 0;
        uint16_t pi_code = 0;

        bool operator==(const FMAnnouncementSwitch &other) const = default;
    };

    struct FIDCEntry {
        bool d1 = false;
        bool d2 = false;
        uint8_t component_id = 0;
        uint8_t extension = 0;
        std::string type;
        std::vector<uint8_t> data;

        bool operator==(const FIDCEntry &other) const = default;
    };

    struct EmergencyWarningLocation {
        uint8_t nff = 0;
        uint8_t zone = 0;
        bool sub_codes = false;
        std::vector<uint8_t> digits;
        uint16_t sub_code_mask = 0;

        bool operator==(const EmergencyWarningLocation &other) const = default;
    };

    struct EmergencyWarning {
        bool other_ensemble = false;
        uint16_t ensemble_id = 0;
        uint8_t subchannel = 0xFF;
        std::string phase;
        bool trigger_second_present = false;
        uint8_t trigger_second = 0;
        bool status_present = false;
        bool last = false;
        std::string stage;
        uint8_t incident_id = 0;
        std::vector<EmergencyWarningLocation> locations;

        bool operator==(const EmergencyWarning &other) const = default;
    };

    struct EmergencyWarningStatus {
        bool supported = false;
        std::vector<EmergencyWarning> alerts;

        bool operator==(const EmergencyWarningStatus &other) const = default;
    };

    struct Statistics {
        uint32_t fibs_received = 0;
        uint32_t fibs_valid = 0;
        uint32_t fibs_crc_errors = 0;
#if DAB_ENABLE_DIAGNOSTICS
        uint32_t figs_received = 0;
        std::array<uint32_t, 8> fig_types{};
        std::array<uint32_t, 32> fig0_extensions{};
        std::array<uint32_t, 8> fig5_extensions{};
#endif
    };

#if DAB_ENABLE_DIAGNOSTICS
    struct Figure {
        uint64_t sequence = 0;
        int64_t received = 0;
        uint8_t type = 0xFF;
        int16_t extension = -1;
        uint8_t flags = 0;
        std::string name;
        std::string status = "Raw";
        std::vector<uint8_t> data;
        std::vector<std::pair<std::string, std::string>> fields;
    };

    struct FigureHistory {
        uint64_t generation = 0;
        uint64_t oldest = 0;
        uint64_t revision = 0;
        std::vector<Figure> figures;
    };
#endif

    FICDecoder(const std::function<void(const Ensemble &)> &ensemble_callback, const std::function<void(const EnsembleStatus &)> &ensemble_status_callback, const std::function<void(const Service &)> &service_callback, const std::function<void(uint16_t)> &service_removed_callback, const std::function<void(const ServiceComponent &)> &service_component_callback, const std::function<void(uint64_t)> &service_component_removed_callback, const std::function<void(const DateTime &)> &date_time_callback);

    void reset();

    void process(const std::vector<uint8_t> &data);

#if DAB_ENABLE_DIAGNOSTICS
    uint64_t get_fic_revision() const;

    FigureHistory get_figures(uint64_t since_sequence, uint16_t limit) const;
#endif

    Statistics get_statistics() const;

#if DAB_ENABLE_SERVICE_LINKING
    std::vector<ServiceLink> get_service_links() const;
#endif

#if DAB_ENABLE_FREQUENCY_INFO
    std::vector<FrequencyInformation> get_frequency_information() const;
#endif

#if DAB_ENABLE_SERVICE_LINKING
    std::vector<OtherEnsembleService> get_other_ensemble_services() const;
#endif

#if DAB_ENABLE_ANNOUNCEMENTS
    std::vector<OEAnnouncementSupport> get_oe_announcement_support() const;

    std::vector<OEAnnouncementSwitch> get_oe_announcement_switches() const;

    std::vector<FMAnnouncementSupport> get_fm_announcement_support() const;

    std::vector<FMAnnouncementSwitch> get_fm_announcement_switches() const;
#endif

#if DAB_ENABLE_FIDC
    std::vector<FIDCEntry> get_fidc_entries() const;
#endif

#if DAB_ENABLE_EWS
    EmergencyWarningStatus get_emergency_warning_status() const;

    uint64_t get_emergency_warning_sequence() const;
#endif

private:
    struct ServiceState {
        uint16_t id = 0;
        std::string label;
        std::string short_label;
        uint8_t ecc = 0;
        uint8_t primary_subchannel = 0xFF;
        bool dab_plus = false;
        int16_t pty_static = -1;
        int16_t pty_dynamic = -1;
#if DAB_ENABLE_ANNOUNCEMENTS
        uint16_t announcement_support_flags = 0;
        std::set<uint8_t> announcement_clusters;
#endif
    };

#if DAB_ENABLE_ANNOUNCEMENTS
    struct AnnouncementCluster {
        uint16_t flags = 0;
        uint8_t subchannel = 0xFF;

        bool operator==(const AnnouncementCluster &other) const {
            return flags == other.flags && subchannel == other.subchannel;
        }

        bool operator!=(const AnnouncementCluster &other) const {
            return !(*this == other);
        }
    };
#endif

    struct TransportComponent {
        bool defined = false;
        bool data = false;
        uint8_t type = 0;
        bool conditional_access = false;
        uint16_t conditional_access_organization = 0;
        uint8_t subchannel = 0xFF;
        uint16_t packet_address = 0xFFFF;
        bool data_groups = false;
        bool conditional_access_organization_present = false;
    };

    struct Label {
        std::string label;
        std::string short_label;
        bool short_label_present = true;
    };

    struct ExtendedLabel {
        bool toggle = false;
        bool version = false;
        bool initialized = false;
        bool utf16 = false;
        uint8_t segment_count = 0;
        uint8_t text_control = 0;
        uint16_t character_flags = 0;
        std::array<std::vector<uint8_t>, 2> segments;
        std::array<bool, 2> received{};
    };

    std::function<void(const Ensemble &)> ensemble_callback = nullptr;
    std::function<void(const EnsembleStatus &)> ensemble_status_callback = nullptr;
    std::function<void(const Service &)> service_callback = nullptr;
    std::function<void(uint16_t)> service_removed_callback = nullptr;
    std::function<void(const ServiceComponent &)> service_component_callback = nullptr;
    std::function<void(uint64_t)> service_component_removed_callback = nullptr;
    std::function<void(const DateTime &)> date_time_callback = nullptr;

    Ensemble ensemble;
    bool ensemble_valid = false;
    Ensemble emitted_ensemble;
    bool emitted_ensemble_valid = false;
    EnsembleStatus ensemble_status;
    EnsembleStatus emitted_ensemble_status;
    bool emitted_ensemble_status_valid = false;
    bool configuration_valid = false;
    bool configuration_cleanup_pending = false;
    bool next_configuration_valid = false;
    uint16_t next_reconfiguration_counter = 0;
    DateTime date_time;
    uint8_t pty_table_id = 0x01;
    Statistics statistics;
#if DAB_ENABLE_DIAGNOSTICS
    mutable std::mutex figure_mutex;
    std::deque<Figure> figure_history;
    uint64_t figure_generation = 0;
    uint64_t figure_sequence = 0;
    Figure *current_figure = nullptr;
#endif

    std::set<uint64_t> service_ids;
    std::map<uint8_t, Subchannel> subchannels;
    std::map<uint8_t, std::string> subchannel_languages;
    std::map<uint16_t, ServiceState> services;
    std::map<uint16_t, Service> emitted_services;
    std::map<uint64_t, ServiceComponent> service_components;
    std::map<uint64_t, ServiceComponent> emitted_service_components;
    std::map<uint8_t, TransportComponent> subchannel_components;
    std::map<uint16_t, TransportComponent> packet_components;
    std::map<uint8_t, uint8_t> subchannel_fec_schemes;
    std::map<uint8_t, std::set<std::vector<uint8_t>>> next_configuration_figs;
#if DAB_ENABLE_ANNOUNCEMENTS
    std::map<uint8_t, AnnouncementCluster> announcement_clusters;
#endif
    std::map<uint64_t, std::pair<std::string, std::string>> service_component_labels;
    std::map<uint32_t, std::pair<std::string, std::string>> data_service_labels;
    std::map<uint64_t, std::map<uint8_t, std::string>> application_labels;
    std::map<uint64_t, Label> basic_labels;
    std::map<uint64_t, ExtendedLabel> extended_labels;
    std::map<uint64_t, Label> completed_extended_labels;
#if DAB_ENABLE_SERVICE_LINKING
    std::map<uint32_t, std::vector<ServiceLink>> service_links;
    std::map<uint32_t, std::vector<uint8_t>> service_link_starts;
#endif
#if DAB_ENABLE_FREQUENCY_INFO
    std::map<uint64_t, std::vector<FrequencyInformation>> frequency_information;
    std::map<uint64_t, std::vector<uint8_t>> frequency_information_starts;
#endif
#if DAB_ENABLE_SERVICE_LINKING
    std::map<uint64_t, OtherEnsembleService> other_ensemble_services;
    std::map<uint64_t, std::vector<uint8_t>> other_ensemble_service_starts;
#endif
#if DAB_ENABLE_ANNOUNCEMENTS
    std::map<uint64_t, OEAnnouncementSupport> oe_announcement_support;
    std::map<uint8_t, OEAnnouncementSwitch> oe_announcement_switches;
    std::map<uint16_t, FMAnnouncementSupport> fm_announcement_support;
    std::map<uint8_t, FMAnnouncementSwitch> fm_announcement_switches;
#endif

#if DAB_ENABLE_FIDC
    std::map<uint16_t, FIDCEntry> fidc_entries;
#endif
#if DAB_ENABLE_EWS
    bool emergency_warning_supported = false;
    std::map<uint32_t, EmergencyWarning> emergency_warnings;
    uint64_t emergency_warning_sequence = 0;
#endif

#if DAB_ENABLE_DIAGNOSTICS
    void store_figure(Figure figure);

    void inspect_field(const std::string &name, const std::string &value);

    void inspect_error(const std::string &reason);

    static std::string figure_name(uint8_t type, int16_t extension);
#endif

    void process_fib(const uint8_t *data);

    void process_fig0(const uint8_t *data, size_t length, bool count_statistics = true);

    void process_fig0_0(const uint8_t *data, size_t length);

    void process_fig0_1(const uint8_t *data, size_t length);

    void process_fig0_2(const uint8_t *data, size_t length, bool pd);

    void process_fig0_3(const uint8_t *data, size_t length);

    void process_fig0_4(const uint8_t *data, size_t length);

    void process_fig0_5(const uint8_t *data, size_t length);

#if DAB_ENABLE_SERVICE_LINKING
    void process_fig0_6(const uint8_t *data, size_t length, bool pd, bool continuation);
#endif

    void process_fig0_7(const uint8_t *data, size_t length);

    void process_fig0_8(const uint8_t *data, size_t length, bool pd);

    void process_fig0_9(const uint8_t *data, size_t length);

    void process_fig0_10(const uint8_t *data, size_t length);

    void process_fig0_13(const uint8_t *data, size_t length, bool pd);

    void process_fig0_14(const uint8_t *data, size_t length);


#if DAB_ENABLE_EWS
    void process_fig0_15(const uint8_t *data, size_t length, bool other_ensemble, bool continuation);
#endif

    void process_fig0_17(const uint8_t *data, size_t length);

#if DAB_ENABLE_ANNOUNCEMENTS
    void process_fig0_18(const uint8_t *data, size_t length);

    void process_fig0_19(const uint8_t *data, size_t length);
#endif

    void process_fig0_20(const uint8_t *data, size_t length, bool pd);

#if DAB_ENABLE_FREQUENCY_INFO
    void process_fig0_21(const uint8_t *data, size_t length, bool other_ensemble, bool continuation);
#endif

#if DAB_ENABLE_SERVICE_LINKING
    void process_fig0_24(const uint8_t *data, size_t length, bool pd, bool other_ensemble, bool continuation);
#endif

#if DAB_ENABLE_ANNOUNCEMENTS
    void process_fig0_25(const uint8_t *data, size_t length);

    void process_fig0_26(const uint8_t *data, size_t length);

    void process_fig0_27(const uint8_t *data, size_t length);

    void process_fig0_28(const uint8_t *data, size_t length);
#endif

    void process_fig1(const uint8_t *data, size_t length);

    void process_fig2(const uint8_t *data, size_t length);

    void update_label(uint64_t label_key);


#if DAB_ENABLE_FIDC
    void process_fig5(const uint8_t *data, size_t length);
#endif

#if DAB_ENABLE_EWS
    static bool decode_emergency_warning_locations(const uint8_t *data, size_t length, std::vector<EmergencyWarningLocation> &locations);

    static std::string decode_emergency_warning_phase(uint8_t phase);

    static std::string decode_emergency_warning_stage(uint8_t stage);
#endif

#if DAB_ENABLE_FIDC
    static std::string decode_fidc_type(uint8_t extension);
#endif

    void update_subchannel(uint8_t subchannel);

    void update_service(uint16_t service_id);

    void update_service_component(uint64_t component_key);

    void prepare_configuration_change();

    void apply_next_configuration();

    void finish_configuration_change();

    void update_ensemble();

    void update_ensemble_status();

    void update_alarm();

    static uint16_t calculate_crc(const uint8_t *data, size_t length);

    static std::string decode_label(const uint8_t *data, size_t length, uint8_t charset);

    static std::string decode_short_label(const uint8_t *data, size_t length, uint8_t charset, uint16_t flags);

    static std::string decode_extended_short_label(const std::vector<uint8_t> &data, bool utf16, uint16_t flags);

    static std::string decode_ebu_latin(const uint8_t *data, size_t length);

    static std::string decode_utf8(const uint8_t *data, size_t length);

    static std::string decode_ucs2(const uint8_t *data, size_t length);

    static std::string decode_language(uint8_t language);

    static std::string decode_pty(uint8_t pty, uint8_t table_id);

#if DAB_ENABLE_ANNOUNCEMENTS
    static std::vector<std::string> decode_announcement_flags(uint16_t flags);
#endif

    static std::string decode_user_application(uint16_t application);

    static uint64_t make_service_component_key(uint32_t service_id, bool data_service, uint8_t component_id);

    static void append_utf8(std::string &output, uint32_t codepoint);

    static const uint16_t UEP_SIZES[64];
    static const uint8_t UEP_PROTECTION_LEVELS[64];
    static const uint16_t UEP_BITRATES[64];
    static const uint8_t EEP_A_SIZE_FACTORS[4];
    static const uint8_t EEP_B_SIZE_FACTORS[4];
    static const char *LANGUAGES_00_2B[44];
    static const char *LANGUAGES_7F_45[59];
    static const char *PTY_RDS[30];
    static const char *PTY_RBDS[30];
#if DAB_ENABLE_ANNOUNCEMENTS
    static const char *ANNOUNCEMENT_TYPES[11];
#endif
};

#endif //FIC_DECODER_H
