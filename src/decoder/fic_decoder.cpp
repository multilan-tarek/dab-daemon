#include "fic_decoder.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <ranges>
#include <utility>

FICDecoder::FICDecoder(const std::function<void(const Ensemble &)> &ensemble_callback, const std::function<void(const EnsembleStatus &)> &ensemble_status_callback, const std::function<void(const Service &)> &service_callback, const std::function<void(uint16_t)> &service_removed_callback, const std::function<void(const ServiceComponent &)> &service_component_callback, const std::function<void(uint64_t)> &service_component_removed_callback, const std::function<void(const DateTime &)> &date_time_callback) {
    this->ensemble_callback = ensemble_callback;
    this->ensemble_status_callback = ensemble_status_callback;
    this->service_callback = service_callback;
    this->service_removed_callback = service_removed_callback;
    this->service_component_callback = service_component_callback;
    this->service_component_removed_callback = service_component_removed_callback;
    this->date_time_callback = date_time_callback;
}

void FICDecoder::reset() {
#if DAB_ENABLE_DIAGNOSTICS
    {
        const std::lock_guard lock(figure_mutex);
        figure_history.clear();
        figure_generation++;
        figure_sequence++;
    }
#endif
    ensemble = {};
    ensemble_valid = false;
    emitted_ensemble = {};
    emitted_ensemble_valid = false;
    ensemble_status = {};
    emitted_ensemble_status = {};
    emitted_ensemble_status_valid = false;
    configuration_valid = false;
    configuration_cleanup_pending = false;
    next_configuration_valid = false;
    next_reconfiguration_counter = 0;
    date_time = {};
    pty_table_id = 0x01;
    statistics = {};
    service_ids.clear();
    subchannels.clear();
    subchannel_languages.clear();
    services.clear();
    emitted_services.clear();
    service_components.clear();
    emitted_service_components.clear();
    subchannel_components.clear();
    packet_components.clear();
    subchannel_fec_schemes.clear();
    next_configuration_figs.clear();
#if DAB_ENABLE_ANNOUNCEMENTS
    announcement_clusters.clear();
#endif
    service_component_labels.clear();
    data_service_labels.clear();
    application_labels.clear();
    basic_labels.clear();
    extended_labels.clear();
    completed_extended_labels.clear();
#if DAB_ENABLE_SERVICE_LINKING
    service_links.clear();
    service_link_starts.clear();
    other_ensemble_services.clear();
    other_ensemble_service_starts.clear();
#endif
#if DAB_ENABLE_FREQUENCY_INFO
    frequency_information.clear();
    frequency_information_starts.clear();
#endif
#if DAB_ENABLE_ANNOUNCEMENTS
    oe_announcement_support.clear();
    oe_announcement_switches.clear();
    fm_announcement_support.clear();
    fm_announcement_switches.clear();
#endif
#if DAB_ENABLE_FIDC
    fidc_entries.clear();
#endif
#if DAB_ENABLE_EWS
    emergency_warning_supported = false;
    emergency_warnings.clear();
    emergency_warning_sequence = 0;
#endif
}

void FICDecoder::process(const std::vector<uint8_t> &data) {
    if (data.size() % 32 != 0) {
#if DAB_ENABLE_DIAGNOSTICS
        Figure figure;
        figure.name = "FIB";
        figure.status = "Incomplete";
        figure.data.assign(data.end() - static_cast<std::ptrdiff_t>(data.size() % 32), data.end());
        figure.fields.emplace_back("Input bytes", std::to_string(data.size()));
        store_figure(std::move(figure));
#endif
        return;
    }

    for (size_t offset = 0; offset < data.size(); offset += 32) {
        process_fib(data.data() + offset);
    }
}

#if DAB_ENABLE_DIAGNOSTICS
uint64_t FICDecoder::get_fic_revision() const {
    const std::lock_guard lock(figure_mutex);
    return figure_sequence;
}

FICDecoder::FigureHistory FICDecoder::get_figures(const uint64_t since_sequence, const uint16_t limit) const {
    const std::lock_guard lock(figure_mutex);
    FigureHistory result;
    result.generation = figure_generation;
    result.oldest = figure_history.empty() ? figure_sequence + 1 : figure_history.front().sequence;
    result.revision = figure_sequence;
    const size_t count = std::min<uint16_t>(limit, 512);

    for (const auto &figure: figure_history) {
        if (result.figures.size() >= count) {
            break;
        }

        if (figure.sequence > since_sequence) {
            result.figures.push_back(figure);
        }
    }

    return result;
}

void FICDecoder::store_figure(Figure figure) {
    figure.received = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::lock_guard lock(figure_mutex);
    figure.sequence = ++figure_sequence;
    figure_history.push_back(std::move(figure));

    if (figure_history.size() > 2048) {
        figure_history.pop_front();
    }
}

void FICDecoder::inspect_field(const std::string &name, const std::string &value) {
    if (current_figure) {
        current_figure->fields.emplace_back(name, value);

        if (current_figure->status == "Raw") {
            current_figure->status = "Decoded";
        }
    }
}

void FICDecoder::inspect_error(const std::string &reason) {
    if (current_figure && current_figure->status != "Malformed") {
        current_figure->status = "Malformed";
        current_figure->fields.emplace_back("Error", reason);
    }
}

std::string FICDecoder::figure_name(const uint8_t type, const int16_t extension) {
    if (type == 0) {
        static const std::map<int16_t, std::string> NAMES{
            {0, "Ensemble information"}, {1, "Subchannels"}, {2, "Services"}, {3, "Packet components"},
            {4, "Conditional access"}, {5, "Component language"}, {6, "Service linking"}, {7, "Configuration"},
            {8, "Component mapping"}, {9, "Country and time offset"}, {10, "Date and time"},
            {13, "User applications"}, {14, "Packet FEC"}, {15, "Emergency warning"}, {17, "Programme type"},
            {18, "Announcement support"}, {19, "Announcement switching"}, {20, "Component information"},
            {21, "Frequency information"}, {24, "Other ensemble services"}, {25, "Other ensemble announcements"},
            {26, "Other ensemble switching"}, {27, "FM announcement support"}, {28, "FM announcement switching"},
        };
        const auto name = NAMES.find(extension);
        return name == NAMES.end() ? "Unknown extension" : name->second;
    }

    if (type == 1 || type == 2) {
        return type == 1 ? "Label" : "Extended label";
    }

    return type == 5 ? "FIDC" : "Unknown FIG";
}
#endif

FICDecoder::Statistics FICDecoder::get_statistics() const {
    return statistics;
}

bool FICDecoder::is_service_information_complete() const {
    if (!ensemble_valid || ensemble.id != ensemble_status.ensemble_id || ensemble.label.empty() || configuration_cleanup_pending || service_ids.empty() || service_ids.size() < ensemble_status.service_count) {
        return false;
    }

    if (ensemble_status.service_count == 0) {
        if (subchannels.empty()) {
            return false;
        }

        for (const auto &[subchannel_id, subchannel]: subchannels) {
            const auto component = subchannel_components.find(subchannel_id);

            if (component != subchannel_components.end() && component->second.defined) {
                continue;
            }

            const bool packet_service_found = std::ranges::any_of(packet_components | std::views::values, [subchannel_id](const TransportComponent &packet_component) {
                return packet_component.defined && packet_component.subchannel == subchannel_id;
            });

            if (!packet_service_found) {
                return false;
            }
        }
    }

    for (const auto &[service_id, service]: services) {
        // Only supported, unencrypted primary audio components enter the station list.
        if (service.primary_subchannel == 0xFF) {
            continue;
        }

        if (service.label.empty() || !subchannels.contains(service.primary_subchannel) || !emitted_services.contains(service_id)) {
            return false;
        }
    }

    for (const auto &[label_key, label]: extended_labels) {
        const uint8_t extension = label_key >> 60;
        const uint16_t id = static_cast<uint16_t>(label_key);

        if ((extension == 0 && id == ensemble.id) || (extension == 1 && services.contains(id) && services.at(id).primary_subchannel != 0xFF)) {
            if (!label.initialized || !label.received[0] || (label.segment_count != 0 && !label.received[1])) {
                return false;
            }
        }
    }

    return true;
}

#if DAB_ENABLE_SERVICE_LINKING
std::vector<FICDecoder::ServiceLink> FICDecoder::get_service_links() const {
    std::vector<ServiceLink> result;

    for (const auto &links: service_links | std::views::values) {
        result.insert(result.end(), links.begin(), links.end());
    }

    return result;
}
#endif


#if DAB_ENABLE_FREQUENCY_INFO
std::vector<FICDecoder::FrequencyInformation> FICDecoder::get_frequency_information() const {
    std::vector<FrequencyInformation> result;

    for (const auto &frequencies: frequency_information | std::views::values) {
        result.insert(result.end(), frequencies.begin(), frequencies.end());
    }

    return result;
}
#endif


