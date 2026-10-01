//
// Created by tarek on 10/1/26.
//

#include "fic_decoder.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <utility>

FICDecoder::FICDecoder(const std::function<void(const Ensemble &)> &ensemble_callback, const std::function<void(const EnsembleStatus &)> &ensemble_status_callback, const std::function<void(const Service &)> &service_callback, const std::function<void(uint16_t)> &service_removed_callback, const std::function<void(const ServiceComponent &)> &service_component_callback, const std::function<void(uint64_t)> &service_component_removed_callback, const std::function<void(bool)> &alarm_callback, const std::function<void(const DateTime &)> &date_time_callback) {
    this->ensemble_callback = ensemble_callback;
    this->ensemble_status_callback = ensemble_status_callback;
    this->service_callback = service_callback;
    this->service_removed_callback = service_removed_callback;
    this->service_component_callback = service_component_callback;
    this->service_component_removed_callback = service_component_removed_callback;
    this->alarm_callback = alarm_callback;
    this->date_time_callback = date_time_callback;
}

void FICDecoder::reset() {
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
    alarm = false;
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
    announcement_clusters.clear();
    service_component_labels.clear();
    data_service_labels.clear();
    application_labels.clear();
    extended_labels.clear();
    completed_extended_labels.clear();
    service_links.clear();
    frequency_information.clear();
    other_ensemble_services.clear();
    oe_announcement_support.clear();
    oe_announcement_switches.clear();
    fm_announcement_support.clear();
    fm_announcement_switches.clear();
}

void FICDecoder::process(const std::vector<uint8_t> &data) {
    if (data.size() % 32 != 0) {
        return;
    }

    for (size_t offset = 0; offset < data.size(); offset += 32) {
        process_fib(data.data() + offset);
    }
}

FICDecoder::Statistics FICDecoder::get_statistics() const {
    return statistics;
}

std::vector<FICDecoder::ServiceLink> FICDecoder::get_service_links() const {
    std::vector<ServiceLink> result;

    for (const auto &links: service_links | std::views::values) {
        result.insert(result.end(), links.begin(), links.end());
    }

    return result;
}

std::vector<FICDecoder::FrequencyInformation> FICDecoder::get_frequency_information() const {
    std::vector<FrequencyInformation> result;

    for (const auto &frequencies: frequency_information | std::views::values) {
        result.insert(result.end(), frequencies.begin(), frequencies.end());
    }

    return result;
}

std::vector<FICDecoder::OtherEnsembleService> FICDecoder::get_other_ensemble_services() const {
    std::vector<OtherEnsembleService> result;

    for (const auto &service: other_ensemble_services | std::views::values) {
        result.push_back(service);
    }

    return result;
}

std::vector<FICDecoder::OEAnnouncementSupport> FICDecoder::get_oe_announcement_support() const {
    std::vector<OEAnnouncementSupport> result;

    for (const auto &support: oe_announcement_support | std::views::values) {
        result.push_back(support);
    }

    return result;
}

std::vector<FICDecoder::OEAnnouncementSwitch> FICDecoder::get_oe_announcement_switches() const {
    std::vector<OEAnnouncementSwitch> result;

    for (const auto &announcement: oe_announcement_switches | std::views::values) {
        result.push_back(announcement);
    }

    return result;
}

std::vector<FICDecoder::FMAnnouncementSupport> FICDecoder::get_fm_announcement_support() const {
    std::vector<FMAnnouncementSupport> result;

    for (const auto &announcement: fm_announcement_support | std::views::values) {
        result.push_back(announcement);
    }

    return result;
}

std::vector<FICDecoder::FMAnnouncementSwitch> FICDecoder::get_fm_announcement_switches() const {
    std::vector<FMAnnouncementSwitch> result;

    for (const auto &announcement: fm_announcement_switches | std::views::values) {
        result.push_back(announcement);
    }

    return result;
}

void FICDecoder::process_fib(const uint8_t *data) {
    statistics.fibs_received++;
    const uint16_t stored_crc = static_cast<uint16_t>(data[30] << 8 | data[31]);

    if (stored_crc != calculate_crc(data, 30)) {
        statistics.fibs_crc_errors++;
        return;
    }

    statistics.fibs_valid++;

    size_t offset = 0;

    while (offset < 30 && data[offset] != 0xFF) {
        const uint8_t type = data[offset] >> 5;
        const size_t length = data[offset] & 0x1F;
        offset++;

        if (offset + length > 30) {
            return;
        }

        statistics.figs_received++;
        statistics.fig_types[type]++;

        if (type == 0) {
            process_fig0(data + offset, length);
        } else if (type == 1) {
            process_fig1(data + offset, length);
        } else if (type == 2) {
            process_fig2(data + offset, length);
        }

        offset += length;
    }
}

