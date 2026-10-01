//
// Created by tarek on 9/30/26.
//

#include "dbus_handler.h"
#include <format>
#include <vector>

namespace {
using DBusService = sdbus::Struct<uint16_t, std::string, std::string, uint8_t, uint8_t, bool, uint16_t, std::string, std::string, std::string, std::vector<std::string>, std::vector<std::string>>;
using DBusServiceComponent = sdbus::Struct<uint32_t, bool, uint8_t, std::string, std::string, uint8_t, uint16_t, bool, uint8_t, bool, uint16_t, uint8_t, std::string, bool, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint16_t, std::vector<std::string>, std::vector<std::string>>;

DBusService make_dbus_service(const FICDecoder::Service &service) {
    return {service.id, service.label, service.short_label, service.ecc, service.subchannel.id, service.dab_plus, service.subchannel.bitrate, service.subchannel.protection, service.language, service.pty, service.announcement_support, service.announcements};
}

DBusServiceComponent make_dbus_service_component(const FICDecoder::ServiceComponent &service_component) {
    return {service_component.service_id, service_component.data_service, service_component.id, service_component.label, service_component.short_label, service_component.subchannel, service_component.component_id, service_component.data, service_component.type, service_component.conditional_access, service_component.conditional_access_organization, service_component.fec_scheme, service_component.language, service_component.part_time, service_component.change_flags, service_component.change_day, service_component.change_hour, service_component.change_minute, service_component.change_second, service_component.transfer_service_id, service_component.transfer_ensemble_id, service_component.applications, service_component.application_labels};
}
}