#if DAB_ENABLE_SERVICE_LINKING
std::vector<FICDecoder::OtherEnsembleService> FICDecoder::get_other_ensemble_services() const {
    std::vector<OtherEnsembleService> result;

    for (const auto &service: other_ensemble_services | std::views::values) {
        result.push_back(service);
    }

    return result;
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
std::vector<FICDecoder::OEAnnouncementSupport> FICDecoder::get_oe_announcement_support() const {
    std::vector<OEAnnouncementSupport> result;

    for (const auto &support: oe_announcement_support | std::views::values) {
        result.push_back(support);
    }

    return result;
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
std::vector<FICDecoder::OEAnnouncementSwitch> FICDecoder::get_oe_announcement_switches() const {
    std::vector<OEAnnouncementSwitch> result;

    for (const auto &announcement: oe_announcement_switches | std::views::values) {
        result.push_back(announcement);
    }

    return result;
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
std::vector<FICDecoder::FMAnnouncementSupport> FICDecoder::get_fm_announcement_support() const {
    std::vector<FMAnnouncementSupport> result;

    for (const auto &announcement: fm_announcement_support | std::views::values) {
        result.push_back(announcement);
    }

    return result;
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
std::vector<FICDecoder::FMAnnouncementSwitch> FICDecoder::get_fm_announcement_switches() const {
    std::vector<FMAnnouncementSwitch> result;

    for (const auto &announcement: fm_announcement_switches | std::views::values) {
        result.push_back(announcement);
    }

    return result;
}
#endif


#if DAB_ENABLE_FIDC
std::vector<FICDecoder::FIDCEntry> FICDecoder::get_fidc_entries() const {
    std::vector<FIDCEntry> result;

    for (const auto &entry: fidc_entries | std::views::values) {
        result.push_back(entry);
    }

    return result;
}
#endif


#if DAB_ENABLE_EWS
FICDecoder::EmergencyWarningStatus FICDecoder::get_emergency_warning_status() const {
    EmergencyWarningStatus result;
    result.supported = emergency_warning_supported;

    for (const auto &alert: emergency_warnings | std::views::values) {
        result.alerts.push_back(alert);
    }

    return result;
}
#endif


#if DAB_ENABLE_EWS
uint64_t FICDecoder::get_emergency_warning_sequence() const {
    return emergency_warning_sequence;
}
#endif


void FICDecoder::process_fib(const uint8_t *data) {
    statistics.fibs_received++;
    const uint16_t stored_crc = static_cast<uint16_t>(data[30] << 8 | data[31]);

    if (stored_crc != calculate_crc(data, 30)) {
        statistics.fibs_crc_errors++;
#if DAB_ENABLE_DIAGNOSTICS
        Figure figure;
        figure.name = "FIB";
        figure.status = "CRC error";
        figure.data.assign(data, data + 32);
        figure.fields.emplace_back("Received CRC", std::format("0x{:04X}", stored_crc));
        figure.fields.emplace_back("Calculated CRC", std::format("0x{:04X}", calculate_crc(data, 30)));
        store_figure(std::move(figure));
#endif
        return;
    }

    statistics.fibs_valid++;

    size_t offset = 0;

    while (offset < 30 && data[offset] != 0xFF) {
        const uint8_t type = data[offset] >> 5;
        const size_t length = data[offset] & 0x1F;
        offset++;

#if DAB_ENABLE_DIAGNOSTICS
        Figure figure;
        figure.type = type;
        figure.extension = (type == 0 || type == 1 || type == 2 || type == 5) && length > 0 && offset < 30 ? data[offset] & (type == 0 ? 0x1F : 0x07) : -1;
        figure.flags = type == 0 && length > 0 && offset < 30 ? data[offset] & 0xE0 : 0;
        figure.name = figure_name(type, figure.extension);
        if (figure.name == "Unknown FIG" || figure.name == "Unknown extension") {
            figure.status = "Unsupported";
        }
        if ((type == 1 || type == 2) && figure.extension != 0 && figure.extension != 1 && figure.extension != 4 && figure.extension != 5 && figure.extension != 6) {
            figure.status = "Unsupported";
        }
#if !DAB_ENABLE_FIDC
        if (type == 5) {
            figure.status = "Disabled";
        }
#endif
        figure.data.assign(data + offset - 1, data + std::min(offset + length, size_t{30}));
        figure.fields.emplace_back("Type", std::to_string(type));
        figure.fields.emplace_back("Length", std::to_string(length));
        figure.fields.emplace_back("Extension", std::to_string(figure.extension));

        if (type == 0 && length > 0) {
            figure.fields.emplace_back("C/N", std::to_string((figure.flags >> 7) & 1));
            figure.fields.emplace_back("OE", std::to_string((figure.flags >> 6) & 1));
            figure.fields.emplace_back("P/D", std::to_string((figure.flags >> 5) & 1));
        }

        current_figure = &figure;
#endif
        if (offset + length > 30) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("FIG length exceeds FIB payload");
            current_figure = nullptr;
            store_figure(std::move(figure));
#endif
            return;
        }

#if DAB_ENABLE_DIAGNOSTICS
        statistics.figs_received++;
        statistics.fig_types[type]++;
#endif

        if (type == 0) {
            process_fig0(data + offset, length);
        } else if (type == 1) {
            process_fig1(data + offset, length);
        } else if (type == 2) {
            process_fig2(data + offset, length);
#if DAB_ENABLE_FIDC
        } else if (type == 5) {
            process_fig5(data + offset, length);
#endif
        }

#if DAB_ENABLE_DIAGNOSTICS
        if (length == 0 && (type == 0 || type == 1 || type == 2 || type == 5)) {
            inspect_error("Missing FIG extension header");
        }

        current_figure = nullptr;
        store_figure(std::move(figure));
#endif
        offset += length;
    }
}

void FICDecoder::process_fig0(const uint8_t *data, const size_t length, const bool count_statistics) {
    if (length < 1) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    const uint8_t header = data[0];
    const bool cn = (header & 0x80) != 0;
    const bool oe = (header & 0x40) != 0;
    const bool pd = (header & 0x20) != 0;
    const uint8_t extension = header & 0x1F;

#if DAB_ENABLE_DIAGNOSTICS
    if (current_figure) {
        if (oe && extension != 15 && extension != 21 && extension != 24) {
            current_figure->status = "Other ensemble";
        } else if (cn && extension != 6 && extension != 15 && extension != 21 && extension != 24) {
            current_figure->status = "Next configuration";
        }
#if !DAB_ENABLE_SERVICE_LINKING
        if (extension == 6 || extension == 24) {
            current_figure->status = "Disabled";
        }
#endif
#if !DAB_ENABLE_FREQUENCY_INFO
        if (extension == 21) {
            current_figure->status = "Disabled";
        }
#endif
#if !DAB_ENABLE_EWS
        if (extension == 15) {
            current_figure->status = "Disabled";
        }
#endif
#if !DAB_ENABLE_ANNOUNCEMENTS
        if (extension == 18 || extension == 19 || extension == 25 || extension == 26 || extension == 27 || extension == 28) {
            current_figure->status = "Disabled";
        }
#endif
    }

    if (count_statistics) {
        statistics.fig0_extensions[extension]++;
    }
#else
    (void)count_statistics;
#endif

    if (extension == 6) {
#if DAB_ENABLE_SERVICE_LINKING
        if (!oe) {
            process_fig0_6(data + 1, length - 1, pd, cn);
        }
#endif
        return;
    }

    if (extension == 15) {
#if DAB_ENABLE_EWS
        if (!pd) {
            process_fig0_15(data + 1, length - 1, oe, cn);
        }
#endif
        return;
    }

    if (extension == 21) {
#if DAB_ENABLE_FREQUENCY_INFO
        if (!pd) {
            process_fig0_21(data + 1, length - 1, oe, cn);
        }
#endif
        return;
    }

    if (extension == 24) {
#if DAB_ENABLE_SERVICE_LINKING
        process_fig0_24(data + 1, length - 1, pd, oe, cn);
#endif
        return;
    }

    if (extension == 27) {
#if DAB_ENABLE_ANNOUNCEMENTS
        if (!oe && !pd) {
            process_fig0_27(data + 1, length - 1);
        }
#endif
        return;
    }

    if (oe) {
        return;
    }

    if (cn) {
        if (extension == 7 && length >= 3) {
            const uint16_t reconfiguration_counter = static_cast<uint16_t>((data[1] & 0x03) << 8 | data[2]);

            if (next_configuration_valid && next_reconfiguration_counter != reconfiguration_counter) {
                next_configuration_figs.clear();
            }

            next_configuration_valid = true;
            next_reconfiguration_counter = reconfiguration_counter;
        }

        if (extension == 1 || extension == 2 || extension == 3 || extension == 4 || extension == 8 || extension == 13 || extension == 14) {
            std::vector<uint8_t> next_data(data, data + length);
            next_data[0] &= 0x7F;
            next_configuration_figs[extension].insert(std::move(next_data));
        }

        return;
    }

    if (extension == 0 && !pd) {
        process_fig0_0(data + 1, length - 1);
    } else if (extension == 1 && !pd) {
        process_fig0_1(data + 1, length - 1);
    } else if (extension == 2) {
        process_fig0_2(data + 1, length - 1, pd);
    } else if (extension == 3 && !pd) {
        process_fig0_3(data + 1, length - 1);
    } else if (extension == 4 && !pd) {
        process_fig0_4(data + 1, length - 1);
    } else if (extension == 5 && !pd) {
        process_fig0_5(data + 1, length - 1);
    } else if (extension == 7 && !pd) {
        process_fig0_7(data + 1, length - 1);
    } else if (extension == 8) {
        process_fig0_8(data + 1, length - 1, pd);
    } else if (extension == 9 && !pd) {
        process_fig0_9(data + 1, length - 1);
    } else if (extension == 10 && !pd) {
        process_fig0_10(data + 1, length - 1);
    } else if (extension == 13) {
        process_fig0_13(data + 1, length - 1, pd);
    } else if (extension == 14 && !pd) {
        process_fig0_14(data + 1, length - 1);
    } else if (extension == 17 && !pd) {
        process_fig0_17(data + 1, length - 1);
#if DAB_ENABLE_ANNOUNCEMENTS
    } else if (extension == 18 && !pd) {
        process_fig0_18(data + 1, length - 1);
    } else if (extension == 19 && !pd) {
        process_fig0_19(data + 1, length - 1);
#endif
    } else if (extension == 20) {
        process_fig0_20(data + 1, length - 1, pd);
#if DAB_ENABLE_ANNOUNCEMENTS
    } else if (extension == 25 && !pd) {
        process_fig0_25(data + 1, length - 1);
    } else if (extension == 26 && !pd) {
        process_fig0_26(data + 1, length - 1);
    } else if (extension == 28 && !pd) {
        process_fig0_28(data + 1, length - 1);
#endif
    }
}

void FICDecoder::process_fig0_0(const uint8_t *data, const size_t length) {
    if (length < 4) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    const uint16_t ensemble_id = static_cast<uint16_t>(data[0] << 8 | data[1]);
    const uint8_t change_flags = data[2] >> 6;
    const bool alarm_supported = (data[2] & 0x20) != 0;
    const uint8_t cif_high = data[2] & 0x1F;
    const uint8_t cif_low = data[3];

    if (cif_high > 19 || cif_low > 249) {
        return;
    }

    if (change_flags != 0 && length < 5) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    ensemble_status.ensemble_id = ensemble_id;
    ensemble_status.change_flags = change_flags;
    ensemble_status.alarm_supported = alarm_supported;
    ensemble_status.occurrence_change = change_flags != 0 ? data[4] : 0;
    ensemble_status.cif_counter = static_cast<uint16_t>(cif_high * 250 + cif_low);
#if DAB_ENABLE_DIAGNOSTICS
    inspect_field("Ensemble ID", std::format("0x{:04X}", ensemble_id));
    inspect_field("Change flags", std::to_string(change_flags));
    inspect_field("Alarm supported", std::to_string(alarm_supported));
    inspect_field("CIF counter", std::to_string(ensemble_status.cif_counter));
    inspect_field("Occurrence change", std::to_string(ensemble_status.occurrence_change));
#endif
    update_ensemble_status();
}

void FICDecoder::process_fig0_1(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 3 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        Subchannel subchannel;
        subchannel.id = data[offset] >> 2;
        subchannel.start = static_cast<uint16_t>((data[offset] & 0x03) << 8 | data[offset + 1]);
        offset += 2;

        const bool long_form = (data[offset] & 0x80) != 0;

        if (long_form) {
            if (offset + 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            const uint8_t option = (data[offset] & 0x70) >> 4;
            const uint8_t protection_level = (data[offset] & 0x0C) >> 2;
            subchannel.size = static_cast<uint16_t>((data[offset] & 0x03) << 8 | data[offset + 1]);
            offset += 2;

            if (option == 0) {
                subchannel.protection = "EEP " + std::to_string(protection_level + 1) + "-A";
                subchannel.bitrate = static_cast<uint16_t>(subchannel.size / EEP_A_SIZE_FACTORS[protection_level] * 8);
            } else if (option == 1) {
                subchannel.protection = "EEP " + std::to_string(protection_level + 1) + "-B";
                subchannel.bitrate = static_cast<uint16_t>(subchannel.size / EEP_B_SIZE_FACTORS[protection_level] * 32);
            } else {
                continue;
            }
        } else {
            const bool table_switch = (data[offset] & 0x40) != 0;
            const uint8_t table_index = data[offset] & 0x3F;
            offset++;

            if (table_switch) {
                continue;
            }

            subchannel.size = UEP_SIZES[table_index];
            subchannel.protection = "UEP " + std::to_string(UEP_PROTECTION_LEVELS[table_index]);
            subchannel.bitrate = UEP_BITRATES[table_index];
        }

#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("SubCh[{}].", subchannel.id);
        inspect_field(prefix + "Start CU", std::to_string(subchannel.start));
        inspect_field(prefix + "Size CU", std::to_string(subchannel.size));
        inspect_field(prefix + "Bitrate kbit/s", std::to_string(subchannel.bitrate));
        inspect_field(prefix + "Protection", subchannel.protection);
        inspect_field(prefix + "Long form", std::to_string(long_form));
#endif
        const auto current = subchannels.find(subchannel.id);

        if (current == subchannels.end() || current->second != subchannel) {
            subchannels[subchannel.id] = subchannel;
            update_subchannel(subchannel.id);
        }
    }
}

void FICDecoder::process_fig0_2(const uint8_t *data, const size_t length, const bool pd) {
    size_t offset = 0;
    const size_t service_id_length = pd ? 4 : 2;
    bool service_ids_changed = false;

    while (offset < length) {
        if (offset + service_id_length + 1 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const size_t component_count = data[offset + service_id_length] & 0x0F;
        offset += service_id_length + 1 + component_count * 2;

        if (offset > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }
    }

    offset = 0;

    while (offset < length) {
        uint32_t service_id = 0;

        for (size_t index = 0; index < service_id_length; index++) {
            service_id = service_id << 8 | data[offset + index];
        }

        offset += service_id_length;
        const uint8_t component_count = data[offset] & 0x0F;
        offset++;
        const uint64_t service_key = static_cast<uint64_t>(pd) << 32 | service_id;

        if (service_ids.insert(service_key).second) {
            service_ids_changed = true;
        }

        ServiceState *service = nullptr;

        if (!pd) {
            service = &services[static_cast<uint16_t>(service_id)];
            service->id = static_cast<uint16_t>(service_id);
        }

        for (uint8_t component = 0; component < component_count; component++) {
            const uint8_t tmid = data[offset] >> 6;
            const uint8_t type = data[offset] & 0x3F;
            const bool primary = (data[offset + 1] & 0x02) != 0;
            const bool conditional_access = (data[offset + 1] & 0x01) != 0;
            uint8_t subchannel = 0xFF;
            uint16_t packet_component_id = 0xFFFF;

            if (tmid == 0 || tmid == 1) {
                subchannel = data[offset + 1] >> 2;
                TransportComponent &transport_component = subchannel_components[subchannel];
                transport_component.defined = true;
                transport_component.data = tmid != 0;
                transport_component.type = type;
                transport_component.conditional_access = conditional_access;
                transport_component.subchannel = subchannel;
            } else if (tmid == 3) {
                packet_component_id = static_cast<uint16_t>((data[offset] & 0x3F) << 6 | data[offset + 1] >> 2);
                TransportComponent &transport_component = packet_components[packet_component_id];
                transport_component.defined = true;
                transport_component.data = true;
                transport_component.conditional_access = conditional_access;
            } else {
                offset += 2;
                continue;
            }

#if DAB_ENABLE_DIAGNOSTICS
            const auto prefix = std::format("Service[0x{:X}].Component[{}].", service_id, component);
            inspect_field(prefix + "TMId", std::to_string(tmid));
            inspect_field(prefix + "Type", std::to_string(type));
            inspect_field(prefix + "Primary", std::to_string(primary));
            inspect_field(prefix + "CA", std::to_string(conditional_access));
            inspect_field(prefix + "SubChId", subchannel == 0xFF ? "-" : std::to_string(subchannel));
            inspect_field(prefix + "SCId", packet_component_id == 0xFFFF ? "-" : std::format("0x{:03X}", packet_component_id));
#endif
            if (primary) {
                const uint64_t component_key = make_service_component_key(service_id, pd, 0);
                ServiceComponent &service_component = service_components[component_key];
                service_component.service_id = service_id;
                service_component.data_service = pd;
                service_component.id = 0;
                service_component.subchannel = subchannel;
                service_component.component_id = packet_component_id;
            }

            for (const auto &[component_key, service_component]: service_components) {
                if ((tmid == 3 && service_component.component_id == packet_component_id) || (tmid != 3 && service_component.component_id == 0xFFFF && service_component.subchannel == subchannel)) {
                    update_service_component(component_key);
                }
            }

            if (service && tmid == 0) {
                const uint8_t ascty = type;

                if (!conditional_access && primary && (ascty == 0 || ascty == 63)) {
                    const uint8_t old_subchannel = service->primary_subchannel;
                    const bool old_dab_plus = service->dab_plus;
                    service->primary_subchannel = subchannel;
                    service->dab_plus = ascty == 63;

                    if (old_subchannel != service->primary_subchannel || old_dab_plus != service->dab_plus) {
                        update_service(static_cast<uint16_t>(service_id));
                    }
                }
            }

            offset += 2;
        }
    }

    if (service_ids_changed) {
        update_ensemble_status();

        if (configuration_cleanup_pending && ensemble_status.service_count != 0 && ensemble_status.services_found >= ensemble_status.service_count) {
            finish_configuration_change();
        }
    }
}

void FICDecoder::process_fig0_3(const uint8_t *data, const size_t length) {
    size_t offset = 0;
    std::map<uint16_t, TransportComponent> decoded_components;

    while (offset < length) {
        if (offset + 5 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const uint16_t component_id = static_cast<uint16_t>(data[offset] << 4 | data[offset + 1] >> 4);
        const bool conditional_access_organization_present = (data[offset + 1] & 0x01) != 0;

        if (conditional_access_organization_present && offset + 7 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        TransportComponent transport_component;
        const auto current = packet_components.find(component_id);

        if (current != packet_components.end()) {
            transport_component = current->second;
        }

        transport_component.data = true;
        transport_component.type = data[offset + 2] & 0x3F;
        transport_component.subchannel = data[offset + 3] >> 2;
        transport_component.packet_address = static_cast<uint16_t>((data[offset + 3] & 0x03) << 8 | data[offset + 4]);
        transport_component.data_groups = (data[offset + 2] & 0x80) == 0;
        transport_component.conditional_access_organization_present = conditional_access_organization_present;
        transport_component.conditional_access_organization = conditional_access_organization_present ? static_cast<uint16_t>(data[offset + 5] << 8 | data[offset + 6]) : 0;
#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("SCId[0x{:03X}].", component_id);
        inspect_field(prefix + "DSCTy", std::to_string(transport_component.type));
        inspect_field(prefix + "SubChId", std::to_string(transport_component.subchannel));
        inspect_field(prefix + "Packet address", std::to_string(transport_component.packet_address));
        inspect_field(prefix + "Data groups", std::to_string(transport_component.data_groups));
        inspect_field(prefix + "CA organization present", std::to_string(conditional_access_organization_present));
        inspect_field(prefix + "CA organization", std::format("0x{:04X}", transport_component.conditional_access_organization));
#endif
        decoded_components[component_id] = transport_component;
        offset += conditional_access_organization_present ? 7 : 5;
    }

    for (const auto &[component_id, transport_component]: decoded_components) {
        packet_components[component_id] = transport_component;

        for (const auto &[component_key, component]: service_components) {
            if (component.component_id == component_id) {
                update_service_component(component_key);
            }
        }
    }
}

void FICDecoder::process_fig0_4(const uint8_t *data, const size_t length) {
    if (length % 3 != 0) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    for (size_t offset = 0; offset < length; offset += 3) {
        const uint8_t subchannel = data[offset] & 0x3F;
        TransportComponent &transport_component = subchannel_components[subchannel];
        transport_component.subchannel = subchannel;
        transport_component.conditional_access_organization_present = true;
        transport_component.conditional_access_organization = static_cast<uint16_t>(data[offset + 1] << 8 | data[offset + 2]);

#if DAB_ENABLE_DIAGNOSTICS
        inspect_field(std::format("SubCh[{}].CA organization", subchannel), std::format("0x{:04X}", transport_component.conditional_access_organization));
#endif
        for (const auto &[component_key, component]: service_components) {
            if (component.component_id == 0xFFFF && component.subchannel == subchannel) {
                update_service_component(component_key);
            }
        }
    }
}

void FICDecoder::process_fig0_5(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if ((data[offset] & 0x80) != 0) {
            if (offset + 3 > length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            const uint16_t component_id = static_cast<uint16_t>((data[offset] & 0x0F) << 8 | data[offset + 1]);
            const std::string language = decode_language(data[offset + 2]);

#if DAB_ENABLE_DIAGNOSTICS
            inspect_field(std::format("SCId[0x{:03X}].Language", component_id), language);
#endif
            for (auto &[component_key, component]: service_components) {
                if (component.component_id != component_id || component.language == language) {
                    continue;
                }

                component.language = language;
                update_service_component(component_key);
            }

            offset += 3;
            continue;
        }

        if (offset + 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const bool fic_component = (data[offset] & 0x40) != 0;

#if DAB_ENABLE_DIAGNOSTICS
        inspect_field(std::format("{}[{}].Language", fic_component ? "FIC" : "SubCh", data[offset] & 0x3F), decode_language(data[offset + 1]));
#endif
        if (!fic_component) {
            const uint8_t subchannel = data[offset] & 0x3F;
            const std::string language = decode_language(data[offset + 1]);
            const auto current = subchannel_languages.find(subchannel);

            if (current == subchannel_languages.end() || current->second != language) {
                subchannel_languages[subchannel] = language;
                update_subchannel(subchannel);
            }

            for (auto &[component_key, component]: service_components) {
                if (component.subchannel != subchannel || component.language == language) {
                    continue;
                }

                component.language = language;
                update_service_component(component_key);
            }
        }

        offset += 2;
    }
}

#if DAB_ENABLE_SERVICE_LINKING
void FICDecoder::process_fig0_6(const uint8_t *data, const size_t length, const bool pd, const bool continuation) {
    size_t offset = 0;
    std::map<uint32_t, std::vector<ServiceLink>> decoded_links;
    std::map<uint32_t, std::vector<uint8_t>> starts;
    std::map<uint32_t, bool> actuators;
    std::set<uint32_t> change_events;

    while (offset < length) {
        if (offset + 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const size_t entry_start = offset;
        const uint16_t header = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const bool identifier_list = (header & 0x8000) != 0;
        const bool active = (header & 0x4000) != 0;
        const bool hard = (header & 0x2000) != 0;
        const bool international = (header & 0x1000) != 0;
        const uint16_t linkage_set = header & 0x0FFF;
        const uint32_t database_key = static_cast<uint32_t>(pd) << 31 | static_cast<uint32_t>(hard) << 30 | static_cast<uint32_t>(international) << 29 | linkage_set;
        offset += 2;
#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Linkage[0x{:03X}].", linkage_set);
        inspect_field(prefix + "Active", std::to_string(active));
        inspect_field(prefix + "Hard", std::to_string(hard));
        inspect_field(prefix + "International", std::to_string(international));
        inspect_field(prefix + "Identifier list", std::to_string(identifier_list));
#endif


        if (!identifier_list) {
            if (!continuation) {
                change_events.insert(database_key);
            } else {
                actuators[database_key] = active;
            }
            continue;
        }

        if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const uint8_t list_header = data[offset++];
        const uint8_t identifier_list_qualifier = list_header >> 5 & 0x03;
        const size_t identifier_count = list_header & 0x0F;
        const size_t identifier_length = pd ? 4 : international ? 3 : 2;
        const size_t maximum_identifiers = pd ? 6 : international ? 8 : 12;

        if (identifier_count > maximum_identifiers || offset + identifier_count * identifier_length > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        if (linkage_set == 0 || identifier_count == 0 || (list_header & 0x80) != 0 || identifier_list_qualifier == 2 || (identifier_list_qualifier == 3 && !international)) {
            offset += identifier_count * identifier_length;
            continue;
        }

        std::vector<ServiceLink> &links = decoded_links[database_key];
        actuators[database_key] = active;

        for (size_t index = 0; index < identifier_count; index++) {
            const bool key_service = !continuation && index == 0;
            ServiceLink link;
            link.linkage_set = linkage_set;
            link.active = active;
            link.hard = hard;
            link.international = international;
            link.type = key_service || identifier_list_qualifier == 0 ? "DAB" : identifier_list_qualifier == 1 ? "FM-RDS" : pd ? "DRM" : "DRM/AMSS";

            if (international && !pd) {
                link.ecc = data[offset++];
            }

            for (size_t byte = international && !pd ? 1 : 0; byte < identifier_length; byte++) {
                link.id = link.id << 8 | data[offset++];
            }

            if (!key_service && identifier_list_qualifier == 3 && !pd) {
                link.id |= static_cast<uint32_t>(link.ecc) << 16;
                link.ecc = 0;
            }
#if DAB_ENABLE_DIAGNOSTICS
            const auto identifier_prefix = prefix + std::format("Identifier[{}].", index);
            inspect_field(identifier_prefix + "System", link.type);
            inspect_field(identifier_prefix + "ID", std::format("0x{:X}", link.id));
            inspect_field(identifier_prefix + "ECC", std::format("0x{:02X}", link.ecc));
#endif
            links.push_back(std::move(link));
        }

        if (!continuation) {
            std::vector<uint8_t> entry(data + entry_start, data + offset);
            entry[0] &= 0xBF;
            std::vector<uint8_t> &start = starts[database_key];
            start.insert(start.end(), entry.begin(), entry.end());
        }
    }

    for (const uint32_t database_key: change_events) {
        service_links.erase(database_key);
        service_link_starts.erase(database_key);
    }

    for (auto &[database_key, decoded]: decoded_links) {
        if (continuation) {
            if (!service_link_starts.contains(database_key)) {
                continue;
            }
        } else if (!service_link_starts.contains(database_key) || service_link_starts[database_key] != starts[database_key]) {
            service_link_starts[database_key] = std::move(starts[database_key]);
            service_links.erase(database_key);
        }

        std::vector<ServiceLink> &links = service_links[database_key];

        for (ServiceLink &link: decoded) {
            const auto current = std::ranges::find_if(links, [&link](const ServiceLink &entry) {
                return entry.type == link.type && entry.id == link.id && entry.ecc == link.ecc;
            });

            if (current == links.end()) {
                links.push_back(std::move(link));
            } else {
                *current = std::move(link);
            }
        }
    }

    for (const auto &[database_key, active]: actuators) {
        const auto current = service_links.find(database_key);

        if (current == service_links.end()) {
            continue;
        }

        for (ServiceLink &link: current->second) {
            link.active = active;
        }
    }
}
#endif


void FICDecoder::process_fig0_7(const uint8_t *data, const size_t length) {
    if (length < 2) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    const uint8_t service_count = data[0] >> 2;
    const uint16_t reconfiguration_counter = static_cast<uint16_t>((data[0] & 0x03) << 8 | data[1]);

#if DAB_ENABLE_DIAGNOSTICS
    inspect_field("Service count", std::to_string(service_count));
    inspect_field("Reconfiguration counter", std::to_string(reconfiguration_counter));
#endif
    if (configuration_valid && ensemble_status.reconfiguration_counter != reconfiguration_counter) {
        prepare_configuration_change();
    }

    configuration_valid = true;
    ensemble_status.service_count = service_count;
    ensemble_status.reconfiguration_counter = reconfiguration_counter;
    update_ensemble_status();

    if (configuration_cleanup_pending && ensemble_status.service_count != 0 && ensemble_status.services_found >= ensemble_status.service_count) {
        finish_configuration_change();
    }
}

void FICDecoder::process_fig0_8(const uint8_t *data, const size_t length, const bool pd) {
    size_t offset = 0;
    const size_t service_id_length = pd ? 4 : 2;
    std::map<uint64_t, ServiceComponent> decoded_components;

    while (offset < length) {
        if (offset + service_id_length + 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        uint32_t service_id = 0;

        for (size_t index = 0; index < service_id_length; index++) {
            service_id = service_id << 8 | data[offset + index];
        }

        offset += service_id_length;
        const bool extension_present = (data[offset] & 0x80) != 0;
        const uint8_t component_id = data[offset] & 0x0F;
        offset++;
        const uint64_t component_key = make_service_component_key(service_id, pd, component_id);
        ServiceComponent component;
        const auto current = service_components.find(component_key);

        if (current != service_components.end()) {
            component = current->second;
        }

        component.service_id = service_id;
        component.data_service = pd;
        component.id = component_id;

        if ((data[offset] & 0x80) == 0) {
            component.subchannel = data[offset] & 0x3F;
            component.component_id = 0xFFFF;
            offset++;
        } else {
            if (offset + 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            component.subchannel = 0xFF;
            component.component_id = static_cast<uint16_t>((data[offset] & 0x0F) << 8 | data[offset + 1]);
            offset += 2;
        }

        if (extension_present) {
            if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            offset++;
        }

#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Service[0x{:X}].SCIdS[{}].", service_id, component_id);
        inspect_field(prefix + "SubChId", component.subchannel == 0xFF ? "-" : std::to_string(component.subchannel));
        inspect_field(prefix + "SCId", component.component_id == 0xFFFF ? "-" : std::format("0x{:03X}", component.component_id));
        inspect_field(prefix + "Extension present", std::to_string(extension_present));
#endif
        decoded_components[component_key] = std::move(component);
    }

    for (auto &[component_key, component]: decoded_components) {
        service_components[component_key] = std::move(component);
        update_service_component(component_key);
    }
}

void FICDecoder::process_fig0_9(const uint8_t *data, const size_t length) {
    if (length < 3) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    const bool extended = (data[0] & 0x80) != 0;
    const uint8_t previous_ecc = ensemble_status.ecc;
    ensemble_status.lto_unique = (data[0] & 0x40) != 0;
    const int16_t local_time_offset = static_cast<int16_t>(data[0] & 0x1F) * 30;
    ensemble_status.local_time_offset = (data[0] & 0x20) != 0 ? static_cast<int16_t>(-local_time_offset) : local_time_offset;
    ensemble_status.ecc = data[1];
    ensemble_status.international_table_id = data[2];
    const uint8_t new_pty_table_id = data[2];
    const bool pty_changed = pty_table_id != new_pty_table_id;
    pty_table_id = new_pty_table_id;

#if DAB_ENABLE_DIAGNOSTICS
    inspect_field("ECC", std::format("0x{:02X}", ensemble_status.ecc));
    inspect_field("LTO minutes", std::to_string(ensemble_status.local_time_offset));
    inspect_field("LTO unique", std::to_string(ensemble_status.lto_unique));
    inspect_field("International table ID", std::to_string(new_pty_table_id));
    inspect_field("Extended", std::to_string(extended));
#endif
    if (extended) {
        size_t offset = 3;

        while (offset + 2 <= length) {
            const size_t service_count = data[offset] >> 6;
            const uint8_t ecc = data[offset + 1];
            offset += 2;

            if (offset + service_count * 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            for (size_t index = 0; index < service_count; index++) {
                const uint16_t service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
                offset += 2;
                ServiceState &service = services[service_id];
                service.id = service_id;

#if DAB_ENABLE_DIAGNOSTICS
                inspect_field(std::format("Service[0x{:04X}].ECC", service_id), std::format("0x{:02X}", ecc));
#endif
                if (service.ecc != ecc) {
                    service.ecc = ecc;
                    update_service(service_id);
                }
            }
        }
    }

    if (pty_changed) {
        for (const auto &[service_id, service]: services) {
            if (service.pty_static >= 0 || service.pty_dynamic >= 0) {
                update_service(service_id);
            }
        }
    }

    if (previous_ecc != ensemble_status.ecc) {
        for (const auto &[service_id, service]: services) {
            if (service.ecc == 0) {
                update_service(service_id);
            }
        }
    }

    update_ensemble_status();
}

void FICDecoder::process_fig0_10(const uint8_t *data, const size_t length) {
    if (length < 4) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    const int mjd = (data[0] & 0x7F) << 10 | data[1] << 2 | data[2] >> 6;
    const int year_base = static_cast<int>(std::floor((mjd - 15078.2) / 365.25));
    const int month_base = static_cast<int>(std::floor((mjd - 14956.1 - std::floor(year_base * 365.25)) / 30.6001));
    const int day = mjd - 14956 - static_cast<int>(std::floor(year_base * 365.25)) - static_cast<int>(std::floor(month_base * 30.6001));
    const int leap_adjustment = month_base == 14 || month_base == 15 ? 1 : 0;

    DateTime new_date_time;
    new_date_time.year = static_cast<uint16_t>(year_base + leap_adjustment + 1900);
    new_date_time.month = static_cast<uint8_t>(month_base - 1 - leap_adjustment * 12);
    new_date_time.day = static_cast<uint8_t>(day);
    new_date_time.hour = static_cast<uint8_t>((data[2] & 0x07) << 2 | data[3] >> 6);
    new_date_time.minute = data[3] & 0x3F;

    if ((data[2] & 0x08) != 0) {
        if (length < 6) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        new_date_time.second = data[4] >> 2;
        new_date_time.milliseconds = static_cast<uint16_t>((data[4] & 0x03) << 8 | data[5]);
    }

#if DAB_ENABLE_DIAGNOSTICS
    inspect_field("MJD", std::to_string(mjd));
    inspect_field("UTC", std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", new_date_time.year, new_date_time.month, new_date_time.day, new_date_time.hour, new_date_time.minute, new_date_time.second, new_date_time.milliseconds));
#endif
    if (date_time == new_date_time) {
        return;
    }

    date_time = new_date_time;

    if (date_time_callback) {
        date_time_callback(date_time);
    }
}

void FICDecoder::process_fig0_13(const uint8_t *data, const size_t length, const bool pd) {
    size_t offset = 0;
    const size_t service_id_length = pd ? 4 : 2;
    std::map<uint64_t, ServiceComponent> decoded_components;

    while (offset < length) {
        if (offset + service_id_length + 1 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        uint32_t service_id = 0;

        for (size_t index = 0; index < service_id_length; index++) {
            service_id = service_id << 8 | data[offset + index];
        }

        offset += service_id_length;
        const uint8_t component_id = data[offset] >> 4;
        const uint8_t application_count = data[offset] & 0x0F;
        offset++;

        if (application_count == 0 || application_count > 6) {
            return;
        }

        const uint64_t component_key = make_service_component_key(service_id, pd, component_id);
        ServiceComponent &component = decoded_components[component_key];
        component.service_id = service_id;
        component.data_service = pd;
        component.id = component_id;
        component.user_applications.clear();

        for (uint8_t index = 0; index < application_count; index++) {
            if (offset + 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            UserApplication application;
            application.type = static_cast<uint16_t>(data[offset] << 3 | data[offset + 1] >> 5);
            application.name = decode_user_application(application.type);
            const size_t application_data_length = data[offset + 1] & 0x1F;
            offset += 2;

            if (application_data_length > 23 || offset + application_data_length > length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            application.data.assign(data + offset, data + offset + application_data_length);
#if DAB_ENABLE_DIAGNOSTICS
            const auto prefix = std::format("Service[0x{:X}].SCIdS[{}].Application[{}].", service_id, component_id, index);
            inspect_field(prefix + "Type", std::format("0x{:03X}", application.type));
            inspect_field(prefix + "Name", application.name);
            inspect_field(prefix + "Data bytes", std::to_string(application_data_length));
#endif
            component.user_applications.push_back(std::move(application));
            offset += application_data_length;
        }
    }

    for (auto &[component_key, decoded_component]: decoded_components) {
        ServiceComponent &component = service_components[component_key];
        component.service_id = decoded_component.service_id;
        component.data_service = decoded_component.data_service;
        component.id = decoded_component.id;
        component.user_applications = std::move(decoded_component.user_applications);
        update_service_component(component_key);
    }
}

void FICDecoder::process_fig0_14(const uint8_t *data, const size_t length) {
    for (size_t offset = 0; offset < length; offset++) {
        const uint8_t subchannel = data[offset] >> 2;
        const uint8_t fec_scheme = data[offset] & 0x03;
#if DAB_ENABLE_DIAGNOSTICS
        inspect_field(std::format("SubCh[{}].FEC scheme", subchannel), std::to_string(fec_scheme));
#endif
        subchannel_fec_schemes[subchannel] = fec_scheme;

        for (auto &[component_key, component]: service_components) {
            if (component.subchannel != subchannel || component.fec_scheme == fec_scheme) {
                continue;
            }

            component.fec_scheme = fec_scheme;
            update_service_component(component_key);
        }
    }
}

#if DAB_ENABLE_EWS
void FICDecoder::process_fig0_15(const uint8_t *data, const size_t length, const bool other_ensemble, const bool continuation) {
    if (length == 0) {

#if DAB_ENABLE_DIAGNOSTICS
        inspect_field("Support signalling", std::to_string(!other_ensemble && continuation));
#endif
        if (!other_ensemble && continuation) {
            emergency_warning_supported = true;
            emergency_warnings.clear();
            emergency_warning_sequence++;
        }

        return;
    }

    EmergencyWarning alert;
    alert.other_ensemble = other_ensemble;
    size_t offset = 0;
    uint32_t key = 0;

    if (other_ensemble) {
        if (length < 3) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        alert.ensemble_id = static_cast<uint16_t>(data[0] << 8 | data[1]);
        alert.phase = "Trigger";
        alert.status_present = true;
        offset = 2;
        key = 0x10000 | alert.ensemble_id;
    } else {
        const uint8_t phase = data[0] >> 6;
        alert.subchannel = data[0] & 0x3F;
        alert.phase = decode_emergency_warning_phase(phase);
        offset = 1;
        key = alert.subchannel;

        if (phase == 0) {
            if (offset >= length || (data[offset] & 0xC0) != 0) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            alert.trigger_second_present = true;
            alert.trigger_second = data[offset] & 0x3F;
            offset++;
        }

        alert.status_present = phase <= 1;

        if (!alert.status_present) {
            if (offset != length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            const auto current = emergency_warnings.find(key);

            if (current != emergency_warnings.end()) {
                alert = current->second;
                alert.phase = decode_emergency_warning_phase(phase);
                alert.trigger_second_present = false;
                alert.trigger_second = 0;
                alert.status_present = false;
                alert.last = false;
            }

            emergency_warning_supported = true;

#if DAB_ENABLE_DIAGNOSTICS
            inspect_field("SubChId", std::to_string(alert.subchannel));
            inspect_field("Phase", alert.phase);
            inspect_field("Status present", "0");
#endif
            emergency_warnings[key] = std::move(alert);
            emergency_warning_sequence++;
            return;
        }
    }

    if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    const uint8_t status = data[offset++];
    alert.last = (status & 0x80) != 0;
    alert.stage = decode_emergency_warning_stage((status >> 4) & 0x07);
    alert.incident_id = status & 0x0F;

    if (!decode_emergency_warning_locations(data + offset, length - offset, alert.locations)) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }
#if DAB_ENABLE_DIAGNOSTICS
    inspect_field("Ensemble ID", std::format("0x{:04X}", alert.ensemble_id));
    inspect_field("SubChId", std::to_string(alert.subchannel));
    inspect_field("Phase", alert.phase);
    inspect_field("Stage", alert.stage);
    inspect_field("Incident ID", std::to_string(alert.incident_id));
    inspect_field("Last", std::to_string(alert.last));
    inspect_field("Trigger second", alert.trigger_second_present ? std::to_string(alert.trigger_second) : "-");
    for (size_t index = 0; index < alert.locations.size(); index++) {
        const auto prefix = std::format("Location[{}].", index);
        inspect_field(prefix + "NFF", std::to_string(alert.locations[index].nff));
        inspect_field(prefix + "Zone", std::to_string(alert.locations[index].zone));
    }
#endif
    emergency_warning_supported = true;

    if (continuation) {
        const auto current = emergency_warnings.find(key);

        if (current != emergency_warnings.end() && current->second.stage == alert.stage && current->second.incident_id == alert.incident_id) {
            EmergencyWarning merged = current->second;
            merged.last = alert.last;

            for (const auto &location: alert.locations) {
                if (std::find(merged.locations.begin(), merged.locations.end(), location) == merged.locations.end()) {
                    merged.locations.push_back(location);
                }
            }

            emergency_warnings[key] = std::move(merged);
        } else {
            emergency_warnings[key] = std::move(alert);
        }
    } else {
        emergency_warnings[key] = std::move(alert);
    }

    emergency_warning_sequence++;
}
#endif


void FICDecoder::process_fig0_17(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 4 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const uint16_t service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const uint8_t flags = data[offset + 2];
        const bool dynamic = (flags & 0x80) != 0;
        const bool language_present = (flags & 0x20) != 0;
        const bool country_code_present = (flags & 0x10) != 0;
        offset += 3;

        if (language_present) {
            if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            offset++;
        }

        if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const int16_t pty = data[offset] & 0x1F;
        offset++;

        if (country_code_present) {
            if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            offset++;
        }

#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Service[0x{:04X}].", service_id);
        inspect_field(prefix + "Dynamic PTY", std::to_string(dynamic));
        inspect_field(prefix + "PTY code", std::to_string(pty));
        inspect_field(prefix + "PTY", decode_pty(static_cast<uint8_t>(pty), pty_table_id));
#endif
        ServiceState &service = services[service_id];
        service.id = service_id;
        int16_t &current_pty = dynamic ? service.pty_dynamic : service.pty_static;

        if (current_pty != pty) {
            current_pty = pty;
            update_service(service_id);
        }
    }
}

#if DAB_ENABLE_ANNOUNCEMENTS
void FICDecoder::process_fig0_18(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 5 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const uint16_t service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const uint16_t flags = static_cast<uint16_t>(data[offset + 2] << 8 | data[offset + 3]);
        const size_t cluster_count = data[offset + 4] & 0x1F;
        offset += 5;

        if (offset + cluster_count > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        std::set<uint8_t> clusters;

        for (size_t index = 0; index < cluster_count; index++) {
            clusters.insert(data[offset++]);
        }

#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Service[0x{:04X}].", service_id);
        inspect_field(prefix + "Announcement flags", std::format("0x{:04X}", flags));
        inspect_field(prefix + "Cluster count", std::to_string(cluster_count));
#endif
        ServiceState &service = services[service_id];
        service.id = service_id;

        if (service.announcement_support_flags != flags || service.announcement_clusters != clusters) {
            service.announcement_support_flags = flags;
            service.announcement_clusters = std::move(clusters);
            update_service(service_id);
        }
    }
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
void FICDecoder::process_fig0_19(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 4 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const uint8_t cluster_id = data[offset];
        const uint16_t flags = static_cast<uint16_t>(data[offset + 1] << 8 | data[offset + 2]);
        const bool region_present = (data[offset + 3] & 0x40) != 0;
        const uint8_t subchannel = data[offset + 3] & 0x3F;
        const size_t entry_length = region_present ? 5 : 4;

        if (offset + entry_length > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        offset += entry_length;
#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Cluster[{}].", cluster_id);
        inspect_field(prefix + "Flags", std::format("0x{:04X}", flags));
        inspect_field(prefix + "SubChId", std::to_string(subchannel));
        inspect_field(prefix + "Region present", std::to_string(region_present));
#endif

        const AnnouncementCluster cluster{flags, subchannel};
        const auto current = announcement_clusters.find(cluster_id);

        if (current != announcement_clusters.end() && current->second == cluster) {
            continue;
        }

        announcement_clusters[cluster_id] = cluster;
        update_alarm();

        for (const auto &[service_id, service]: services) {
            if (service.announcement_clusters.contains(cluster_id)) {
                update_service(service_id);
            }
        }
    }
}
#endif


void FICDecoder::process_fig0_20(const uint8_t *data, const size_t length, const bool pd) {
    size_t offset = 0;
    const size_t service_id_length = pd ? 4 : 2;

    while (offset < length) {
        if (offset + service_id_length + 4 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        uint32_t service_id = 0;

        for (size_t index = 0; index < service_id_length; index++) {
            service_id = service_id << 8 | data[offset + index];
        }

        offset += service_id_length;
        const uint8_t component_id = data[offset] >> 4;
        const uint8_t change_flags = data[offset] >> 2 & 0x03;
        const bool part_time = (data[offset] & 0x02) != 0;
        const bool component_description_present = (data[offset] & 0x01) != 0;
        offset++;
        const uint64_t component_key = make_service_component_key(service_id, pd, component_id);
        ServiceComponent &component = service_components[component_key];
        component.service_id = service_id;
        component.data_service = pd;
        component.id = component_id;
        component.change_flags = change_flags;
        component.part_time = part_time;

        if (component_description_present) {
            if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            component.conditional_access = (data[offset] & 0x80) != 0;
            component.data = (data[offset] & 0x40) != 0;
            component.type = data[offset] & 0x3F;
            offset++;
        }

        if (offset + 3 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const uint32_t change_time = static_cast<uint32_t>(data[offset] << 16 | data[offset + 1] << 8 | data[offset + 2]);
        component.change_day = static_cast<uint8_t>(change_time >> 19 & 0x1F);
        component.change_hour = static_cast<uint8_t>(change_time >> 14 & 0x1F);
        component.change_minute = static_cast<uint8_t>(change_time >> 8 & 0x3F);
        component.change_second = static_cast<uint8_t>(change_time >> 2 & 0x3F);
        const bool transfer_service_present = (change_time & 0x02) != 0;
        const bool transfer_ensemble_present = (change_time & 0x01) != 0;
        offset += 3;
        component.transfer_service_id = 0;
        component.transfer_ensemble_id = 0;

        if (transfer_service_present) {
            if (offset + service_id_length > length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            for (size_t index = 0; index < service_id_length; index++) {
                component.transfer_service_id = component.transfer_service_id << 8 | data[offset + index];
            }

            offset += service_id_length;
        }

        if (transfer_ensemble_present) {
            if (offset + 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            component.transfer_ensemble_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
            offset += 2;
        }
#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Service[0x{:X}].SCIdS[{}].", service_id, component_id);
        inspect_field(prefix + "Change flags", std::to_string(change_flags));
        inspect_field(prefix + "Part time", std::to_string(part_time));
        inspect_field(prefix + "Description present", std::to_string(component_description_present));
        if (component_description_present) {
            inspect_field(prefix + "CA", std::to_string(component.conditional_access));
            inspect_field(prefix + "Data", std::to_string(component.data));
            inspect_field(prefix + "Type", std::to_string(component.type));
        }
        inspect_field(prefix + "Change time", std::format("{} {:02}:{:02}:{:02}", component.change_day, component.change_hour, component.change_minute, component.change_second));
        inspect_field(prefix + "Transfer service", std::format("0x{:X}", component.transfer_service_id));
        inspect_field(prefix + "Transfer ensemble", std::format("0x{:04X}", component.transfer_ensemble_id));
#endif
        update_service_component(component_key);
    }
}

#if DAB_ENABLE_FREQUENCY_INFO
void FICDecoder::process_fig0_21(const uint8_t *data, const size_t length, const bool other_ensemble, const bool continuation) {
    size_t offset = 0;
    std::map<uint64_t, std::vector<FrequencyInformation>> decoded_frequencies;
    std::map<uint64_t, std::vector<uint8_t>> starts;
    std::set<uint64_t> change_events;

    while (offset < length) {
        if (offset + 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const uint16_t information_header = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const uint16_t rfa = information_header >> 5;
        const size_t information_length = information_header & 0x1F;
        offset += 2;

        if (information_length > 26 || offset + information_length > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const size_t information_end = offset + information_length;

        while (offset < information_end) {
            if (offset + 3 > information_end) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            const size_t entry_start = offset;
            const uint16_t id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
            const uint8_t range_modulation = data[offset + 2] >> 4;
            const bool continuous = (data[offset + 2] & 0x08) != 0 && (range_modulation == 0 || !other_ensemble);
            const size_t frequency_length = data[offset + 2] & 0x07;
            offset += 3;

            if (offset + frequency_length > information_end) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            const uint64_t database_key = static_cast<uint64_t>(other_ensemble) << 63 | static_cast<uint64_t>(rfa) << 32 | static_cast<uint64_t>(id) << 16 | range_modulation;
#if DAB_ENABLE_DIAGNOSTICS
            const auto prefix = std::format("Frequency[{}:0x{:04X}:{}].", rfa, id, range_modulation);
            inspect_field(prefix + "RFA", std::to_string(rfa));
            inspect_field(prefix + "ID", std::format("0x{:04X}", id));
            inspect_field(prefix + "Range/modulation", std::to_string(range_modulation));
            inspect_field(prefix + "Continuous", std::to_string(continuous));
            inspect_field(prefix + "List bytes", std::to_string(frequency_length));
#endif
            if (frequency_length == 0) {
                if (!continuation) {
                    change_events.insert(database_key);
                }
                continue;
            }

            if (range_modulation != 0 && range_modulation != 6 && range_modulation != 8 && range_modulation != 14) {
                offset += frequency_length;
                continue;
            }

            if ((range_modulation == 0 && frequency_length % 3 != 0) || ((range_modulation == 6 || range_modulation == 14) && (frequency_length < 3 || frequency_length % 2 != 1))) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            std::vector<FrequencyInformation> &frequencies = decoded_frequencies[database_key];

            if (range_modulation == 0) {
                for (size_t index = 0; index < frequency_length; index += 3) {
                    const uint32_t value = static_cast<uint32_t>(data[offset + index] << 16 | data[offset + index + 1] << 8 | data[offset + index + 2]);
                    const uint8_t control = static_cast<uint8_t>(value >> 19);

                    if ((value & 0x7FFFF) != 0 && control <= 3) {
                        frequencies.push_back({other_ensemble, id, "DAB", continuous, (value & 0x7FFFF) * 16, control});
                    }
                }
            } else if (range_modulation == 8) {
                for (size_t index = 0; index < frequency_length; index++) {
                    const uint8_t value = data[offset + index];

                    if (value != 0 && value <= 204) {
                        frequencies.push_back({other_ensemble, id, "FM-RDS", continuous, static_cast<uint32_t>(87500 + value * 100), 0});
                    }
                }
            } else {
                const uint8_t identifier_high = data[offset];

                for (size_t index = 1; index < frequency_length; index += 2) {
                    const uint16_t value = static_cast<uint16_t>(data[offset + index] << 8 | data[offset + index + 1]);
                    const uint32_t multiplier = range_modulation == 6 && (value & 0x8000) != 0 ? 10 : 1;

                    if ((value & 0x7FFF) != 0 && (range_modulation == 6 || (value & 0x8000) == 0)) {
                        frequencies.push_back({other_ensemble, id, range_modulation == 6 ? "DRM" : "AMSS", continuous, static_cast<uint32_t>(value & 0x7FFF) * multiplier, identifier_high});
                    }
                }
            }
#if DAB_ENABLE_DIAGNOSTICS
            for (size_t index = 0; index < frequencies.size(); index++) {
                const auto entry_prefix = prefix + std::format("Entry[{}].", index);
                inspect_field(entry_prefix + "System", frequencies[index].type);
                inspect_field(entry_prefix + "Frequency kHz", std::to_string(frequencies[index].frequency));
                inspect_field(entry_prefix + "Control", std::to_string(frequencies[index].control));
            }
#endif
            offset += frequency_length;

            if (!continuation) {
                std::vector<uint8_t> &start = starts[database_key];
                start.insert(start.end(), data + entry_start, data + offset);
            }
        }
    }

    for (const uint64_t database_key: change_events) {
        frequency_information.erase(database_key);
        frequency_information_starts.erase(database_key);
    }

    for (auto &[database_key, decoded]: decoded_frequencies) {
        if (continuation) {
            if (!frequency_information_starts.contains(database_key)) {
                continue;
            }
        } else if (!frequency_information_starts.contains(database_key) || frequency_information_starts[database_key] != starts[database_key]) {
            frequency_information_starts[database_key] = std::move(starts[database_key]);
            frequency_information.erase(database_key);
        }

        std::vector<FrequencyInformation> &frequencies = frequency_information[database_key];

        for (FrequencyInformation &frequency: decoded) {
            const auto current = std::ranges::find_if(frequencies, [&frequency](const FrequencyInformation &entry) {
                return entry.frequency == frequency.frequency && (frequency.type == "DAB" || frequency.type == "FM-RDS" || entry.control == frequency.control);
            });

            if (current == frequencies.end()) {
                frequencies.push_back(std::move(frequency));
            } else {
                *current = std::move(frequency);
            }
        }
    }
}
#endif


#if DAB_ENABLE_SERVICE_LINKING
void FICDecoder::process_fig0_24(const uint8_t *data, const size_t length, const bool pd, const bool other_ensemble, const bool continuation) {
    size_t offset = 0;
    const size_t service_id_length = pd ? 4 : 2;
    std::map<uint64_t, OtherEnsembleService> decoded_services;
    std::map<uint64_t, std::vector<uint8_t>> starts;
    std::set<uint64_t> change_events;

    while (offset < length) {
        if (offset + service_id_length + 1 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const size_t entry_start = offset;
        uint32_t service_id = 0;

        for (size_t index = 0; index < service_id_length; index++) {
            service_id = service_id << 8 | data[offset++];
        }

        const uint8_t service_header = data[offset++];
        const size_t ensemble_count = service_header & 0x0F;
        const size_t maximum_ensembles = pd ? 11 : 12;

        if (ensemble_count > maximum_ensembles || offset + ensemble_count * 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        const uint64_t database_key = static_cast<uint64_t>(other_ensemble) << 63 | static_cast<uint64_t>(pd) << 32 | service_id;
#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Service[0x{:X}].", service_id);
        inspect_field(prefix + "CA ID", std::to_string(service_header >> 4 & 0x07));
        inspect_field(prefix + "Ensemble count", std::to_string(ensemble_count));
#endif
        if (ensemble_count == 0) {
            if (!continuation) {
                change_events.insert(database_key);
            }
            continue;
        }

        OtherEnsembleService &service = decoded_services[database_key];
        service.service_id = service_id;
        service.data_service = pd;
        service.conditional_access_id = service_header >> 4 & 0x07;

        for (size_t index = 0; index < ensemble_count; index++) {
            service.ensemble_ids.push_back(static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]));
#if DAB_ENABLE_DIAGNOSTICS
            inspect_field(prefix + std::format("Ensemble[{}]", index), std::format("0x{:04X}", service.ensemble_ids.back()));
#endif

            offset += 2;
        }

        if (!continuation) {
            std::vector<uint8_t> &start = starts[database_key];
            start.insert(start.end(), data + entry_start, data + offset);
        }
    }

    for (const uint64_t database_key: change_events) {
        other_ensemble_services.erase(database_key);
        other_ensemble_service_starts.erase(database_key);
    }

    for (const auto &[database_key, decoded]: decoded_services) {
        if (continuation) {
            if (!other_ensemble_service_starts.contains(database_key)) {
                continue;
            }
        } else if (!other_ensemble_service_starts.contains(database_key) || other_ensemble_service_starts[database_key] != starts[database_key]) {
            other_ensemble_service_starts[database_key] = std::move(starts[database_key]);
            other_ensemble_services.erase(database_key);
        }

        OtherEnsembleService &service = other_ensemble_services[database_key];
        service.service_id = decoded.service_id;
        service.data_service = decoded.data_service;
        service.conditional_access_id = decoded.conditional_access_id;

        for (const uint16_t ensemble_id: decoded.ensemble_ids) {
            if (std::ranges::find(service.ensemble_ids, ensemble_id) == service.ensemble_ids.end()) {
                service.ensemble_ids.push_back(ensemble_id);
            }
        }
    }
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
void FICDecoder::process_fig0_25(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset + 5 <= length) {
        const uint16_t service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const uint16_t flags = static_cast<uint16_t>(data[offset + 2] << 8 | data[offset + 3]);
        const size_t target_count = (data[offset + 4] & 0x03) + 1;
        offset += 5;

        if (offset + target_count * 3 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        for (size_t index = 0; index < target_count; index++) {
            OEAnnouncementSupport entry;
            entry.service_id = service_id;
            entry.announcements = decode_announcement_flags(flags);
            entry.cluster_id = data[offset];
            entry.ensemble_id = static_cast<uint16_t>(data[offset + 1] << 8 | data[offset + 2]);
            offset += 3;
            const uint64_t key = static_cast<uint64_t>(entry.service_id) << 24 | static_cast<uint64_t>(entry.cluster_id) << 16 | entry.ensemble_id;

#if DAB_ENABLE_DIAGNOSTICS
            const auto prefix = std::format("Service[0x{:04X}].Target[{}].", service_id, index);
            inspect_field(prefix + "Flags", std::format("0x{:04X}", flags));
            inspect_field(prefix + "Cluster ID", std::to_string(entry.cluster_id));
            inspect_field(prefix + "Ensemble ID", std::format("0x{:04X}", entry.ensemble_id));
#endif
            oe_announcement_support[key] = std::move(entry);
        }
    }
#if DAB_ENABLE_DIAGNOSTICS
    if (offset != length) {
        inspect_error("Incomplete FIG entry");
    }
#endif
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
void FICDecoder::process_fig0_26(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset + 6 <= length) {
        OEAnnouncementSwitch announcement;
        announcement.cluster_id = data[offset];
        const uint16_t flags = static_cast<uint16_t>(data[offset + 1] << 8 | data[offset + 2]);
        announcement.announcements = decode_announcement_flags(flags);
        announcement.newly_introduced = (data[offset + 3] & 0x80) != 0;
        announcement.component_id = data[offset + 3] & 0x0F;
        announcement.service_id = static_cast<uint16_t>(data[offset + 4] << 8 | data[offset + 5]);
        offset += 6;
#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Cluster[{}].", announcement.cluster_id);
        inspect_field(prefix + "Flags", std::format("0x{:04X}", flags));
        inspect_field(prefix + "New", std::to_string(announcement.newly_introduced));
        inspect_field(prefix + "SCIdS", std::to_string(announcement.component_id));
        inspect_field(prefix + "Service ID", std::format("0x{:04X}", announcement.service_id));
#endif


        if (flags == 0) {
            oe_announcement_switches.erase(announcement.cluster_id);
        } else {
            oe_announcement_switches[announcement.cluster_id] = std::move(announcement);
        }
    }
#if DAB_ENABLE_DIAGNOSTICS
    if (offset != length) {
        inspect_error("Incomplete FIG entry");
    }
#endif
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
void FICDecoder::process_fig0_27(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset + 3 <= length) {
        FMAnnouncementSupport support;
        support.service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const size_t pi_count = data[offset + 2] & 0x0F;
        offset += 3;

        if (offset + pi_count * 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        for (size_t index = 0; index < pi_count; index++) {
            support.pi_codes.push_back(static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]));
            offset += 2;
        }
#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Service[0x{:04X}].", support.service_id);
        inspect_field(prefix + "PI count", std::to_string(pi_count));
        for (size_t index = 0; index < support.pi_codes.size(); index++) {
            inspect_field(prefix + std::format("PI[{}]", index), std::format("0x{:04X}", support.pi_codes[index]));
        }
#endif
        if (pi_count == 0) {
            fm_announcement_support.erase(support.service_id);
        } else {
            fm_announcement_support[support.service_id] = std::move(support);
        }
    }
#if DAB_ENABLE_DIAGNOSTICS
    if (offset != length) {
        inspect_error("Incomplete FIG entry");
    }
#endif
}
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
void FICDecoder::process_fig0_28(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset + 4 <= length) {
        FMAnnouncementSwitch announcement;
        announcement.cluster_id = data[offset];
        announcement.newly_introduced = (data[offset + 1] & 0x80) != 0;
        announcement.region_id = data[offset + 1] & 0x3F;
        announcement.pi_code = static_cast<uint16_t>(data[offset + 2] << 8 | data[offset + 3]);
        offset += 4;
#if DAB_ENABLE_DIAGNOSTICS
        const auto prefix = std::format("Cluster[{}].", announcement.cluster_id);
        inspect_field(prefix + "New", std::to_string(announcement.newly_introduced));
        inspect_field(prefix + "Region ID", std::to_string(announcement.region_id));
        inspect_field(prefix + "PI", std::format("0x{:04X}", announcement.pi_code));
#endif

        fm_announcement_switches[announcement.cluster_id] = announcement;
    }
#if DAB_ENABLE_DIAGNOSTICS
    if (offset != length) {
        inspect_error("Incomplete FIG entry");
    }
#endif
}
#endif


void FICDecoder::process_fig1(const uint8_t *data, const size_t length) {
    if (length < 1) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    if ((data[0] & 0x08) != 0) {
#if DAB_ENABLE_DIAGNOSTICS
        if (current_figure) {
            current_figure->status = "Other ensemble";
        }
#endif
        return;
    }

    const uint8_t charset = data[0] >> 4;
    const uint8_t extension = data[0] & 0x07;
    uint64_t label_key = static_cast<uint64_t>(extension) << 60;
    size_t offset = 1;
    size_t identifier_length = extension == 5 ? 4 : 2;
    bool data_service = extension == 5;
    uint8_t component_id = 0;

    if (extension == 4 || extension == 6) {
        if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        data_service = (data[offset] & 0x80) != 0;
        component_id = data[offset++] & 0x0F;
        identifier_length = data_service ? 4 : 2;
    } else if (extension != 0 && extension != 1 && extension != 5) {
        return;
    }

    if (offset + identifier_length > length) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    uint32_t identifier = 0;

    for (size_t index = 0; index < identifier_length; index++) {
        identifier = identifier << 8 | data[offset++];
    }

    if (extension == 4 || extension == 6) {
        const uint64_t component_key = make_service_component_key(identifier, data_service, component_id);
        label_key |= extension == 6 ? component_key << 8 : component_key;

        if (extension == 6) {
            if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
                inspect_error("Incomplete or invalid FIG payload");
#endif
                return;
            }

            label_key |= data[offset++] & 0x1F;
        }
    } else {
        label_key |= identifier;
    }

    if (offset + 18 != length) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    const std::string label = decode_label(data + offset, 16, charset);

    if (label.empty()) {
        return;
    }

    const uint16_t character_flags = static_cast<uint16_t>(data[offset + 16] << 8 | data[offset + 17]);
#if DAB_ENABLE_DIAGNOSTICS
    const auto prefix = std::format("Label[0x{:X}].", label_key);
    inspect_field(prefix + "Charset", std::to_string(charset));
    inspect_field(prefix + "Identifier", std::format("0x{:X}", identifier));
    inspect_field(prefix + "Character flags", std::format("0x{:04X}", character_flags));
    inspect_field(prefix + "Text", label);
    inspect_field(prefix + "Short text", decode_short_label(data + offset, 16, charset, character_flags));
#endif
    basic_labels[label_key] = {label, decode_short_label(data + offset, 16, charset, character_flags)};
    update_label(label_key);
}

#if DAB_ENABLE_FIDC
void FICDecoder::process_fig5(const uint8_t *data, const size_t length) {
    if (length < 1) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    FIDCEntry entry;
    entry.d1 = (data[0] & 0x80) != 0;
    entry.d2 = (data[0] & 0x40) != 0;
    entry.component_id = (data[0] >> 3) & 0x07;
    entry.extension = data[0] & 0x07;
    entry.type = decode_fidc_type(entry.extension);
    entry.data.assign(data + 1, data + length);
#if DAB_ENABLE_DIAGNOSTICS
    statistics.fig5_extensions[entry.extension]++;
#endif
#if DAB_ENABLE_DIAGNOSTICS
    inspect_field("D1", std::to_string(entry.d1));
    inspect_field("D2", std::to_string(entry.d2));
    inspect_field("TCId", std::to_string(entry.component_id));
    inspect_field("FIDC type", entry.type);
#endif
    const uint16_t key = static_cast<uint16_t>(entry.extension << 3 | entry.component_id);
    fidc_entries[key] = std::move(entry);
}
#endif


#if DAB_ENABLE_EWS
bool FICDecoder::decode_emergency_warning_locations(const uint8_t *data, const size_t length, std::vector<EmergencyWarningLocation> &locations) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 2 > length) {
            return false;
        }

        EmergencyWarningLocation location;
        location.nff = data[offset] >> 6;
        location.zone = data[offset] & 0x3F;
        offset++;

        location.sub_codes = (data[offset] & 0x80) != 0;
        const uint8_t num_digits = (data[offset] >> 4) & 0x07;
        location.digits.push_back(data[offset] & 0x0F);
        offset++;

        if (num_digits > (location.sub_codes ? 4 : 5)) {
            return false;
        }

        const size_t digit_bytes = (num_digits + 1) / 2;

        if (offset + digit_bytes + (location.sub_codes ? 2 : 0) > length) {
            return false;
        }

        for (uint8_t index = 0; index < num_digits; index++) {
            const uint8_t value = data[offset + index / 2];
            location.digits.push_back(index % 2 == 0 ? value >> 4 : value & 0x0F);
        }

        if (num_digits % 2 != 0 && (data[offset + digit_bytes - 1] & 0x0F) != 0) {
            return false;
        }

        offset += digit_bytes;

        if (location.sub_codes) {
            location.sub_code_mask = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
            offset += 2;
        }

        locations.push_back(std::move(location));
    }

    return true;
}
#endif


#if DAB_ENABLE_EWS
std::string FICDecoder::decode_emergency_warning_phase(const uint8_t phase) {
    static const std::array<std::string, 4> PHASES{
        "Pre-trigger",
        "Trigger",
        "Sustain",
        "End"
    };

    return phase < PHASES.size() ? PHASES[phase] : "Unknown";
}
#endif


#if DAB_ENABLE_EWS
std::string FICDecoder::decode_emergency_warning_stage(const uint8_t stage) {
    static const std::array<std::string, 8> STAGES{
        "Level 1 Start",
        "Level 1 Update",
        "Level 1 Repeat",
        "Level 1 Critical",
        "Level 2 Start",
        "Level 2 Update",
        "Level 2 Repeat",
        "Test"
    };

    return stage < STAGES.size() ? STAGES[stage] : "Unknown";
}
#endif


#if DAB_ENABLE_FIDC
std::string FICDecoder::decode_fidc_type(const uint8_t extension) {
    static const std::array<std::string, 8> TYPES{
        "Paging",
        "TMC",
        "EWS",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved"
    };

    return extension < TYPES.size() ? TYPES[extension] : "Reserved";
}
#endif


void FICDecoder::process_fig2(const uint8_t *data, const size_t length) {
    if (length < 1) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    const bool toggle = (data[0] & 0x80) != 0;
    const uint8_t segment_index = data[0] >> 4 & 0x07;
    const uint8_t extension = data[0] & 0x07;
    const bool version = (data[0] & 0x08) != 0;

    if (segment_index > 1) {
        return;
    }

    size_t offset = 1;
    uint64_t label_key = static_cast<uint64_t>(extension) << 60;
    uint32_t service_id = 0;
    bool data_service = false;
    uint8_t component_id = 0;
    uint8_t application = 0;

    if (extension == 0 || extension == 1) {
        if (offset + 2 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        offset += 2;
        label_key |= service_id;
    } else if (extension == 5) {
        if (offset + 4 > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        for (size_t index = 0; index < 4; index++) {
            service_id = service_id << 8 | data[offset++];
        }

        data_service = true;
        label_key |= service_id;
    } else if (extension == 4 || extension == 6) {
        if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        data_service = (data[offset] & 0x80) != 0;
        component_id = data[offset] & 0x0F;
        offset++;
        const size_t service_id_length = data_service ? 4 : 2;

        if (offset + service_id_length + (extension == 6 ? 1 : 0) > length) {
#if DAB_ENABLE_DIAGNOSTICS
            inspect_error("Incomplete or invalid FIG payload");
#endif
            return;
        }

        for (size_t index = 0; index < service_id_length; index++) {
            service_id = service_id << 8 | data[offset++];
        }

        const uint64_t component_key = make_service_component_key(service_id, data_service, component_id);
        label_key |= extension == 6 ? component_key << 8 : component_key;

        if (extension == 6) {
            application = data[offset++] & 0x1F;
            label_key |= application;
        }
    } else {
        return;
    }

    if (offset >= length) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

    const size_t header_length = version ? 1 : 3;

    if (segment_index == 0 && (offset + header_length > length || (data[offset] >> 4 & 0x07) > 1)) {
#if DAB_ENABLE_DIAGNOSTICS
        inspect_error("Incomplete or invalid FIG payload");
#endif
        return;
    }

#if DAB_ENABLE_DIAGNOSTICS
    const auto prefix = std::format("Label[0x{:X}].Segment[{}].", label_key, segment_index);
    inspect_field(prefix + "Toggle", std::to_string(toggle));
    inspect_field(prefix + "Version", std::to_string(version));
    inspect_field(prefix + "Segment index", std::to_string(segment_index));
#endif
    ExtendedLabel &state = extended_labels[label_key];
    const bool any_segment = std::ranges::any_of(state.received, [](const bool received) {
        return received;
    });

    if (any_segment && (state.toggle != toggle || state.version != version)) {
        state = {};
    }

    state.toggle = toggle;
    state.version = version;

    if (segment_index == 0) {
        const bool utf16 = (data[offset] & 0x80) != 0;
        const uint8_t segment_count = data[offset] >> 4 & 0x07;
        const uint8_t text_control = version ? data[offset] & 0x0F : 0;
        const uint16_t character_flags = version ? 0 : static_cast<uint16_t>(data[offset + 1] << 8 | data[offset + 2]);

        if (state.initialized && (state.utf16 != utf16 || state.segment_count != segment_count || state.text_control != text_control || state.character_flags != character_flags)) {
            state.received.fill(false);
        }

        state.utf16 = utf16;
        state.segment_count = segment_count;
        state.text_control = text_control;
        state.character_flags = character_flags;
        state.initialized = true;
        offset += header_length;
    } else if (state.initialized && segment_index > state.segment_count) {
        return;
    }

#if DAB_ENABLE_DIAGNOSTICS
    inspect_field(prefix + "Segment bytes", std::to_string(length - offset));
    inspect_field(prefix + "Initialized", std::to_string(state.initialized));
    if (state.initialized) {
        inspect_field(prefix + "UTF-16", std::to_string(state.utf16));
        inspect_field(prefix + "Last segment", std::to_string(state.segment_count));
        inspect_field(prefix + "Text control", std::format("0x{:X}", state.text_control));
    }
#endif
    state.segments[segment_index].assign(data + offset, data + length);
    state.received[segment_index] = true;

    if (!state.initialized) {
        return;
    }

    std::vector<uint8_t> label_data;

    for (size_t index = 0; index <= state.segment_count; index++) {
        if (!state.received[index]) {
            return;
        }

        label_data.insert(label_data.end(), state.segments[index].begin(), state.segments[index].end());
    }

    if (label_data.size() > 32) {
        return;
    }

    std::string label = decode_label(label_data.data(), label_data.size(), state.utf16 ? 6 : 15);

    if (label.empty()) {
        return;
    }

    const std::string short_label = version ? "" : decode_extended_short_label(label_data, state.utf16, state.character_flags);

    if (version && (state.text_control & 0x0C) != 0) {
        // Preserve the transmitted base direction in plain UTF-8 D-Bus strings.
        label = std::string((state.text_control & 0x04) != 0 ? "\u2067" : "\u2066") + label + "\u2069";
    }

#if DAB_ENABLE_DIAGNOSTICS
    inspect_field(prefix + "Complete text", label);
#endif
    completed_extended_labels[label_key] = {label, short_label, !version};
    update_label(label_key);
}

void FICDecoder::update_label(const uint64_t label_key) {
    const auto basic = basic_labels.find(label_key);
    const auto extended = completed_extended_labels.find(label_key);

    if (basic == basic_labels.end() && extended == completed_extended_labels.end()) {
        return;
    }

    const Label &value = extended != completed_extended_labels.end() ? extended->second : basic->second;
    const std::string &label = value.label;
    const std::string short_label = value.short_label_present ? value.short_label : basic != basic_labels.end() ? basic->second.short_label : "";
    const uint8_t extension = label_key >> 60;
    const uint64_t identifier = label_key & 0x0FFFFFFFFFFFFFFF;
    const uint64_t component_key = extension == 6 ? identifier >> 8 : identifier;
    const uint32_t service_id = extension == 4 || extension == 6 ? static_cast<uint32_t>(component_key >> 4) : static_cast<uint32_t>(identifier);
    const bool data_service = (component_key & 1ULL << 36) != 0;
    const uint8_t component_id = component_key & 0x0F;
    const uint8_t application = identifier & 0x1F;

    if (extension == 0) {
        Ensemble new_ensemble{static_cast<uint16_t>(service_id), label, short_label};

        if (!ensemble_valid || ensemble != new_ensemble) {
            ensemble = std::move(new_ensemble);
            ensemble_valid = true;
            update_ensemble();
        }
    } else if (extension == 1) {
        ServiceState &service = services[static_cast<uint16_t>(service_id)];
        service.id = static_cast<uint16_t>(service_id);
        service.label = label;
        service.short_label = short_label;
        update_service(service.id);

        for (auto &[component_key, component]: service_components) {
            if (!component.data_service && component.service_id == service_id) {
                update_service_component(component_key);
            }
        }
    } else if (extension == 5) {
        data_service_labels[service_id] = {label, short_label};

        for (auto &[component_key, component]: service_components) {
            if (component.data_service && component.service_id == service_id) {
                update_service_component(component_key);
            }
        }
    } else {
        ServiceComponent &component = service_components[component_key];
        component.service_id = service_id;
        component.data_service = data_service;
        component.id = component_id;

        if (extension == 4) {
            service_component_labels[component_key] = {label, short_label};
        } else {
            application_labels[component_key][application] = label;
        }

        update_service_component(component_key);
    }
}

void FICDecoder::update_subchannel(const uint8_t subchannel) {
    for (const auto &[service_id, service]: services) {
        if (service.primary_subchannel == subchannel) {
            update_service(service_id);
        }
    }
}

void FICDecoder::update_service(const uint16_t service_id) {
    const auto service_state = services.find(service_id);

    if (service_state == services.end() || service_state->second.label.empty() || service_state->second.primary_subchannel == 0xFF) {
        return;
    }

    const auto subchannel = subchannels.find(service_state->second.primary_subchannel);

    if (subchannel == subchannels.end()) {
        return;
    }

    Service service;
    service.id = service_id;
    service.label = service_state->second.label;
    service.short_label = service_state->second.short_label;
    service.ecc = service_state->second.ecc == 0 ? ensemble_status.ecc : service_state->second.ecc;
    service.subchannel = subchannel->second;
    service.dab_plus = service_state->second.dab_plus;

    const auto language = subchannel_languages.find(service.subchannel.id);

    if (language != subchannel_languages.end()) {
        service.language = language->second;
    }

    const int16_t pty = service_state->second.pty_dynamic >= 0 ? service_state->second.pty_dynamic : service_state->second.pty_static;

    if (pty >= 0) {
        service.pty = decode_pty(static_cast<uint8_t>(pty), pty_table_id);
    }

#if DAB_ENABLE_ANNOUNCEMENTS
    service.announcement_support = decode_announcement_flags(service_state->second.announcement_support_flags);
    uint16_t active_announcement_flags = 0;

    for (const uint8_t cluster_id: service_state->second.announcement_clusters) {
        const auto cluster = announcement_clusters.find(cluster_id);

        if (cluster != announcement_clusters.end()) {
            active_announcement_flags |= cluster->second.flags;
        }
    }

    active_announcement_flags &= service_state->second.announcement_support_flags;
    service.announcements = decode_announcement_flags(active_announcement_flags);
#endif

    const auto emitted = emitted_services.find(service_id);

    if (emitted != emitted_services.end() && emitted->second == service) {
        return;
    }

    emitted_services[service_id] = service;

    if (service_callback) {
        service_callback(service);
    }
}

void FICDecoder::update_service_component(const uint64_t component_key) {
    const auto component = service_components.find(component_key);

    if (component == service_components.end()) {
        return;
    }

    ServiceComponent &service_component = component->second;
    const TransportComponent *transport_component = nullptr;

    if (service_component.component_id != 0xFFFF) {
        const auto transport = packet_components.find(service_component.component_id);

        if (transport != packet_components.end()) {
            transport_component = &transport->second;
        }

        service_component.subchannel = transport_component ? transport_component->subchannel : 0xFF;
    } else {
        const auto transport = subchannel_components.find(service_component.subchannel);

        if (transport != subchannel_components.end()) {
            transport_component = &transport->second;
        }
    }

    service_component.data = transport_component ? transport_component->data : service_component.component_id != 0xFFFF;
    service_component.type = transport_component ? transport_component->type : 0;
    service_component.conditional_access = transport_component && transport_component->conditional_access;
    service_component.conditional_access_organization = transport_component ? transport_component->conditional_access_organization : 0;
    service_component.conditional_access_organization_present = transport_component && transport_component->conditional_access_organization_present;
    service_component.packet_address = transport_component ? transport_component->packet_address : 0xFFFF;
    service_component.data_groups = transport_component && transport_component->data_groups;
    const auto fec_scheme = subchannel_fec_schemes.find(service_component.subchannel);
    service_component.fec_scheme = fec_scheme != subchannel_fec_schemes.end() ? fec_scheme->second : 0;
    const auto language = subchannel_languages.find(service_component.subchannel);

    if (service_component.component_id == 0xFFFF && language != subchannel_languages.end()) {
        service_component.language = language->second;
    }

    service_component.applications.clear();

    for (UserApplication &application: service_component.user_applications) {
        service_component.applications.push_back(application.name);
        application.xpad = false;
        application.xpad_type = 0xFF;
        application.conditional_access = false;
        application.conditional_access_organization_present = false;
        application.conditional_access_organization = 0;
        application.data_groups = false;
        application.data_type = 0;
        application.parameters.clear();

        if (service_component.component_id != 0xFFFF || (transport_component && transport_component->defined && transport_component->data)) {
            application.parameters = application.data;
            continue;
        }

        if (!transport_component || !transport_component->defined || application.data.size() < 2) {
            continue;
        }

        const bool conditional_access_organization_present = (application.data[0] & 0x40) != 0;
        const size_t header_length = conditional_access_organization_present ? 4 : 2;

        if (application.data.size() < header_length) {
            continue;
        }

        application.xpad = true;
        application.xpad_type = application.data[0] & 0x1F;
        application.conditional_access = (application.data[0] & 0x80) != 0;
        application.conditional_access_organization_present = conditional_access_organization_present;
        application.conditional_access_organization = conditional_access_organization_present ? static_cast<uint16_t>(application.data[2] << 8 | application.data[3]) : 0;
        application.data_groups = (application.data[1] & 0x80) == 0;
        application.data_type = application.data[1] & 0x3F;
        application.parameters.assign(application.data.begin() + static_cast<std::ptrdiff_t>(header_length), application.data.end());
    }

    const auto component_label = service_component_labels.find(component_key);

    if (component_label != service_component_labels.end()) {
        component->second.label = component_label->second.first;
        component->second.short_label = component_label->second.second;
    } else if (component->second.data_service) {
        const auto service_label = data_service_labels.find(component->second.service_id);

        if (service_label != data_service_labels.end()) {
            component->second.label = service_label->second.first;
            component->second.short_label = service_label->second.second;
        }
    } else {
        const auto service = services.find(static_cast<uint16_t>(component->second.service_id));

        if (service != services.end()) {
            component->second.label = service->second.label;
            component->second.short_label = service->second.short_label;
        }
    }

    component->second.application_labels.clear();
    const auto labels = application_labels.find(component_key);

    if (labels != application_labels.end()) {
        for (const auto &label: labels->second | std::views::values) {
            component->second.application_labels.push_back(label);
        }
    }

    const auto emitted = emitted_service_components.find(component_key);

    if (emitted != emitted_service_components.end() && emitted->second == component->second) {
        return;
    }

    emitted_service_components[component_key] = component->second;

    if (service_component_callback) {
        service_component_callback(component->second);
    }
}

void FICDecoder::prepare_configuration_change() {
    configuration_cleanup_pending = true;
    ensemble_status.service_count = 0;
    service_ids.clear();
    subchannels.clear();
    subchannel_languages.clear();
    services.clear();
    service_components.clear();
    subchannel_components.clear();
    packet_components.clear();
    subchannel_fec_schemes.clear();
#if DAB_ENABLE_ANNOUNCEMENTS
    announcement_clusters.clear();
#endif
    update_alarm();
    apply_next_configuration();
}

void FICDecoder::apply_next_configuration() {
    auto configuration_figs = std::move(next_configuration_figs);
    next_configuration_figs.clear();
    next_configuration_valid = false;
    next_reconfiguration_counter = 0;
#if DAB_ENABLE_DIAGNOSTICS
    auto *previous_figure = current_figure;
    current_figure = nullptr;
#endif

    for (const auto &figs: configuration_figs | std::views::values) {
        for (const auto &fig: figs) {
            process_fig0(fig.data(), fig.size(), false);
        }
    }
#if DAB_ENABLE_DIAGNOSTICS
    current_figure = previous_figure;
#endif
}

void FICDecoder::finish_configuration_change() {
    configuration_cleanup_pending = false;

    for (auto service = emitted_services.begin(); service != emitted_services.end();) {
        const uint64_t service_key = service->first;

        if (service_ids.contains(service_key)) {
            ++service;
            continue;
        }

        const uint16_t service_id = service->first;
        service = emitted_services.erase(service);

        if (service_removed_callback) {
            service_removed_callback(service_id);
        }
    }

    for (auto component = emitted_service_components.begin(); component != emitted_service_components.end();) {
        if (service_components.contains(component->first)) {
            ++component;
            continue;
        }

        const uint64_t component_key = component->first;
        component = emitted_service_components.erase(component);

        if (service_component_removed_callback) {
            service_component_removed_callback(component_key);
        }
    }
}

void FICDecoder::update_ensemble() {
    if (!ensemble_valid || ensemble.label.empty()) {
        return;
    }

    if (emitted_ensemble_valid && emitted_ensemble == ensemble) {
        return;
    }

    emitted_ensemble = ensemble;
    emitted_ensemble_valid = true;

    if (ensemble_callback) {
        ensemble_callback(ensemble);
    }
}

void FICDecoder::update_ensemble_status() {
    ensemble_status.services_found = static_cast<uint8_t>(std::min<size_t>(service_ids.size(), 0xFF));

    if (emitted_ensemble_status_valid && emitted_ensemble_status == ensemble_status) {
        return;
    }

    emitted_ensemble_status = ensemble_status;
    emitted_ensemble_status_valid = true;

    if (ensemble_status_callback) {
        ensemble_status_callback(ensemble_status);
    }
}

void FICDecoder::update_alarm() {
    bool new_alarm = false;

#if DAB_ENABLE_ANNOUNCEMENTS
    for (const auto &cluster: announcement_clusters | std::views::values) {
        if ((cluster.flags & 0x0001) != 0) {
            new_alarm = true;
            break;
        }
    }
#endif

    if (ensemble_status.alarm_active == new_alarm) {
        return;
    }

    ensemble_status.alarm_active = new_alarm;
    update_ensemble_status();
}

uint16_t FICDecoder::calculate_crc(const uint8_t *data, const size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t offset = 0; offset < length; offset++) {
        crc ^= static_cast<uint16_t>(data[offset]) << 8;

        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) != 0 ? static_cast<uint16_t>(crc << 1 ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }

    return static_cast<uint16_t>(~crc);
}

std::string FICDecoder::decode_label(const uint8_t *data, const size_t length, const uint8_t charset) {
    std::string result;

    if (charset == 0) {
        result = decode_ebu_latin(data, length);
    } else if (charset == 6) {
        result = decode_ucs2(data, length);
    } else if (charset == 15) {
        result = decode_utf8(data, length);
    } else {
        return {};
    }

    while (!result.empty() && (result.back() == ' ' || result.back() == '\0')) {
        result.pop_back();
    }

    return result;
}

std::string FICDecoder::decode_short_label(const uint8_t *data, const size_t length, const uint8_t charset, const uint16_t flags) {
    if (charset == 15) {
        return decode_extended_short_label(std::vector<uint8_t>(data, data + length), false, flags);
    }

    std::vector<uint8_t> selected;

    if (charset == 6) {
        for (size_t offset = 0; offset + 1 < length && offset / 2 < 16; offset += 2) {
            if ((flags & 1U << (15 - offset / 2)) != 0) {
                selected.push_back(data[offset]);
                selected.push_back(data[offset + 1]);
            }
        }
    } else {
        for (size_t offset = 0; offset < length && offset < 16; offset++) {
            if ((flags & 1U << (15 - offset)) != 0) {
                selected.push_back(data[offset]);
            }
        }
    }

    return decode_label(selected.data(), selected.size(), charset);
}

std::string FICDecoder::decode_extended_short_label(const std::vector<uint8_t> &data, const bool utf16, const uint16_t flags) {
    std::vector<uint8_t> selected;

    if (utf16) {
        for (size_t offset = 0; offset + 1 < data.size() && offset / 2 < 16; offset += 2) {
            if ((flags & 1U << (15 - offset / 2)) != 0) {
                selected.push_back(data[offset]);
                selected.push_back(data[offset + 1]);
            }
        }

        return decode_label(selected.data(), selected.size(), 6);
    }

    size_t offset = 0;
    size_t character = 0;

    while (offset < data.size() && character < 16) {
        size_t character_length = 1;

        if ((data[offset] & 0xE0) == 0xC0) {
            character_length = 2;
        } else if ((data[offset] & 0xF0) == 0xE0) {
            character_length = 3;
        } else if ((data[offset] & 0xF8) == 0xF0) {
            character_length = 4;
        }

        character_length = std::min(character_length, data.size() - offset);

        if ((flags & 1U << (15 - character)) != 0) {
            selected.insert(selected.end(), data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + character_length));
        }

        offset += character_length;
        character++;
    }

    return decode_label(selected.data(), selected.size(), 15);
}

std::string FICDecoder::decode_ebu_latin(const uint8_t *data, const size_t length) {
    static constexpr const char *VALUES_00_1F[] = {
        "", "Ę", "Į", "Ų", "Ă", "Ė", "Ď", "Ș", "Ț", "Ċ", "", "", "Ġ", "Ĺ", "Ż", "Ń",
        "ą", "ę", "į", "ų", "ă", "ė", "ď", "ș", "ț", "ċ", "Ň", "Ě", "ġ", "ĺ", "ż", ""
    };

    static constexpr const char *VALUES_7B_FF[] = {
        "«", "ů", "»", "Ľ", "Ħ", "á", "à", "é", "è", "í", "ì", "ó", "ò", "ú", "ù", "Ñ", "Ç", "Ş", "ß", "¡", "Ÿ",
        "â", "ä", "ê", "ë", "î", "ï", "ô", "ö", "û", "ü", "ñ", "ç", "ş", "ğ", "ı", "ÿ", "Ķ", "Ņ", "©", "Ģ", "Ğ", "ě", "ň", "ő", "Ő", "€", "£", "$", "Ā", "Ē", "Ī", "Ū",
        "ķ", "ņ", "Ļ", "ģ", "ļ", "İ", "ń", "ű", "Ű", "¿", "ľ", "°", "ā", "ē", "ī", "ū", "Á", "À", "É", "È", "Í", "Ì", "Ó", "Ò", "Ú", "Ù", "Ř", "Č", "Š", "Ž", "Ð", "Ŀ",
        "Â", "Ä", "Ê", "Ë", "Î", "Ï", "Ô", "Ö", "Û", "Ü", "ř", "č", "š", "ž", "đ", "ŀ", "Ã", "Å", "Æ", "Œ", "ŷ", "Ý", "Õ", "Ø", "Þ", "Ŋ", "Ŕ", "Ć", "Ś", "Ź", "Ť", "ð",
        "ã", "å", "æ", "œ", "ŵ", "ý", "õ", "ø", "þ", "ŋ", "ŕ", "ć", "ś", "ź", "ť", "ħ"
    };

    std::string result;

    for (size_t offset = 0; offset < length; offset++) {
        const uint8_t value = data[offset];

        if (value <= 0x1F) {
            result += VALUES_00_1F[value];
        } else if (value >= 0x7B) {
            result += VALUES_7B_FF[value - 0x7B];
        } else if (value == 0x24) {
            result += "ł";
        } else if (value == 0x5C) {
            result += "Ů";
        } else if (value == 0x5E) {
            result += "Ł";
        } else if (value == 0x60) {
            result += "Ą";
        } else {
            result += static_cast<char>(value);
        }
    }

    return result;
}

std::string FICDecoder::decode_utf8(const uint8_t *data, const size_t length) {
    size_t end = length;

    while (end > 0 && (data[end - 1] == 0 || data[end - 1] == ' ')) {
        end--;
    }

    for (size_t offset = 0; offset < end;) {
        const uint8_t first = data[offset];
        const size_t character_length = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 : first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;

        if (character_length == 0 || offset + character_length > end || first < 0x20 || first == 0x7F) {
            return {};
        }

        uint32_t codepoint = character_length == 1 ? first : first & (0x7F >> character_length);

        for (size_t index = 1; index < character_length; index++) {
            if ((data[offset + index] & 0xC0) != 0x80) {
                return {};
            }

            codepoint = codepoint << 6 | (data[offset + index] & 0x3F);
        }

        if ((character_length == 3 && codepoint < 0x800) || (character_length == 4 && codepoint < 0x10000) || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
            return {};
        }

        offset += character_length;
    }

    return end ? std::string(reinterpret_cast<const char *>(data), end) : "";
}

std::string FICDecoder::decode_ucs2(const uint8_t *data, const size_t length) {
    if (length % 2 != 0) {
        return {};
    }

    size_t end = length;

    while (end >= 2 && data[end - 2] == 0 && (data[end - 1] == 0 || data[end - 1] == ' ')) {
        end -= 2;
    }

    std::string result;

    for (size_t offset = 0; offset < end; offset += 2) {
        const uint16_t codepoint = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);

        if (codepoint < 0x20 || codepoint == 0x7F || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            return {};
        }

        append_utf8(result, codepoint);
    }

    return result;
}

std::string FICDecoder::decode_language(const uint8_t language) {
    if (language <= 0x2B) {
        return LANGUAGES_00_2B[language];
    }

    if (language == 0x40) {
        return "Background sound / clean feed";
    }

    if (language >= 0x45 && language <= 0x7F) {
        return LANGUAGES_7F_45[0x7F - language];
    }

    return "Unknown";
}

std::string FICDecoder::decode_pty(const uint8_t pty, const uint8_t table_id) {
    if (pty > 0x1D) {
        return "Not used";
    }

    return table_id == 0x02 ? PTY_RBDS[pty] : PTY_RDS[pty];
}

#if DAB_ENABLE_ANNOUNCEMENTS
std::vector<std::string> FICDecoder::decode_announcement_flags(const uint16_t flags) {
    std::vector<std::string> result;

    for (uint8_t type = 0; type < 11; type++) {
        if ((flags & (1u << type)) != 0) {
            result.emplace_back(ANNOUNCEMENT_TYPES[type]);
        }
    }

    return result;
}
#endif


std::string FICDecoder::decode_user_application(const uint16_t application) {
    if (application == 0x002) {
        return "SlideShow";
    }

    if (application == 0x004) {
        return "TPEG";
    }

    if (application == 0x007) {
        return "SPI";
    }

    if (application == 0x009) {
        return "DMB";
    }

    if (application == 0x00D) {
        return "Filecasting";
    }

    if (application == 0x00E) {
        return "FIS";
    }

    if (application == 0x44A) {
        return "Journaline";
    }

    return std::format("0x{:03X}", application);
}

uint64_t FICDecoder::make_service_component_key(const uint32_t service_id, const bool data_service, const uint8_t component_id) {
    return static_cast<uint64_t>(data_service) << 36 | static_cast<uint64_t>(service_id) << 4 | component_id;
}

void FICDecoder::append_utf8(std::string &output, const uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        output += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        output += static_cast<char>(0xC0 | codepoint >> 6);
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        output += static_cast<char>(0xE0 | codepoint >> 12);
        output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        output += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

const uint16_t FICDecoder::UEP_SIZES[64] = {
    16, 21, 24, 29, 35, 24, 29, 35, 42, 52, 29, 35, 42, 52, 32, 42,
    48, 58, 70, 40, 52, 58, 70, 84, 48, 58, 70, 84, 104, 58, 70, 84,
    104, 64, 84, 96, 116, 140, 80, 104, 116, 140, 168, 96, 116, 140, 168, 208,
    116, 140, 168, 208, 232, 128, 168, 192, 232, 280, 160, 208, 280, 192, 280, 416
};

const uint8_t FICDecoder::UEP_PROTECTION_LEVELS[64] = {
    5, 4, 3, 2, 1, 5, 4, 3, 2, 1, 5, 4, 3, 2, 5, 4,
    3, 2, 1, 5, 4, 3, 2, 1, 5, 4, 3, 2, 1, 5, 4, 3,
    2, 5, 4, 3, 2, 1, 5, 4, 3, 2, 1, 5, 4, 3, 2, 1,
    5, 4, 3, 2, 1, 5, 4, 3, 2, 1, 5, 4, 2, 5, 3, 1
};

const uint16_t FICDecoder::UEP_BITRATES[64] = {
    32, 32, 32, 32, 32, 48, 48, 48, 48, 48, 56, 56, 56, 56, 64, 64,
    64, 64, 64, 80, 80, 80, 80, 80, 96, 96, 96, 96, 96, 112, 112, 112,
    112, 128, 128, 128, 128, 128, 160, 160, 160, 160, 160, 192, 192, 192, 192, 192,
    224, 224, 224, 224, 224, 256, 256, 256, 256, 256, 320, 320, 320, 384, 384, 384
};

const uint8_t FICDecoder::EEP_A_SIZE_FACTORS[4] = {12, 8, 6, 4};
const uint8_t FICDecoder::EEP_B_SIZE_FACTORS[4] = {27, 21, 18, 15};

const char *FICDecoder::LANGUAGES_00_2B[44] = {
    "Unknown / not applicable", "Albanian", "Breton", "Catalan", "Croatian", "Welsh", "Czech", "Danish",
    "German", "English", "Spanish", "Esperanto", "Estonian", "Basque", "Faroese", "French",
    "Frisian", "Irish", "Gaelic", "Galician", "Icelandic", "Italian", "Sami", "Latin",
    "Latvian", "Luxembourgian", "Lithuanian", "Hungarian", "Maltese", "Dutch", "Norwegian", "Occitan",
    "Polish", "Portuguese", "Romanian", "Romansh", "Serbian", "Slovak", "Slovene", "Finnish",
    "Swedish", "Turkish", "Flemish", "Walloon"
};

const char *FICDecoder::LANGUAGES_7F_45[59] = {
    "Amharic", "Arabic", "Armenian", "Assamese", "Azerbaijani", "Bambora", "Belorussian", "Bengali",
    "Bulgarian", "Burmese", "Chinese", "Chuvash", "Dari", "Fulani", "Georgian", "Greek",
    "Gujurati", "Gurani", "Hausa", "Hebrew", "Hindi", "Indonesian", "Japanese", "Kannada",
    "Kazakh", "Khmer", "Korean", "Laotian", "Macedonian", "Malagasay", "Malaysian", "Moldavian",
    "Marathi", "Ndebele", "Nepali", "Oriya", "Papiamento", "Persian", "Punjabi", "Pushtu",
    "Quechua", "Russian", "Rusyn", "Serbo-Croat", "Shona", "Sinhalese", "Somali", "Sranan Tongo",
    "Swahili", "Tadzhik", "Tamil", "Tatar", "Telugu", "Thai", "Ukranian", "Urdu",
    "Uzbek", "Vietnamese", "Zulu"
};

const char *FICDecoder::PTY_RDS[30] = {
    "No programme type", "News", "Current Affairs", "Information", "Sport", "Education", "Drama", "Culture",
    "Science", "Varied", "Pop Music", "Rock Music", "Easy Listening Music", "Light Classical", "Serious Classical", "Other Music",
    "Weather / meteorology", "Finance / Business", "Children's programmes", "Social Affairs", "Religion", "Phone In", "Travel", "Leisure",
    "Jazz Music", "Country Music", "National Music", "Oldies Music", "Folk Music", "Documentary"
};

const char *FICDecoder::PTY_RBDS[30] = {
    "No program type", "News", "Information", "Sports", "Talk", "Rock", "Classic Rock", "Adult Hits",
    "Soft Rock", "Top 40", "Country", "Oldies", "Soft", "Nostalgia", "Jazz", "Classical",
    "Rhythm and Blues", "Soft Rhythm and Blues", "Foreign Language", "Religious Music", "Religious Talk", "Personality", "Public", "College",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Weather"
};

#if DAB_ENABLE_ANNOUNCEMENTS
const char *FICDecoder::ANNOUNCEMENT_TYPES[11] = {
    "Alarm", "Traffic", "Transport", "Warning", "News", "Weather", "Event", "SpecialEvent", "ProgrammeInformation", "Sport", "Financial"
};
#endif