void FICDecoder::process_fig0(const uint8_t *data, const size_t length, const bool count_statistics) {
    if (length < 1) {
        return;
    }

    const uint8_t header = data[0];
    const bool cn = (header & 0x80) != 0;
    const bool oe = (header & 0x40) != 0;
    const bool pd = (header & 0x20) != 0;
    const uint8_t extension = header & 0x1F;

    if (count_statistics) {
        statistics.fig0_extensions[extension]++;
    }

    if (extension == 6) {
        if (!oe) {
            process_fig0_6(data + 1, length - 1, pd, cn);
        }
        return;
    }

    if (extension == 21) {
        if (!pd) {
            process_fig0_21(data + 1, length - 1, oe);
        }
        return;
    }

    if (extension == 24) {
        process_fig0_24(data + 1, length - 1, pd, oe);
        return;
    }

    if (extension == 27) {
        if (!oe && !pd) {
            process_fig0_27(data + 1, length - 1);
        }
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
    } else if (extension == 18 && !pd) {
        process_fig0_18(data + 1, length - 1);
    } else if (extension == 19 && !pd) {
        process_fig0_19(data + 1, length - 1);
    } else if (extension == 20) {
        process_fig0_20(data + 1, length - 1, pd);
    } else if (extension == 25 && !pd) {
        process_fig0_25(data + 1, length - 1);
    } else if (extension == 26 && !pd) {
        process_fig0_26(data + 1, length - 1);
    } else if (extension == 28 && !pd) {
        process_fig0_28(data + 1, length - 1);
    }
}

void FICDecoder::process_fig0_0(const uint8_t *data, const size_t length) {
    if (length < 4) {
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
        return;
    }

    ensemble_status.ensemble_id = ensemble_id;
    ensemble_status.change_flags = change_flags;
    ensemble_status.alarm_supported = alarm_supported;
    ensemble_status.occurrence_change = change_flags != 0 ? data[4] : 0;
    ensemble_status.cif_counter = static_cast<uint16_t>(cif_high * 250 + cif_low);
    update_ensemble_status();
}

void FICDecoder::process_fig0_1(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 3 > length) {
            return;
        }

        Subchannel subchannel;
        subchannel.id = data[offset] >> 2;
        subchannel.start = static_cast<uint16_t>((data[offset] & 0x03) << 8 | data[offset + 1]);
        offset += 2;

        const bool long_form = (data[offset] & 0x80) != 0;

        if (long_form) {
            if (offset + 2 > length) {
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

        const auto current = subchannels.find(subchannel.id);

        if (current == subchannels.end() || current->second != subchannel) {
            subchannels[subchannel.id] = subchannel;
            update_subchannel(subchannel.id);
        }
    }
}