DBusHandler::DBusHandler(const int id, const std::unique_ptr<sdbus::IConnection> &connection, TunerHandler *tuner_handler) {
    logger.set_id(id);
    this->tuner_handler = tuner_handler;

    object = sdbus::createObject(
        *connection,
        sdbus::ObjectPath{std::format("/de/multilan/dabdaemon/tuner{}", id)}
    );

    object->addVTable(
        sdbus::registerProperty("State").withGetter([this]() {
            return this->tuner_handler->get_state();
        }),
        sdbus::registerProperty("Frequency").withGetter([this]() {
            return this->tuner_handler->get_frequency();
        }),
        sdbus::registerProperty("Alarm").withGetter([this]() {
            return this->tuner_handler->get_alarm();
        }),
        sdbus::registerProperty("DateTime").withGetter([this]() {
            const auto date_time = this->tuner_handler->get_date_time();
            return sdbus::Struct<uint16_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint16_t>{date_time.year, date_time.month, date_time.day, date_time.hour, date_time.minute, date_time.second, date_time.milliseconds};
        }),
        sdbus::registerProperty("Ensemble").withGetter([this]() {
            return this->tuner_handler->get_ensemble();
        }),
        sdbus::registerProperty("EnsembleStatus").withGetter([this]() {
            const auto status = this->tuner_handler->get_ensemble_status();
            return sdbus::Struct<uint16_t, uint8_t, uint8_t, uint8_t, bool, uint8_t, uint16_t, uint16_t, uint8_t, int16_t, bool, uint8_t>{status.ensemble_id, status.service_count, status.services_found, status.change_flags, status.alarm_supported, status.occurrence_change, status.reconfiguration_counter, status.cif_counter, status.ecc, status.local_time_offset, status.lto_unique, status.international_table_id};
        }),
        sdbus::registerProperty("FICStatistics").withGetter([this]() {
            const auto statistics = this->tuner_handler->get_fic_statistics();
            const std::vector<uint32_t> fig_types(statistics.fig_types.begin(), statistics.fig_types.end());
            const std::vector<uint32_t> fig0_extensions(statistics.fig0_extensions.begin(), statistics.fig0_extensions.end());
            return sdbus::Struct<uint32_t, uint32_t, uint32_t, uint32_t, std::vector<uint32_t>, std::vector<uint32_t>>{statistics.fibs_received, statistics.fibs_valid, statistics.fibs_crc_errors, statistics.figs_received, fig_types, fig0_extensions};
        }),
        sdbus::registerProperty("Services").withGetter([this]() {
            std::vector<DBusService> result;

            for (const auto &service: this->tuner_handler->get_services()) {
                result.emplace_back(make_dbus_service(service));
            }

            return result;
        }),
        sdbus::registerProperty("ServiceComponents").withGetter([this]() {
            std::vector<DBusServiceComponent> result;

            for (const auto &service_component: this->tuner_handler->get_service_components()) {
                result.emplace_back(make_dbus_service_component(service_component));
            }

            return result;
        }),
        sdbus::registerProperty("ServiceLinks").withGetter([this]() {
            std::vector<sdbus::Struct<uint16_t, bool, bool, bool, bool, std::string, uint32_t, uint8_t>> result;

            for (const auto &link: this->tuner_handler->get_service_links()) {
                result.emplace_back(link.linkage_set, link.active, link.hard, link.shared, link.international, link.type, link.id, link.ecc);
            }

            return result;
        }),
        sdbus::registerProperty("FrequencyInformation").withGetter([this]() {
            std::vector<sdbus::Struct<bool, uint16_t, std::string, bool, uint32_t, uint8_t>> result;

            for (const auto &frequency: this->tuner_handler->get_frequency_information()) {
                result.emplace_back(frequency.other_ensemble, frequency.id, frequency.type, frequency.continuous, frequency.frequency, frequency.control);
            }

            return result;
        }),
        sdbus::registerProperty("OtherEnsembleServices").withGetter([this]() {
            std::vector<sdbus::Struct<uint32_t, bool, uint8_t, std::vector<uint16_t>>> result;

            for (const auto &service: this->tuner_handler->get_other_ensemble_services()) {
                result.emplace_back(service.service_id, service.data_service, service.conditional_access_id, service.ensemble_ids);
            }

            return result;
        }),
        sdbus::registerProperty("OEAnnouncementSupport").withGetter([this]() {
            std::vector<sdbus::Struct<uint16_t, std::vector<std::string>, uint8_t, uint16_t>> result;

            for (const auto &support: this->tuner_handler->get_oe_announcement_support()) {
                result.emplace_back(support.service_id, support.announcements, support.cluster_id, support.ensemble_id);
            }

            return result;
        }),
        sdbus::registerProperty("OEAnnouncementSwitches").withGetter([this]() {
            std::vector<sdbus::Struct<uint8_t, std::vector<std::string>, bool, uint8_t, uint16_t>> result;

            for (const auto &announcement: this->tuner_handler->get_oe_announcement_switches()) {
                result.emplace_back(announcement.cluster_id, announcement.announcements, announcement.newly_introduced, announcement.component_id, announcement.service_id);
            }

            return result;
        }),
        sdbus::registerProperty("FMAnnouncementSupport").withGetter([this]() {
            std::vector<sdbus::Struct<uint16_t, std::vector<uint16_t>>> result;

            for (const auto &support: this->tuner_handler->get_fm_announcement_support()) {
                result.emplace_back(support.service_id, support.pi_codes);
            }

            return result;
        }),
        sdbus::registerProperty("FMAnnouncementSwitches").withGetter([this]() {
            std::vector<sdbus::Struct<uint8_t, bool, uint8_t, uint16_t>> result;

            for (const auto &announcement: this->tuner_handler->get_fm_announcement_switches()) {
                result.emplace_back(announcement.cluster_id, announcement.newly_introduced, announcement.region_id, announcement.pi_code);
            }

            return result;
        }),
        sdbus::registerProperty("Service").withGetter([this]() {
            return make_dbus_service(this->tuner_handler->get_service());
        }),
        sdbus::registerProperty("AudioPipe").withGetter([this]() {
            return this->tuner_handler->get_audio_pipe();
        }),
        sdbus::registerProperty("SlideShow").withGetter([this]() {
            return this->tuner_handler->get_slideshow();
        }),
        sdbus::registerProperty("DLS").withGetter([this]() {
            return this->tuner_handler->get_dls();
        }),
        sdbus::registerProperty("DLP").withGetter([this]() {
            const auto dlp = this->tuner_handler->get_dlp();
            return sdbus::Struct<std::string, std::string, std::string, bool>{dlp.title, dlp.artist, dlp.album, dlp.item_running};
        }),
        sdbus::registerMethod("SetFrequency").withInputParamNames("frequency_khz").withOutputParamNames("success").implementedAs([this](const uint32_t frequency) {
            logger.debug(std::format("SetFrequency({})", frequency));
            return this->tuner_handler->set_frequency(frequency);
        }),
        sdbus::registerMethod("SetEnsemble").withInputParamNames("ensemble").withOutputParamNames("success").implementedAs([this](const std::string &ensemble) {
            logger.debug(std::format("SetEnsemble({})", ensemble));
            return this->tuner_handler->set_ensemble(ensemble);
        }),
        sdbus::registerMethod("SetService").withInputParamNames("service_id").withOutputParamNames("success").implementedAs([this](const uint16_t service_id) {
            logger.debug(std::format("SetService(0x{:04X})", service_id));
            return this->tuner_handler->set_service(service_id);
        }),
        sdbus::registerMethod("CloseService").withOutputParamNames("success").implementedAs([this]() {
            logger.debug("CloseService()");
            return this->tuner_handler->close_service();
        })
    ).forInterface("de.multilan.dabdaemon.Tuner1");

    logger.debug(std::format("Object registered at /de/multilan/dabdaemon/tuner{}", id));
}

DBusHandler::~DBusHandler() {
    object.reset();
    logger.debug("Object removed");
}