void FICDecoder::process_fig0_2(const uint8_t *data, const size_t length, const bool pd) {
    size_t offset = 0;
    bool service_ids_changed = false;

    while (offset < length) {
        const size_t service_id_length = pd ? 4 : 2;

        if (offset + service_id_length + 1 > length) {
            return;
        }

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

        if (offset + static_cast<size_t>(component_count) * 2 > length) {
            return;
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
            TransportComponent transport_component{tmid != 0, tmid == 3 ? static_cast<uint8_t>(0) : type, conditional_access, 0, 0xFF};

            if (tmid == 0 || tmid == 1) {
                const uint8_t subchannel = data[offset + 1] >> 2;
                transport_component.subchannel = subchannel;
                const auto current_transport_component = subchannel_components.find(subchannel);

                if (current_transport_component != subchannel_components.end() && current_transport_component->second.conditional_access_organization != 0) {
                    transport_component.conditional_access = true;
                    transport_component.conditional_access_organization = current_transport_component->second.conditional_access_organization;
                }

                subchannel_components[subchannel] = transport_component;
            } else if (tmid == 3) {
                const uint16_t packet_component_id = static_cast<uint16_t>((data[offset] & 0x3F) << 6 | data[offset + 1] >> 2);
                packet_components[packet_component_id] = transport_component;
            }

            if (primary) {
                const uint64_t component_key = make_service_component_key(service_id, pd, 0);
                ServiceComponent &service_component = service_components[component_key];
                service_component.service_id = service_id;
                service_component.data_service = pd;
                service_component.id = 0;
                service_component.data = tmid != 0;
                service_component.type = tmid == 3 ? 0 : type;
                service_component.conditional_access = conditional_access;

                if (tmid == 0 || tmid == 1) {
                    service_component.subchannel = data[offset + 1] >> 2;
                    service_component.component_id = 0xFFFF;
                } else if (tmid == 3) {
                    service_component.subchannel = 0xFF;
                    service_component.component_id = static_cast<uint16_t>((data[offset] & 0x3F) << 6 | data[offset + 1] >> 2);
                }

                const auto transport = tmid == 3 ? packet_components.find(service_component.component_id) : packet_components.end();

                if (transport != packet_components.end()) {
                    service_component.subchannel = transport->second.subchannel;
                    service_component.data = transport->second.data;
                    service_component.type = transport->second.type;
                    service_component.conditional_access = transport->second.conditional_access;
                    service_component.conditional_access_organization = transport->second.conditional_access_organization;
                } else if (tmid == 0 || tmid == 1) {
                    const auto stream_transport = subchannel_components.find(service_component.subchannel);

                    if (stream_transport != subchannel_components.end()) {
                        service_component.conditional_access = stream_transport->second.conditional_access;
                        service_component.conditional_access_organization = stream_transport->second.conditional_access_organization;
                    }
                }

                const auto fec_scheme = subchannel_fec_schemes.find(service_component.subchannel);

                if (fec_scheme != subchannel_fec_schemes.end()) {
                    service_component.fec_scheme = fec_scheme->second;
                }

                update_service_component(component_key);
            }

            if (service && tmid == 0) {
                const uint8_t ascty = type;
                const uint8_t subchannel = data[offset + 1] >> 2;

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

    while (offset < length) {
        if (offset + 5 > length) {
            return;
        }

        const uint16_t component_id = static_cast<uint16_t>(data[offset] << 4 | data[offset + 1] >> 4);
        const bool conditional_access = (data[offset + 1] & 0x01) != 0;

        if (conditional_access && offset + 7 > length) {
            return;
        }

        const uint8_t type = data[offset + 2] & 0x3F;
        const uint8_t subchannel = data[offset + 3] >> 2;
        const uint16_t conditional_access_organization = conditional_access ? static_cast<uint16_t>(data[offset + 5] << 8 | data[offset + 6]) : 0;
        offset += conditional_access ? 7 : 5;

        if (offset > length) {
            return;
        }

        for (auto &[component_key, component]: service_components) {
            if (component.component_id != component_id) {
                continue;
            }

            component.subchannel = subchannel;
            component.data = true;
            component.type = type;
            component.conditional_access = conditional_access;
            component.conditional_access_organization = conditional_access_organization;

            const auto fec_scheme = subchannel_fec_schemes.find(subchannel);

            if (fec_scheme != subchannel_fec_schemes.end()) {
                component.fec_scheme = fec_scheme->second;
            }

            update_service_component(component_key);
        }

        packet_components[component_id] = {true, type, conditional_access, conditional_access_organization, subchannel};
    }
}

void FICDecoder::process_fig0_4(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 3 > length) {
            return;
        }

        const uint8_t subchannel = data[offset] & 0x3F;
        const uint16_t conditional_access_organization = static_cast<uint16_t>(data[offset + 1] << 8 | data[offset + 2]);
        offset += 3;
        TransportComponent &transport_component = subchannel_components[subchannel];
        transport_component.subchannel = subchannel;
        transport_component.conditional_access = true;
        transport_component.conditional_access_organization = conditional_access_organization;

        for (auto &[component_key, component]: service_components) {
            if (component.subchannel != subchannel) {
                continue;
            }

            component.conditional_access = true;
            component.conditional_access_organization = conditional_access_organization;
            update_service_component(component_key);
        }
    }
}

void FICDecoder::process_fig0_5(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if ((data[offset] & 0x80) != 0) {
            if (offset + 3 > length) {
                return;
            }

            const uint16_t component_id = static_cast<uint16_t>((data[offset] & 0x0F) << 8 | data[offset + 1]);
            const std::string language = decode_language(data[offset + 2]);

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
            return;
        }

        const bool fic_component = (data[offset] & 0x40) != 0;

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

void FICDecoder::process_fig0_6(const uint8_t *data, const size_t length, const bool pd, const bool continuation) {
    if (length < 2) {
        return;
    }

    const uint16_t header = static_cast<uint16_t>(data[0] << 8 | data[1]);
    const bool identifier_list = (header & 0x8000) != 0;
    const bool active = (header & 0x4000) != 0;
    const bool hard = (header & 0x2000) != 0;
    const bool international = (header & 0x1000) != 0;
    const uint16_t linkage_set = header & 0x0FFF;

    if (!identifier_list) {
        for (auto &links: service_links | std::views::values) {
            for (ServiceLink &link: links) {
                if (link.linkage_set == linkage_set && link.hard == hard && link.international == international) {
                    link.active = active;
                }
            }
        }
        return;
    }

    if (length < 3) {
        return;
    }

    const uint8_t identifier_list_qualifier = data[2] >> 5 & 0x03;
    const bool shared = !hard && (data[2] & 0x10) != 0;
    const size_t identifier_count = data[2] & 0x0F;
    const size_t identifier_length = pd ? 4 : international ? 3 : 2;

    if (3 + identifier_count * identifier_length > length) {
        return;
    }

    const uint32_t database_key = static_cast<uint32_t>(pd) << 31 | static_cast<uint32_t>(hard) << 30 | static_cast<uint32_t>(international) << 29 | static_cast<uint32_t>(identifier_list_qualifier) << 12 | linkage_set;
    std::vector<ServiceLink> &links = service_links[database_key];

    if (!continuation) {
        links.clear();
    }

    size_t offset = 3;

    for (size_t index = 0; index < identifier_count; index++) {
        ServiceLink link;
        link.linkage_set = linkage_set;
        link.active = active;
        link.hard = hard;
        link.shared = shared;
        link.international = international;
        link.type = identifier_list_qualifier == 0 ? "DAB" : identifier_list_qualifier == 1 ? "FM-RDS" : identifier_list_qualifier == 2 ? "AMSS" : "DRM";

        if (international && !pd) {
            link.ecc = data[offset++];
        }

        for (size_t byte = international && !pd ? 1 : 0; byte < identifier_length; byte++) {
            link.id = link.id << 8 | data[offset++];
        }

        links.push_back(std::move(link));
    }
}

void FICDecoder::process_fig0_7(const uint8_t *data, const size_t length) {
    if (length < 2) {
        return;
    }

    const uint8_t service_count = data[0] >> 2;
    const uint16_t reconfiguration_counter = static_cast<uint16_t>((data[0] & 0x03) << 8 | data[1]);

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

    while (offset < length) {
        if (offset + service_id_length + 2 > length) {
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

        ServiceComponent &component = service_components[make_service_component_key(service_id, pd, component_id)];
        component.service_id = service_id;
        component.data_service = pd;
        component.id = component_id;

        if ((data[offset] & 0x80) == 0) {
            component.subchannel = data[offset] & 0x3F;
            component.component_id = 0xFFFF;
            offset++;

            const auto transport_component = subchannel_components.find(component.subchannel);

            if (transport_component != subchannel_components.end()) {
                component.data = transport_component->second.data;
                component.type = transport_component->second.type;
                component.conditional_access = transport_component->second.conditional_access;
                component.conditional_access_organization = transport_component->second.conditional_access_organization;
            }

            const auto fec_scheme = subchannel_fec_schemes.find(component.subchannel);

            if (fec_scheme != subchannel_fec_schemes.end()) {
                component.fec_scheme = fec_scheme->second;
            }

            const auto language = subchannel_languages.find(component.subchannel);

            if (language != subchannel_languages.end()) {
                component.language = language->second;
            }
        } else {
            if (offset + 2 > length) {
                return;
            }

            component.subchannel = 0xFF;
            component.component_id = static_cast<uint16_t>((data[offset] & 0x0F) << 8 | data[offset + 1]);
            offset += 2;

            const auto transport_component = packet_components.find(component.component_id);

            if (transport_component != packet_components.end()) {
                component.subchannel = transport_component->second.subchannel;
                component.data = transport_component->second.data;
                component.type = transport_component->second.type;
                component.conditional_access = transport_component->second.conditional_access;
                component.conditional_access_organization = transport_component->second.conditional_access_organization;
            }
        }

        if (extension_present) {
            if (offset >= length) {
                return;
            }

            offset++;
        }

        update_service_component(make_service_component_key(service_id, pd, component_id));
    }
}

void FICDecoder::process_fig0_9(const uint8_t *data, const size_t length) {
    if (length < 3) {
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

    if (extended) {
        size_t offset = 3;

        while (offset + 2 <= length) {
            const size_t service_count = data[offset] >> 6;
            const uint8_t ecc = data[offset + 1];
            offset += 2;

            if (offset + service_count * 2 > length) {
                return;
            }

            for (size_t index = 0; index < service_count; index++) {
                const uint16_t service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
                offset += 2;
                ServiceState &service = services[service_id];
                service.id = service_id;

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
            return;
        }

        new_date_time.second = data[4] >> 2;
        new_date_time.milliseconds = static_cast<uint16_t>((data[4] & 0x03) << 8 | data[5]);
    }

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

    while (offset < length) {
        if (offset + service_id_length + 1 > length) {
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
        std::vector<std::string> applications;

        for (uint8_t index = 0; index < application_count; index++) {
            if (offset + 2 > length) {
                return;
            }

            const uint16_t application = static_cast<uint16_t>(data[offset] << 3 | data[offset + 1] >> 5);
            const size_t application_data_length = data[offset + 1] & 0x1F;
            offset += 2;

            if (offset + application_data_length > length) {
                return;
            }

            applications.push_back(decode_user_application(application));
            offset += application_data_length;
        }

        const uint64_t component_key = make_service_component_key(service_id, pd, component_id);
        ServiceComponent &component = service_components[component_key];
        component.service_id = service_id;
        component.data_service = pd;
        component.id = component_id;
        component.applications = std::move(applications);
        update_service_component(component_key);
    }
}

void FICDecoder::process_fig0_14(const uint8_t *data, const size_t length) {
    for (size_t offset = 0; offset < length; offset++) {
        const uint8_t subchannel = data[offset] >> 2;
        const uint8_t fec_scheme = data[offset] & 0x03;
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

void FICDecoder::process_fig0_17(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 4 > length) {
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
                return;
            }

            offset++;
        }

        if (offset >= length) {
            return;
        }

        const int16_t pty = data[offset] & 0x1F;
        offset++;

        if (country_code_present) {
            if (offset >= length) {
                return;
            }

            offset++;
        }

        ServiceState &service = services[service_id];
        service.id = service_id;
        int16_t &current_pty = dynamic ? service.pty_dynamic : service.pty_static;

        if (current_pty != pty) {
            current_pty = pty;
            update_service(service_id);
        }
    }
}

void FICDecoder::process_fig0_18(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 5 > length) {
            return;
        }

        const uint16_t service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const uint16_t flags = static_cast<uint16_t>(data[offset + 2] << 8 | data[offset + 3]);
        const size_t cluster_count = data[offset + 4] & 0x1F;
        offset += 5;

        if (offset + cluster_count > length) {
            return;
        }

        std::set<uint8_t> clusters;

        for (size_t index = 0; index < cluster_count; index++) {
            clusters.insert(data[offset++]);
        }

        ServiceState &service = services[service_id];
        service.id = service_id;

        if (service.announcement_support_flags != flags || service.announcement_clusters != clusters) {
            service.announcement_support_flags = flags;
            service.announcement_clusters = std::move(clusters);
            update_service(service_id);
        }
    }
}

void FICDecoder::process_fig0_19(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset < length) {
        if (offset + 4 > length) {
            return;
        }

        const uint8_t cluster_id = data[offset];
        const uint16_t flags = static_cast<uint16_t>(data[offset + 1] << 8 | data[offset + 2]);
        const bool region_present = (data[offset + 3] & 0x40) != 0;
        const uint8_t subchannel = data[offset + 3] & 0x3F;
        const size_t entry_length = region_present ? 5 : 4;

        if (offset + entry_length > length) {
            return;
        }

        offset += entry_length;
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

void FICDecoder::process_fig0_20(const uint8_t *data, const size_t length, const bool pd) {
    size_t offset = 0;
    const size_t service_id_length = pd ? 4 : 2;

    while (offset < length) {
        if (offset + service_id_length + 4 > length) {
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
                return;
            }

            component.conditional_access = (data[offset] & 0x80) != 0;
            component.data = (data[offset] & 0x40) != 0;
            component.type = data[offset] & 0x3F;
            offset++;
        }

        if (offset + 3 > length) {
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
                return;
            }

            for (size_t index = 0; index < service_id_length; index++) {
                component.transfer_service_id = component.transfer_service_id << 8 | data[offset + index];
            }

            offset += service_id_length;
        }

        if (transfer_ensemble_present) {
            if (offset + 2 > length) {
                return;
            }

            component.transfer_ensemble_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
            offset += 2;
        }

        update_service_component(component_key);
    }
}

void FICDecoder::process_fig0_21(const uint8_t *data, const size_t length, const bool other_ensemble) {
    size_t offset = 0;

    while (offset + 2 <= length) {
        const uint16_t information_header = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const uint16_t region = information_header >> 5;
        const size_t information_length = information_header & 0x1F;
        offset += 2;

        if (offset + information_length > length) {
            return;
        }

        const size_t information_end = offset + information_length;

        while (offset + 3 <= information_end) {
            const uint16_t id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
            const uint8_t range_modulation = data[offset + 2] >> 4;
            const bool continuous = (data[offset + 2] & 0x08) != 0;
            const size_t frequency_length = data[offset + 2] & 0x07;
            offset += 3;

            if (offset + frequency_length > information_end) {
                return;
            }

            const uint64_t database_key = static_cast<uint64_t>(other_ensemble) << 63 | static_cast<uint64_t>(region) << 32 | static_cast<uint64_t>(id) << 16 | range_modulation;

            if (frequency_length == 0) {
                frequency_information.erase(database_key);
                continue;
            }

            std::vector<FrequencyInformation> frequencies;

            if (range_modulation == 0) {
                for (size_t index = 0; index + 3 <= frequency_length; index += 3) {
                    const uint32_t value = static_cast<uint32_t>(data[offset + index] << 16 | data[offset + index + 1] << 8 | data[offset + index + 2]);
                    frequencies.push_back({other_ensemble, id, "DAB", continuous, (value & 0x7FFFF) * 16, static_cast<uint8_t>(value >> 19)});
                }
            } else if (range_modulation == 8) {
                for (size_t index = 0; index < frequency_length; index++) {
                    frequencies.push_back({other_ensemble, id, "FM-RDS", continuous, static_cast<uint32_t>(87500 + data[offset + index] * 100), 0});
                }
            } else if (range_modulation == 6 || range_modulation == 14) {
                for (size_t index = 0; index + 3 <= frequency_length; index += 3) {
                    const uint16_t value = static_cast<uint16_t>(data[offset + index + 1] << 8 | data[offset + index + 2]);
                    const uint32_t multiplier = range_modulation == 6 && (value & 0x8000) != 0 ? 10 : 1;
                    frequencies.push_back({other_ensemble, static_cast<uint16_t>(id), range_modulation == 6 ? "DRM" : "AMSS", continuous, static_cast<uint32_t>(value & 0x7FFF) * multiplier, data[offset + index]});
                }
            }

            frequency_information[database_key] = std::move(frequencies);
            offset += frequency_length;
        }

        offset = information_end;
    }
}

void FICDecoder::process_fig0_24(const uint8_t *data, const size_t length, const bool pd, const bool other_ensemble) {
    size_t offset = 0;
    const size_t service_id_length = pd ? 4 : 2;

    while (offset + service_id_length + 1 <= length) {
        uint32_t service_id = 0;

        for (size_t index = 0; index < service_id_length; index++) {
            service_id = service_id << 8 | data[offset++];
        }

        const uint8_t service_header = data[offset++];
        const size_t ensemble_count = service_header & 0x0F;

        if (offset + ensemble_count * 2 > length) {
            return;
        }

        OtherEnsembleService service;
        service.service_id = service_id;
        service.data_service = pd;
        service.conditional_access_id = service_header >> 4 & 0x07;

        for (size_t index = 0; index < ensemble_count; index++) {
            service.ensemble_ids.push_back(static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]));
            offset += 2;
        }

        const uint64_t database_key = static_cast<uint64_t>(other_ensemble) << 63 | static_cast<uint64_t>(pd) << 32 | service_id;

        if (ensemble_count == 0) {
            other_ensemble_services.erase(database_key);
        } else {
            other_ensemble_services[database_key] = std::move(service);
        }
    }
}

void FICDecoder::process_fig0_25(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset + 5 <= length) {
        const uint16_t service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const uint16_t flags = static_cast<uint16_t>(data[offset + 2] << 8 | data[offset + 3]);
        const size_t target_count = (data[offset + 4] & 0x03) + 1;
        offset += 5;

        if (offset + target_count * 3 > length) {
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
            oe_announcement_support[key] = std::move(entry);
        }
    }
}

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

        if (flags == 0) {
            oe_announcement_switches.erase(announcement.cluster_id);
        } else {
            oe_announcement_switches[announcement.cluster_id] = std::move(announcement);
        }
    }
}

void FICDecoder::process_fig0_27(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset + 3 <= length) {
        FMAnnouncementSupport support;
        support.service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        const size_t pi_count = data[offset + 2] & 0x0F;
        offset += 3;

        if (offset + pi_count * 2 > length) {
            return;
        }

        for (size_t index = 0; index < pi_count; index++) {
            support.pi_codes.push_back(static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]));
            offset += 2;
        }

        if (pi_count == 0) {
            fm_announcement_support.erase(support.service_id);
        } else {
            fm_announcement_support[support.service_id] = std::move(support);
        }
    }
}

void FICDecoder::process_fig0_28(const uint8_t *data, const size_t length) {
    size_t offset = 0;

    while (offset + 4 <= length) {
        FMAnnouncementSwitch announcement;
        announcement.cluster_id = data[offset];
        announcement.newly_introduced = (data[offset + 1] & 0x80) != 0;
        announcement.region_id = data[offset + 1] & 0x3F;
        announcement.pi_code = static_cast<uint16_t>(data[offset + 2] << 8 | data[offset + 3]);
        offset += 4;
        fm_announcement_switches[announcement.cluster_id] = announcement;
    }
}

void FICDecoder::process_fig1(const uint8_t *data, const size_t length) {
    if (length < 1) {
        return;
    }

    const uint8_t header = data[0];
    const uint8_t charset = header >> 4;
    const bool oe = (header & 0x08) != 0;
    const uint8_t extension = header & 0x07;

    if (oe) {
        return;
    }

    if (extension == 0 || extension == 1) {
        if (length != 21) {
            return;
        }

        const uint16_t id = static_cast<uint16_t>(data[1] << 8 | data[2]);
        const std::string label = decode_label(data + 3, 16, charset);
        const uint16_t character_flags = static_cast<uint16_t>(data[19] << 8 | data[20]);
        const std::string short_label = decode_short_label(data + 3, 16, charset, character_flags);
        const uint64_t label_key = static_cast<uint64_t>(extension) << 60 | id;

        if (completed_extended_labels.contains(label_key)) {
            return;
        }

        if (extension == 0) {
            const Ensemble new_ensemble{id, label, short_label};

            if (!ensemble_valid || ensemble != new_ensemble) {
                ensemble = new_ensemble;
                ensemble_valid = true;
                update_ensemble();
            }
        } else {
            ServiceState &service = services[id];
            service.id = id;

            if (service.label != label || service.short_label != short_label) {
                service.label = label;
                service.short_label = short_label;
                update_service(id);

                for (auto &[component_key, component]: service_components) {
                    if (!component.data_service && component.service_id == id) {
                        update_service_component(component_key);
                    }
                }
            }
        }

        return;
    }

    if (extension == 4 || extension == 6) {
        if (length < 22) {
            return;
        }

        const bool data_service = (data[1] & 0x80) != 0;
        const uint8_t component_id = data[1] & 0x0F;
        const size_t service_id_length = data_service ? 4 : 2;
        const size_t application_length = extension == 6 ? 1 : 0;
        const size_t label_offset = 2 + service_id_length + application_length;

        if (label_offset + 18 != length) {
            return;
        }

        uint32_t service_id = 0;

        for (size_t index = 0; index < service_id_length; index++) {
            service_id = service_id << 8 | data[2 + index];
        }

        const std::string label = decode_label(data + label_offset, 16, charset);
        const uint16_t character_flags = static_cast<uint16_t>(data[label_offset + 16] << 8 | data[label_offset + 17]);
        const std::string short_label = decode_short_label(data + label_offset, 16, charset, character_flags);
        const uint64_t component_key = make_service_component_key(service_id, data_service, component_id);
        const uint8_t application = extension == 6 ? data[2 + service_id_length] & 0x1F : 0;
        const uint64_t label_key = static_cast<uint64_t>(extension) << 60 | (extension == 6 ? component_key << 8 | application : component_key);

        if (completed_extended_labels.contains(label_key)) {
            return;
        }

        ServiceComponent &component = service_components[component_key];
        component.service_id = service_id;
        component.data_service = data_service;
        component.id = component_id;

        if (extension == 4) {
            service_component_labels[component_key] = {label, short_label};
            component.label = label;
            component.short_label = short_label;
        } else {
            application_labels[component_key][application] = label;
        }

        update_service_component(component_key);
        return;
    }

    if (extension == 5) {
        if (length != 23) {
            return;
        }

        const uint32_t service_id = static_cast<uint32_t>(data[1]) << 24 | static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 8 | data[4];
        const std::string label = decode_label(data + 5, 16, charset);
        const uint16_t character_flags = static_cast<uint16_t>(data[21] << 8 | data[22]);
        const std::string short_label = decode_short_label(data + 5, 16, charset, character_flags);

        if (completed_extended_labels.contains(static_cast<uint64_t>(extension) << 60 | service_id)) {
            return;
        }

        data_service_labels[service_id] = {label, short_label};

        for (auto &[component_key, component]: service_components) {
            if (component.data_service && component.service_id == service_id) {
                component.label = label;
                component.short_label = short_label;
                update_service_component(component_key);
            }
        }
    }
}

void FICDecoder::process_fig2(const uint8_t *data, const size_t length) {
    if (length < 1) {
        return;
    }

    const bool toggle = (data[0] & 0x80) != 0;
    const uint8_t segment_index = data[0] >> 4 & 0x07;
    const uint8_t extension = data[0] & 0x07;
    size_t offset = 1;
    uint64_t label_key = static_cast<uint64_t>(extension) << 60;
    uint32_t service_id = 0;
    bool data_service = false;
    uint8_t component_id = 0;
    uint8_t application = 0;

    if (extension == 0 || extension == 1) {
        if (offset + 2 > length) {
            return;
        }

        service_id = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);
        offset += 2;
        label_key |= service_id;
    } else if (extension == 5) {
        if (offset + 4 > length) {
            return;
        }

        for (size_t index = 0; index < 4; index++) {
            service_id = service_id << 8 | data[offset++];
        }

        data_service = true;
        label_key |= service_id;
    } else if (extension == 4 || extension == 6) {
        if (offset >= length) {
            return;
        }

        data_service = (data[offset] & 0x80) != 0;
        component_id = data[offset] & 0x0F;
        offset++;
        const size_t service_id_length = data_service ? 4 : 2;

        if (offset + service_id_length + (extension == 6 ? 1 : 0) > length) {
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
        return;
    }

    ExtendedLabel &state = extended_labels[label_key];
    const bool any_segment = std::ranges::any_of(state.received, [](const bool received) { return received; });

    if (any_segment && state.toggle != toggle) {
        state = {};
        completed_extended_labels.erase(label_key);
    }

    state.toggle = toggle;

    if (segment_index == 0) {
        if (offset + 3 > length) {
            return;
        }

        state.utf16 = (data[offset] & 0x80) != 0;
        state.segment_count = data[offset] >> 4 & 0x07;
        state.character_flags = static_cast<uint16_t>(data[offset + 1] << 8 | data[offset + 2]);
        state.initialized = true;
        offset += 3;
    }

    state.segments[segment_index].assign(data + offset, data + length);
    state.received[segment_index] = true;

    if (!state.initialized) {
        return;
    }

    for (size_t index = 0; index <= state.segment_count; index++) {
        if (!state.received[index]) {
            return;
        }
    }

    std::vector<uint8_t> label_data;

    for (size_t index = 0; index <= state.segment_count; index++) {
        label_data.insert(label_data.end(), state.segments[index].begin(), state.segments[index].end());
    }

    const std::string label = decode_label(label_data.data(), label_data.size(), state.utf16 ? 6 : 15);
    const std::string short_label = decode_extended_short_label(label_data, state.utf16, state.character_flags);
    completed_extended_labels.insert(label_key);

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
    } else if (extension == 5) {
        data_service_labels[service_id] = {label, short_label};

        for (auto &[component_key, component]: service_components) {
            if (component.data_service && component.service_id == service_id) {
                update_service_component(component_key);
            }
        }
    } else {
        const uint64_t component_key = make_service_component_key(service_id, data_service, component_id);
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
    announcement_clusters.clear();
    update_alarm();
    apply_next_configuration();
}

void FICDecoder::apply_next_configuration() {
    auto configuration_figs = std::move(next_configuration_figs);
    next_configuration_figs.clear();
    next_configuration_valid = false;
    next_reconfiguration_counter = 0;

    for (const auto &figs: configuration_figs | std::views::values) {
        for (const auto &fig: figs) {
            process_fig0(fig.data(), fig.size(), false);
        }
    }
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

    for (const auto &cluster: announcement_clusters | std::views::values) {
        if ((cluster.flags & 0x0001) != 0) {
            new_alarm = true;
            break;
        }
    }

    if (alarm == new_alarm) {
        return;
    }

    alarm = new_alarm;

    if (alarm_callback) {
        alarm_callback(alarm);
    }
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
        result.assign(reinterpret_cast<const char *>(data), length);
    } else {
        return {};
    }

    while (!result.empty() && (result.back() == ' ' || result.back() == '\0')) {
        result.pop_back();
    }

    return result;
}

std::string FICDecoder::decode_short_label(const uint8_t *data, const size_t length, const uint8_t charset, const uint16_t flags) {
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

std::string FICDecoder::decode_ucs2(const uint8_t *data, const size_t length) {
    std::string result;

    for (size_t offset = 0; offset + 1 < length; offset += 2) {
        const uint16_t codepoint = static_cast<uint16_t>(data[offset] << 8 | data[offset + 1]);

        if (codepoint != 0) {
            append_utf8(result, codepoint);
        }
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

std::vector<std::string> FICDecoder::decode_announcement_flags(const uint16_t flags) {
    std::vector<std::string> result;

    for (uint8_t type = 0; type < 11; type++) {
        if ((flags & (1u << type)) != 0) {
            result.emplace_back(ANNOUNCEMENT_TYPES[type]);
        }
    }

    return result;
}

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

const char *FICDecoder::ANNOUNCEMENT_TYPES[11] = {
    "Alarm", "Traffic", "Transport", "Warning", "News", "Weather", "Event", "SpecialEvent", "ProgrammeInformation", "Sport", "Financial"
};
