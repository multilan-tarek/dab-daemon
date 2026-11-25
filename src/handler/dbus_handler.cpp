#include "dbus_handler.h"
#include "../config/features.h"
#include <format>
#include <map>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include "../helper/partial.h"

namespace {
using DBusService = sdbus::Struct<uint16_t, std::string, std::string, uint8_t, uint8_t, bool, uint16_t, std::string, std::string, std::string, std::vector<std::string>, std::vector<std::string>, std::string>;
using DBusUserApplication = sdbus::Struct<uint16_t, std::string, std::vector<uint8_t>, bool, uint8_t, bool, bool, uint16_t, bool, uint8_t, std::vector<uint8_t>>;
using DBusServiceComponent = sdbus::Struct<uint32_t, bool, uint8_t, std::string, std::string, uint8_t, uint16_t, bool, uint8_t, bool, uint16_t, uint8_t, std::string, bool, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint16_t, std::vector<std::string>, std::vector<std::string>, uint16_t, bool, bool, std::vector<DBusUserApplication>>;
#if DAB_ENABLE_SPI
using DBusSPIMultimedia = sdbus::Struct<std::string, std::string, std::string, std::string, uint16_t, uint16_t>;
using DBusServiceLogo = sdbus::Struct<uint32_t, uint8_t, uint16_t, std::vector<DBusSPIMultimedia>>;
using DBusSPIServiceInformation = sdbus::Struct<uint32_t, bool, uint8_t, uint8_t, uint16_t, std::string, std::string, std::string, std::string, std::string, std::string, std::vector<std::string>, std::vector<DBusSPIMultimedia>>;
using DBusSPIProgramme = sdbus::Struct<uint32_t, bool, uint8_t, std::string, uint32_t, std::string, std::string, std::string, std::string, std::string, std::string, std::string, uint32_t, std::string, uint32_t, std::vector<std::string>, std::vector<DBusSPIMultimedia>>;
using DBusSPIProgrammeGroup = sdbus::Struct<std::string, uint32_t, std::string, std::string, std::string, std::string, std::string, std::vector<std::string>, std::vector<DBusSPIMultimedia>>;
using DBusSPIAsset = sdbus::Struct<uint16_t, std::string, std::string, std::string>;
using DBusSPIObject = sdbus::Struct<uint16_t, uint32_t, uint8_t, uint16_t, std::string, std::string, int32_t, std::vector<uint8_t>, std::string>;
#endif

#if DAB_ENABLE_SLIDESHOW
using DBusSlideShowItem = sdbus::Struct<uint16_t, std::string, std::string, bool, bool, int64_t, bool, int64_t, uint8_t, uint8_t, std::string, std::string, std::string, uint8_t, bool>;
#endif

using DBusEnsemble = sdbus::Struct<std::string, uint32_t, uint16_t, uint8_t, std::string, std::string, std::vector<DBusService>>;
using DBusBandIIIChannel = sdbus::Struct<std::string, uint32_t>;
#if DAB_ENABLE_DIAGNOSTICS
using DBusFICFigure = sdbus::Struct<uint64_t, int64_t, uint8_t, int16_t, uint8_t, std::string, std::string, std::vector<uint8_t>, std::vector<sdbus::Struct<std::string, std::string>>>;
#endif
#if DAB_ENABLE_SCAN
using DBusScanProgress = sdbus::Struct<uint16_t, uint16_t, std::string, uint32_t>;
using DBusScanResult = sdbus::Struct<std::string, uint32_t, uint16_t, uint8_t, std::string, std::string, std::vector<DBusService>>;
#endif

#if DAB_ENABLE_FIDC
using DBusFIDCEntry = sdbus::Struct<bool, bool, uint8_t, uint8_t, std::string, std::vector<uint8_t>>;
#endif

#if DAB_ENABLE_EWS
using DBusEmergencyWarningLocation = sdbus::Struct<uint8_t, uint8_t, bool, std::vector<uint8_t>, uint16_t>;
using DBusEmergencyWarning = sdbus::Struct<bool, uint16_t, uint8_t, std::string, bool, uint8_t, bool, bool, std::string, uint8_t, std::vector<DBusEmergencyWarningLocation>>;
#endif

#if DAB_ENABLE_JOURNALINE
using DBusJournalineStatus = sdbus::Struct<bool, uint8_t, uint8_t, uint16_t, bool, uint8_t, uint16_t, uint16_t>;
using DBusJournalineIndexEntry = sdbus::Struct<uint16_t, uint8_t, bool, uint8_t, std::string>;
using DBusJournalineLink = sdbus::Struct<uint16_t, std::string>;
using DBusJournalineGeneralLink = sdbus::Struct<uint8_t, std::string, std::string>;
using DBusJournalineObject = sdbus::Struct<uint16_t, uint8_t, bool, uint8_t, std::string, std::string, std::vector<DBusJournalineLink>, std::vector<std::vector<std::string>>, std::vector<DBusJournalineGeneralLink>, std::string, bool, int64_t>;
#endif

#if DAB_ENABLE_TPEG
using DBusTPEGStatus = sdbus::Struct<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint16_t>;
using DBusTPEGServiceIdentifier = sdbus::Struct<uint8_t, uint8_t, uint8_t>;
using DBusTPEGComponent = sdbus::Struct<uint8_t, uint16_t, std::vector<uint8_t>>;
using DBusTPEGFrame = sdbus::Struct<uint64_t, uint8_t, DBusTPEGServiceIdentifier, uint8_t, std::vector<DBusTPEGComponent>, std::vector<uint8_t>>;
using DBusTPEGApplication = sdbus::Struct<uint8_t, uint8_t, uint16_t, uint8_t, uint8_t, uint32_t, uint8_t, std::string>;
using DBusTPEGServiceInformation = sdbus::Struct<DBusTPEGServiceIdentifier, std::string, std::string, uint8_t, uint8_t, std::vector<DBusTPEGApplication>>;
using DBusTPEGField = sdbus::Struct<std::string, std::string, std::string>;
using DBusTPEGMessage = sdbus::Struct<DBusTPEGServiceIdentifier, uint8_t, uint16_t, uint32_t, uint8_t, uint32_t, uint32_t, bool, uint8_t, uint64_t, std::string, std::vector<DBusTPEGField>, std::vector<uint8_t>>;
#endif


template<typename... ValueTypes>
sdbus::Struct<std::decay_t<ValueTypes>...> make_dbus_struct(ValueTypes &&...values) {
    using ValueTuple = std::tuple<std::decay_t<ValueTypes>...>;
    return sdbus::Struct<std::decay_t<ValueTypes>...>{ValueTuple{std::forward<ValueTypes>(values)...}};
}

DBusService make_dbus_service(const FICDecoder::Service &service, const std::string &logo_path = "") {
    return make_dbus_struct(service.id, service.label, service.short_label, service.ecc, service.subchannel.id, service.dab_plus, service.subchannel.bitrate, service.subchannel.protection, service.language, service.pty, service.announcement_support, service.announcements, logo_path);
}

DBusServiceComponent make_dbus_service_component(const FICDecoder::ServiceComponent &service_component) {
    std::vector<DBusUserApplication> user_applications;

    for (const auto &application: service_component.user_applications) {
        user_applications.emplace_back(make_dbus_struct(application.type, application.name, application.data, application.xpad, application.xpad_type, application.conditional_access, application.conditional_access_organization_present, application.conditional_access_organization, application.data_groups, application.data_type, application.parameters));
    }

    return make_dbus_struct(service_component.service_id, service_component.data_service, service_component.id, service_component.label, service_component.short_label, service_component.subchannel, service_component.component_id, service_component.data, service_component.type, service_component.conditional_access, service_component.conditional_access_organization, service_component.fec_scheme, service_component.language, service_component.part_time, service_component.change_flags, service_component.change_day, service_component.change_hour, service_component.change_minute, service_component.change_second, service_component.transfer_service_id, service_component.transfer_ensemble_id, service_component.applications, service_component.application_labels, service_component.packet_address, service_component.data_groups, service_component.conditional_access_organization_present, user_applications);
}

#if DAB_ENABLE_SPI
std::vector<DBusSPIMultimedia> make_dbus_spi_multimedia(const std::vector<SPIDecoder::Multimedia> &multimedia) {
    std::vector<DBusSPIMultimedia> result;
    result.reserve(multimedia.size());

    for (const auto &item: multimedia) {
        result.emplace_back(make_dbus_struct(item.url, item.path, item.mime_type, item.type, item.width, item.height));
    }

    return result;
}

DBusSPIServiceInformation make_dbus_spi_service_information(const SPIDecoder::ServiceInformation &service) {
    return make_dbus_struct(service.service_id, service.data_service, service.component_id, service.ecc, service.ensemble_id, service.short_name, service.medium_name, service.long_name, service.short_description, service.long_description, service.language, service.genres, make_dbus_spi_multimedia(service.multimedia));
}

DBusSPIProgramme make_dbus_spi_programme(const SPIDecoder::Programme &programme) {
    return make_dbus_struct(programme.service_id, programme.data_service, programme.component_id, programme.id, programme.short_id, programme.short_name, programme.medium_name, programme.long_name, programme.short_description, programme.long_description, programme.language, programme.start_time, programme.duration, programme.actual_time, programme.actual_duration, programme.genres, make_dbus_spi_multimedia(programme.multimedia));
}

DBusSPIProgrammeGroup make_dbus_spi_programme_group(const SPIDecoder::ProgrammeGroup &group) {
    return make_dbus_struct(group.id, group.short_id <= 0xFFFFFF ? group.short_id : 0, group.short_name, group.medium_name, group.long_name, group.short_description, group.long_description, group.genres, make_dbus_spi_multimedia(group.multimedia));
}
#endif

#if DAB_ENABLE_JOURNALINE
DBusJournalineObject make_dbus_journaline_object(const JournalineDecoder::Object &object) {
    std::vector<DBusJournalineLink> links;
    links.reserve(object.links.size());

    for (const auto &link: object.links) {
        links.emplace_back(make_dbus_struct(link.object_id, link.text));
    }

    std::vector<DBusJournalineGeneralLink> general_links;
    general_links.reserve(object.general_links.size());

    for (const auto &link: object.general_links) {
        general_links.emplace_back(make_dbus_struct(link.type, link.address, link.label));
    }

    return make_dbus_struct(object.id, object.type, object.static_content, object.revision, object.title, object.body, links, object.items, general_links, object.language, object.expiry_present, object.expiry);
}
#endif

#if DAB_ENABLE_TPEG
DBusTPEGFrame make_dbus_tpeg_frame(const TPEGDecoder::Frame &frame) {
    std::vector<DBusTPEGComponent> components;
    components.reserve(frame.components.size());

    for (const auto &component: frame.components) {
        components.emplace_back(make_dbus_struct(component.id, component.header_crc, component.data));
    }

    return make_dbus_struct(frame.sequence, frame.type, make_dbus_struct(frame.service.a, frame.service.b, frame.service.c), frame.encryption, components, frame.data);
}
#endif

constexpr std::string_view TUNER_INTERFACE = "de.multilan.dabdaemon.Tuner1";
constexpr std::string_view METADATA_INTERFACE = "de.multilan.dabdaemon.Metadata1";
constexpr std::string_view SIGNAL_INTERFACE = "de.multilan.dabdaemon.Signal1";
#if DAB_ENABLE_AUDIO
constexpr std::string_view AUDIO_INTERFACE = "de.multilan.dabdaemon.Audio1";
#endif
#if DAB_ENABLE_SLIDESHOW
constexpr std::string_view SLIDESHOW_INTERFACE = "de.multilan.dabdaemon.Slideshow1";
#endif
#if DAB_ENABLE_SPI
constexpr std::string_view SPI_INTERFACE = "de.multilan.dabdaemon.SPI1";
#endif
#if DAB_ENABLE_JOURNALINE
constexpr std::string_view JOURNALINE_INTERFACE = "de.multilan.dabdaemon.Journaline1";
#endif
#if DAB_ENABLE_TPEG
constexpr std::string_view TPEG_INTERFACE = "de.multilan.dabdaemon.TPEG1";
#endif
#if DAB_ENABLE_EWS
constexpr std::string_view EWS_INTERFACE = "de.multilan.dabdaemon.EWS1";
#endif
#if DAB_ENABLE_FIDC
constexpr std::string_view FIDC_INTERFACE = "de.multilan.dabdaemon.FIDC1";
#endif
#if DAB_ENABLE_ANNOUNCEMENTS
constexpr std::string_view ANNOUNCEMENTS_INTERFACE = "de.multilan.dabdaemon.Announcements1";
#endif
#if DAB_ENABLE_SERVICE_LINKING
constexpr std::string_view SERVICE_LINKING_INTERFACE = "de.multilan.dabdaemon.ServiceLinking1";
#endif
#if DAB_ENABLE_FREQUENCY_INFO
constexpr std::string_view FREQUENCY_INFO_INTERFACE = "de.multilan.dabdaemon.FrequencyInfo1";
#endif
#if DAB_ENABLE_SCAN
constexpr std::string_view SCAN_INTERFACE = "de.multilan.dabdaemon.Scan1";
#endif
#if DAB_ENABLE_DIAGNOSTICS
constexpr std::string_view DIAGNOSTICS_INTERFACE = "de.multilan.dabdaemon.Diagnostics1";
#endif

std::string_view interface_for_property(const std::string_view property) {
    if (property == "State" || property == "Frequency" || property == "BandIIIChannels" || property == "Ensemble" || property == "Ensembles" || property == "ServicesReady" || property == "EnsembleStatus" || property == "Services" || property == "ServiceComponents" || property == "Service") {
        return TUNER_INTERFACE;
    }

#if DAB_ENABLE_DATA_APPLICATIONS
    if (property == "DataApplication") {
        return TUNER_INTERFACE;
    }
#endif

#if DAB_ENABLE_AUDIO
    if (property == "AudioPipe" || property == "AudioCodec") {
        return AUDIO_INTERFACE;
    }
#if DAB_ENABLE_DIAGNOSTICS
    if (property == "AudioStatistics") {
        return AUDIO_INTERFACE;
    }
#endif
#endif

    if (property == "DateTime" || property == "DLS" || property == "DLP") {
        return METADATA_INTERFACE;
    }

    if (property == "SignalStatus") {
        return SIGNAL_INTERFACE;
    }

#if DAB_ENABLE_SLIDESHOW
    if (property == "SlideShow" || property == "SlideShowItems") {
        return SLIDESHOW_INTERFACE;
    }
#endif

#if DAB_ENABLE_SPI
    if (property == "SPIRevision" || property == "SPIAssets" || property == "SPIObjects" || property == "ServiceLogos") {
        return SPI_INTERFACE;
    }
#endif

#if DAB_ENABLE_JOURNALINE
    if (property == "JournalineRevision" || property == "JournalineStatus") {
        return JOURNALINE_INTERFACE;
    }
#endif

#if DAB_ENABLE_TPEG
    if (property == "TPEGRevision" || property == "TPEGStatus" || property == "TPEGReceiving") {
        return TPEG_INTERFACE;
    }
#endif

#if DAB_ENABLE_EWS
    if (property == "EmergencyWarning") {
        return EWS_INTERFACE;
    }
#endif

#if DAB_ENABLE_FIDC
    if (property == "FIDC") {
        return FIDC_INTERFACE;
    }
#endif

#if DAB_ENABLE_ANNOUNCEMENTS
    if (property == "OEAnnouncementSupport" || property == "OEAnnouncementSwitches" || property == "FMAnnouncementSupport" || property == "FMAnnouncementSwitches") {
        return ANNOUNCEMENTS_INTERFACE;
    }
#endif

#if DAB_ENABLE_SERVICE_LINKING
    if (property == "ServiceLinks" || property == "OtherEnsembleServices") {
        return SERVICE_LINKING_INTERFACE;
    }
#endif

#if DAB_ENABLE_FREQUENCY_INFO
    if (property == "FrequencyInformation") {
        return FREQUENCY_INFO_INTERFACE;
    }
#endif

#if DAB_ENABLE_SCAN
    if (property == "ScanState" || property == "ScanMode" || property == "ScanModes" || property == "ScanProgress" || property == "ScanResults") {
        return SCAN_INTERFACE;
    }
#endif

#if DAB_ENABLE_DIAGNOSTICS
    if (property == "FICStatistics" || property == "HardwareStatus" || property == "FICRevision") {
        return DIAGNOSTICS_INTERFACE;
    }
#if DAB_ENABLE_SLIDESHOW
    if (property == "SlideShowStatistics") {
        return DIAGNOSTICS_INTERFACE;
    }
#endif
#if DAB_ENABLE_PACKET
    if (property == "DataPathStatistics") {
        return DIAGNOSTICS_INTERFACE;
    }
#endif
#endif

    return {};
}

bool property_was_compiled_out(const std::string_view property) {
    (void)property;
#if !DAB_ENABLE_AUDIO || !DAB_ENABLE_DIAGNOSTICS
    if (property == "AudioStatistics") {
        return true;
    }
#endif
#if !DAB_ENABLE_DATA_APPLICATIONS
    if (property == "DataApplication") {
        return true;
    }
#endif
#if !DAB_ENABLE_AUDIO
    if (property == "AudioPipe" || property == "AudioCodec") {
        return true;
    }
#endif
#if !DAB_ENABLE_SLIDESHOW
    if (property == "SlideShow" || property == "SlideShowItems" || property == "SlideShowStatistics") {
        return true;
    }
#endif
#if !DAB_ENABLE_SPI
    if (property == "SPIRevision" || property == "SPIAssets" || property == "SPIObjects" || property == "ServiceLogos") {
        return true;
    }
#endif
#if !DAB_ENABLE_JOURNALINE
    if (property == "JournalineRevision" || property == "JournalineStatus") {
        return true;
    }
#endif
#if !DAB_ENABLE_TPEG
    if (property == "TPEGRevision" || property == "TPEGStatus" || property == "TPEGReceiving") {
        return true;
    }
#endif
#if !DAB_ENABLE_EWS
    if (property == "EmergencyWarning") {
        return true;
    }
#endif
#if !DAB_ENABLE_FIDC
    if (property == "FIDC") {
        return true;
    }
#endif
#if !DAB_ENABLE_ANNOUNCEMENTS
    if (property == "OEAnnouncementSupport" || property == "OEAnnouncementSwitches" || property == "FMAnnouncementSupport" || property == "FMAnnouncementSwitches") {
        return true;
    }
#endif
#if !DAB_ENABLE_SERVICE_LINKING
    if (property == "ServiceLinks" || property == "OtherEnsembleServices") {
        return true;
    }
#endif
#if !DAB_ENABLE_FREQUENCY_INFO
    if (property == "FrequencyInformation") {
        return true;
    }
#endif
#if !DAB_ENABLE_SCAN
    if (property == "ScanState" || property == "ScanMode" || property == "ScanModes" || property == "ScanProgress" || property == "ScanResults") {
        return true;
    }
#endif
#if !DAB_ENABLE_DIAGNOSTICS
    if (property == "FICStatistics" || property == "HardwareStatus" || property == "FICRevision" || property == "DataPathStatistics" || property == "SlideShowStatistics") {
        return true;
    }
#elif !DAB_ENABLE_PACKET
    if (property == "DataPathStatistics") {
        return true;
    }
#endif

    return false;
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
        sdbus::registerProperty("BandIIIChannels").withGetter([this]() {
            std::vector<DBusBandIIIChannel> result;

            for (const auto &channel: this->tuner_handler->get_band_iii_channels()) {
                result.emplace_back(make_dbus_struct(std::string(channel.name), channel.frequency));
            }

            return result;
        }).withUpdateBehavior(sdbus::Flags::CONST_PROPERTY_VALUE),
        sdbus::registerProperty("Ensemble").withGetter([this]() {
            return this->tuner_handler->get_ensemble();
        }),
        sdbus::registerProperty("Ensembles").withGetter([this]() {
            std::vector<DBusEnsemble> result;
            for (const auto &ensemble: this->tuner_handler->get_ensembles()) {
                std::vector<DBusService> services;
                for (const auto &service: ensemble.services) {
                    services.emplace_back(make_dbus_service(service));
                }
                result.emplace_back(make_dbus_struct(ensemble.channel, ensemble.frequency, ensemble.ensemble_id, ensemble.ecc, ensemble.label, ensemble.short_label, services));
            }
            return result;
        }),
        sdbus::registerProperty("ServicesReady").withGetter([this]() {
            return this->tuner_handler->get_services_ready();
        }),
        sdbus::registerProperty("EnsembleStatus").withGetter([this]() {
            const auto status = this->tuner_handler->get_ensemble_status();
            return make_dbus_struct(status.ensemble_id, status.service_count, status.services_found, status.change_flags, status.alarm_supported, status.alarm_active, status.occurrence_change, status.reconfiguration_counter, status.cif_counter, status.ecc, status.local_time_offset, status.lto_unique, status.international_table_id);
        }),
        sdbus::registerProperty("Services").withGetter([this]() {
            std::vector<DBusService> result;
            std::map<uint32_t, std::string> logo_paths;
#if DAB_ENABLE_SPI
            logo_paths = this->tuner_handler->get_logo_paths();
#endif

            for (const auto &service: this->tuner_handler->get_services()) {
                result.emplace_back(make_dbus_service(service, logo_paths.contains(service.id) ? logo_paths.at(service.id) : ""));
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
        sdbus::registerProperty("Service").withGetter([this]() {
            const auto service = this->tuner_handler->get_service();
            std::map<uint32_t, std::string> logo_paths;
#if DAB_ENABLE_SPI
            logo_paths = this->tuner_handler->get_logo_paths();
#endif
            return make_dbus_service(service, logo_paths.contains(service.id) ? logo_paths.at(service.id) : "");
        }),
#if DAB_ENABLE_DATA_APPLICATIONS
        sdbus::registerProperty("DataApplication").withGetter([this]() {
            return this->tuner_handler->get_data_application();
        }),
#endif
        sdbus::registerMethod("SetFrequency").withInputParamNames("frequency_khz").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result, const uint32_t frequency) {
            logger.debug(std::format("SetFrequency({})", frequency));
            queue_command(std::move(result), [this, frequency]() {
                return this->tuner_handler->set_frequency(frequency);
            });
        }),
        sdbus::registerMethod("SetEnsemble").withInputParamNames("ensemble").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result, const std::string &ensemble) {
            logger.debug(std::format("SetEnsemble({})", ensemble));
            queue_command(std::move(result), [this, ensemble]() {
                return this->tuner_handler->set_ensemble(ensemble);
            });
        }),
        sdbus::registerMethod("SetService").withInputParamNames("service_id").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result, const uint16_t service_id) {
            logger.debug(std::format("SetService(0x{:04X})", service_id));
            queue_command(std::move(result), [this, service_id]() {
                return this->tuner_handler->set_service(service_id);
            });
        }),
#if DAB_ENABLE_DATA_APPLICATIONS
        sdbus::registerMethod("SetDataService").withInputParamNames("service_id", "data_service", "component_id", "application_type").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result, const uint32_t service_id, const bool data_service, const uint8_t component_id, const uint16_t application_type) {
            logger.debug(std::format("SetDataService(0x{:X}, {}, {}, 0x{:03X})", service_id, data_service, component_id, application_type));
            queue_command(std::move(result), [this, service_id, data_service, component_id, application_type]() {
                return this->tuner_handler->set_data_service(service_id, data_service, component_id, application_type);
            });
        }),
#endif
        sdbus::registerMethod("CloseService").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result) {
            logger.debug("CloseService()");
            queue_command(std::move(result), [this]() {
                return this->tuner_handler->close_service();
            });
        })
    ).forInterface(std::string(TUNER_INTERFACE));

    object->addVTable(
        sdbus::registerProperty("SignalStatus").withGetter([this]() {
            const auto status = this->tuner_handler->get_signal_status();
            return make_dbus_struct(status.ofdm_lock, status.fec_lock, status.fec_sync, status.signal_quality_valid, status.signal_quality, status.antenna_level, status.msc_cer_valid, status.msc_cer, status.fic_crc_error_rate_valid, status.fic_crc_error_rate);
        })
    ).forInterface(std::string(SIGNAL_INTERFACE));

    object->addVTable(
        sdbus::registerProperty("DateTime").withGetter([this]() {
            const auto date_time = this->tuner_handler->get_date_time();
            return make_dbus_struct(date_time.year, date_time.month, date_time.day, date_time.hour, date_time.minute, date_time.second, date_time.milliseconds);
        }),
        sdbus::registerProperty("DLS").withGetter([this]() {
            return this->tuner_handler->get_dls();
        }),
        sdbus::registerProperty("DLP").withGetter([this]() {
            const auto dlp = this->tuner_handler->get_dlp();
            return make_dbus_struct(dlp.title, dlp.artist, dlp.album, dlp.item_running);
        })
    ).forInterface(std::string(METADATA_INTERFACE));

#if DAB_ENABLE_AUDIO
    object->addVTable(
        sdbus::registerProperty("AudioPipe").withGetter([this]() {
            return this->tuner_handler->get_audio_pipe();
        }).withUpdateBehavior(sdbus::Flags::CONST_PROPERTY_VALUE),
        sdbus::registerProperty("AudioCodec").withGetter([this]() {
            return this->tuner_handler->get_audio_codec();
        })
#if DAB_ENABLE_DIAGNOSTICS
        , sdbus::registerProperty("AudioStatistics").withGetter([this]() {
            const auto statistics = this->tuner_handler->get_audio_statistics();
            return make_dbus_struct(statistics.sampling_rate, statistics.channels, statistics.sbr,
                                    statistics.frames_received, statistics.frames_output, statistics.frames_discarded,
                                    statistics.rs_corrected, statistics.rs_uncorrectable, statistics.crc_errors,
                                    statistics.sync_losses, statistics.dropouts, statistics.rs_available, statistics.crc_available);
        })
#endif
    ).forInterface(std::string(AUDIO_INTERFACE));
#endif


#if DAB_ENABLE_SLIDESHOW
    object->addVTable(
        sdbus::registerProperty("SlideShow").withGetter([this]() {
            return this->tuner_handler->get_slideshow();
        }),
        sdbus::registerProperty("SlideShowItems").withGetter([this]() {
            std::vector<DBusSlideShowItem> result;

            for (const auto &item: this->tuner_handler->get_slideshow_items()) {
                result.emplace_back(make_dbus_struct(item.transport_id, item.content_name, item.path, item.trigger_time_present, item.trigger_now, item.trigger_time, item.expire_time_present, item.expire_time, item.category_id, item.slide_id, item.category_title, item.click_through_url, item.alternative_location_url, item.alert, item.active));
            }

            return result;
        })
    ).forInterface(std::string(SLIDESHOW_INTERFACE));
#endif


#if DAB_ENABLE_SPI
    object->addVTable(
        sdbus::registerProperty("SPIRevision").withGetter([this]() {
            return this->tuner_handler->get_spi_revision();
        }),
        sdbus::registerProperty("ServiceLogos").withGetter([this]() {
            std::vector<DBusServiceLogo> result;

            for (const auto &logo: this->tuner_handler->get_service_logos()) {
                result.emplace_back(make_dbus_struct(logo.service_id, logo.ecc, logo.ensemble_id, make_dbus_spi_multimedia(logo.multimedia)));
            }

            return result;
        }),
        sdbus::registerProperty("SPIAssets").withGetter([this]() {
            std::vector<DBusSPIAsset> result;

            for (const auto &asset: this->tuner_handler->get_spi_assets()) {
                result.emplace_back(make_dbus_struct(asset.transport_id, asset.content_name, asset.path, asset.mime_type));
            }

            return result;
        }),
        sdbus::registerProperty("SPIObjects").withGetter([this]() {
            std::vector<DBusSPIObject> result;

            for (const auto &spi_object: this->tuner_handler->get_spi_objects()) {
                result.emplace_back(make_dbus_struct(spi_object.transport_id, spi_object.body_size, spi_object.content_type, spi_object.content_subtype, spi_object.content_name, spi_object.mime_type, static_cast<int32_t>(spi_object.compression_type), spi_object.profile_subset, spi_object.decode_status));
            }

            return result;
        }),
        sdbus::registerMethod("GetServiceInformation").withInputParamNames("service_id", "data_service", "component_id").withOutputParamNames("services").implementedAs([this](const uint32_t service_id, const bool data_service, const uint8_t component_id) {
            std::vector<DBusSPIServiceInformation> result;

            for (const auto &service: this->tuner_handler->get_service_information(service_id, data_service, component_id)) {
                result.emplace_back(make_dbus_spi_service_information(service));
            }

            return result;
        }),
        sdbus::registerMethod("GetProgramme").withInputParamNames("service_id", "data_service", "component_id", "timestamp").withOutputParamNames("programmes").implementedAs([this](const uint32_t service_id, const bool data_service, const uint8_t component_id, const int64_t timestamp) {
            std::vector<DBusSPIProgramme> result;

            for (const auto &programme: this->tuner_handler->get_programme(service_id, data_service, component_id, timestamp)) {
                result.emplace_back(make_dbus_spi_programme(programme));
            }

            return result;
        }),
        sdbus::registerMethod("GetSchedule").withInputParamNames("service_id", "data_service", "component_id", "from", "to").withOutputParamNames("programmes").implementedAs([this](const uint32_t service_id, const bool data_service, const uint8_t component_id, const int64_t from, const int64_t to) {
            std::vector<DBusSPIProgramme> result;

            for (const auto &programme: this->tuner_handler->get_schedule(service_id, data_service, component_id, from, to)) {
                result.emplace_back(make_dbus_spi_programme(programme));
            }

            return result;
        }),
        sdbus::registerMethod("GetProgrammeGroups").withOutputParamNames("groups").implementedAs([this]() {
            std::vector<DBusSPIProgrammeGroup> result;

            for (const auto &group: this->tuner_handler->get_programme_groups()) {
                result.emplace_back(make_dbus_spi_programme_group(group));
            }

            return result;
        }),
        sdbus::registerMethod("GetGroupProgrammes").withInputParamNames("group_id", "short_id").withOutputParamNames("programmes").implementedAs([this](const std::string &group_id, const uint32_t short_id) {
            std::vector<DBusSPIProgramme> result;

            for (const auto &programme: this->tuner_handler->get_group_programmes(group_id, short_id)) {
                result.emplace_back(make_dbus_spi_programme(programme));
            }

            return result;
        })
    ).forInterface(std::string(SPI_INTERFACE));
#endif


#if DAB_ENABLE_JOURNALINE
    object->addVTable(
        sdbus::registerProperty("JournalineRevision").withGetter([this]() {
            return this->tuner_handler->get_journaline_revision();
        }),
        sdbus::registerProperty("JournalineStatus").withGetter([this]() {
            const auto status = this->tuner_handler->get_journaline_status();
            return make_dbus_struct(status.configured, status.version, status.extended_header_length, status.point_of_entry, status.toc_present, status.toc_revision, status.object_count, status.timeout_minutes);
        }),
        sdbus::registerMethod("GetJournalineIndex").withOutputParamNames("objects").implementedAs([this]() {
            std::vector<DBusJournalineIndexEntry> result;

            for (const auto &journaline_object: this->tuner_handler->get_journaline_index()) {
                result.emplace_back(make_dbus_struct(journaline_object.id, journaline_object.type, journaline_object.static_content, journaline_object.revision, journaline_object.title));
            }

            return result;
        }),
        sdbus::registerMethod("GetJournalineObject").withInputParamNames("object_id").withOutputParamNames("objects").implementedAs([this](const uint16_t object_id) {
            std::vector<DBusJournalineObject> result;

            for (const auto &journaline_object: this->tuner_handler->get_journaline_object(object_id)) {
                result.emplace_back(make_dbus_journaline_object(journaline_object));
            }

            return result;
        })
    ).forInterface(std::string(JOURNALINE_INTERFACE));
#endif


#if DAB_ENABLE_TPEG
    object->addVTable(
        sdbus::registerProperty("TPEGReceiving").withGetter([this]() {
            return this->tuner_handler->get_tpeg_receiving();
        }),
        sdbus::registerMethod("StopTPEG").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result) {
            queue_command(std::move(result), [this]() {
                return this->tuner_handler->stop_tpeg();
            });
        }),
        sdbus::registerProperty("TPEGRevision").withGetter([this]() {
            return this->tuner_handler->get_tpeg_revision();
        }),
        sdbus::registerProperty("TPEGStatus").withGetter([this]() {
            const auto status = this->tuner_handler->get_tpeg_status();
            return make_dbus_struct(status.frames_received, status.groups_received, status.group_crc_errors, status.transport_crc_errors, status.directory_crc_errors, status.component_crc_errors, status.continuity_errors, status.malformed_frames, status.service_count);
        }),
        sdbus::registerMethod("GetTPEGServices").withOutputParamNames("services").implementedAs([this]() {
            std::vector<DBusTPEGServiceIdentifier> result;

            for (const auto &service: this->tuner_handler->get_tpeg_services()) {
                result.emplace_back(make_dbus_struct(service.a, service.b, service.c));
            }

            return result;
        }),
        sdbus::registerMethod("GetTPEGContentStatus").withOutputParamNames("status").implementedAs([this]() {
            const auto status = this->tuner_handler->get_tpeg_content_status();
            return make_dbus_struct(status.messages_received, status.data_crc_errors, status.malformed_components, status.unsupported_components);
        }),
        sdbus::registerMethod("GetTPEGServiceInformation").withOutputParamNames("services").implementedAs([this]() {
            std::vector<DBusTPEGServiceInformation> result;

            for (const auto &service: this->tuner_handler->get_tpeg_service_information()) {
                std::vector<DBusTPEGApplication> applications;

                for (const auto &application: service.applications) {
                    applications.emplace_back(make_dbus_struct(application.component_id, application.content_id, application.id, application.major_version, application.minor_version, application.message_count, application.encryption, application.description));
                }

                result.emplace_back(make_dbus_struct(make_dbus_struct(service.id.a, service.id.b, service.id.c), service.name, service.description, service.table_version, service.character_encoding, applications));
            }

            return result;
        }),
        sdbus::registerMethod("GetTPEGMessages").withOutputParamNames("messages").implementedAs([this]() {
            std::vector<DBusTPEGMessage> result;

            for (const auto &message: this->tuner_handler->get_tpeg_messages()) {
                std::vector<DBusTPEGField> fields;

                for (const auto &field: message.fields) {
                    fields.emplace_back(make_dbus_struct(field.component, field.name, field.value));
                }

                result.emplace_back(make_dbus_struct(make_dbus_struct(message.service.a, message.service.b, message.service.c), message.component_id, message.application_id, message.id, message.version, message.expiry, message.generation_time, message.cancelled, message.priority, message.sequence, message.summary, fields, message.data));
            }

            return result;
        }),
        sdbus::registerMethod("GetTPEGFrames").withInputParamNames("since_sequence", "limit").withOutputParamNames("frames").implementedAs([this](const uint64_t since_sequence, const uint16_t limit) {
            std::vector<DBusTPEGFrame> result;

            for (const auto &frame: this->tuner_handler->get_tpeg_frames(since_sequence, limit)) {
                result.emplace_back(make_dbus_tpeg_frame(frame));
            }

            return result;
        })
    ).forInterface(std::string(TPEG_INTERFACE));
#endif


#if DAB_ENABLE_EWS
    object->addVTable(
        sdbus::registerProperty("EmergencyWarning").withGetter([this]() {
            const auto status = this->tuner_handler->get_emergency_warning_status();
            std::vector<DBusEmergencyWarning> alerts;

            for (const auto &alert: status.alerts) {
                std::vector<DBusEmergencyWarningLocation> locations;

                for (const auto &location: alert.locations) {
                    locations.emplace_back(make_dbus_struct(location.nff, location.zone, location.sub_codes, location.digits, location.sub_code_mask));
                }

                alerts.emplace_back(make_dbus_struct(alert.other_ensemble, alert.ensemble_id, alert.subchannel, alert.phase, alert.trigger_second_present, alert.trigger_second, alert.status_present, alert.last, alert.stage, alert.incident_id, locations));
            }

            return make_dbus_struct(status.supported, alerts);
        })
    ).forInterface(std::string(EWS_INTERFACE));
#endif


#if DAB_ENABLE_FIDC
    object->addVTable(
        sdbus::registerProperty("FIDC").withGetter([this]() {
            std::vector<DBusFIDCEntry> result;

            for (const auto &entry: this->tuner_handler->get_fidc_entries()) {
                result.emplace_back(make_dbus_struct(entry.d1, entry.d2, entry.component_id, entry.extension, entry.type, entry.data));
            }

            return result;
        })
    ).forInterface(std::string(FIDC_INTERFACE));
#endif


#if DAB_ENABLE_ANNOUNCEMENTS
    object->addVTable(
        sdbus::registerProperty("OEAnnouncementSupport").withGetter([this]() {
            std::vector<sdbus::Struct<uint16_t, std::vector<std::string>, uint8_t, uint16_t>> result;

            for (const auto &support: this->tuner_handler->get_oe_announcement_support()) {
                result.emplace_back(make_dbus_struct(support.service_id, support.announcements, support.cluster_id, support.ensemble_id));
            }

            return result;
        }),
        sdbus::registerProperty("OEAnnouncementSwitches").withGetter([this]() {
            std::vector<sdbus::Struct<uint8_t, std::vector<std::string>, bool, uint8_t, uint16_t>> result;

            for (const auto &announcement: this->tuner_handler->get_oe_announcement_switches()) {
                result.emplace_back(make_dbus_struct(announcement.cluster_id, announcement.announcements, announcement.newly_introduced, announcement.component_id, announcement.service_id));
            }

            return result;
        }),
        sdbus::registerProperty("FMAnnouncementSupport").withGetter([this]() {
            std::vector<sdbus::Struct<uint16_t, std::vector<uint16_t>>> result;

            for (const auto &support: this->tuner_handler->get_fm_announcement_support()) {
                result.emplace_back(make_dbus_struct(support.service_id, support.pi_codes));
            }

            return result;
        }),
        sdbus::registerProperty("FMAnnouncementSwitches").withGetter([this]() {
            std::vector<sdbus::Struct<uint8_t, bool, uint8_t, uint16_t>> result;

            for (const auto &announcement: this->tuner_handler->get_fm_announcement_switches()) {
                result.emplace_back(make_dbus_struct(announcement.cluster_id, announcement.newly_introduced, announcement.region_id, announcement.pi_code));
            }

            return result;
        })
    ).forInterface(std::string(ANNOUNCEMENTS_INTERFACE));
#endif


#if DAB_ENABLE_SERVICE_LINKING
    object->addVTable(
        sdbus::registerProperty("ServiceLinks").withGetter([this]() {
            std::vector<sdbus::Struct<uint16_t, bool, bool, bool, bool, std::string, uint32_t, uint8_t>> result;

            for (const auto &link: this->tuner_handler->get_service_links()) {
                result.emplace_back(make_dbus_struct(link.linkage_set, link.active, link.hard, link.shared, link.international, link.type, link.id, link.ecc));
            }

            return result;
        }),
        sdbus::registerProperty("OtherEnsembleServices").withGetter([this]() {
            std::vector<sdbus::Struct<uint32_t, bool, uint8_t, std::vector<uint16_t>>> result;

            for (const auto &service: this->tuner_handler->get_other_ensemble_services()) {
                result.emplace_back(make_dbus_struct(service.service_id, service.data_service, service.conditional_access_id, service.ensemble_ids));
            }

            return result;
        })
    ).forInterface(std::string(SERVICE_LINKING_INTERFACE));
#endif


#if DAB_ENABLE_FREQUENCY_INFO
    object->addVTable(
        sdbus::registerProperty("FrequencyInformation").withGetter([this]() {
            std::vector<sdbus::Struct<bool, uint16_t, std::string, bool, uint32_t, uint8_t>> result;

            for (const auto &frequency: this->tuner_handler->get_frequency_information()) {
                result.emplace_back(make_dbus_struct(frequency.other_ensemble, frequency.id, frequency.type, frequency.continuous, frequency.frequency, frequency.control));
            }

            return result;
        })
    ).forInterface(std::string(FREQUENCY_INFO_INTERFACE));
#endif


#if DAB_ENABLE_SCAN
    object->addVTable(
        sdbus::registerProperty("ScanState").withGetter([this]() {
            return this->tuner_handler->get_scan_state();
        }),
        sdbus::registerProperty("ScanMode").withGetter([this]() {
            return this->tuner_handler->get_scan_mode();
        }),
        sdbus::registerProperty("ScanModes").withGetter([this]() {
            return this->tuner_handler->get_scan_modes();
        }).withUpdateBehavior(sdbus::Flags::CONST_PROPERTY_VALUE),
        sdbus::registerProperty("ScanProgress").withGetter([this]() {
            const auto progress = this->tuner_handler->get_scan_progress();
            return make_dbus_struct(progress.current, progress.total, progress.channel, progress.frequency);
        }),
        sdbus::registerProperty("ScanResults").withGetter([this]() {
            std::vector<DBusScanResult> result;

            for (const auto &scan_result: this->tuner_handler->get_scan_results()) {
                std::vector<DBusService> services;
                services.reserve(scan_result.services.size());

                for (const auto &service: scan_result.services) {
                    services.emplace_back(make_dbus_service(service));
                }

                result.emplace_back(make_dbus_struct(scan_result.channel, scan_result.frequency, scan_result.ensemble_id, scan_result.ecc, scan_result.label, scan_result.short_label, services));
            }

            return result;
        }),
        sdbus::registerMethod("StartScan").withInputParamNames("mode").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result, const std::string &mode) {
            logger.debug(std::format("StartScan({})", mode));
            queue_command(std::move(result), [this, mode]() {
                return this->tuner_handler->start_scan(mode);
            });
        }),
#if DAB_ENABLE_SEEK
        sdbus::registerMethod("SeekUp").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result) {
            logger.debug("SeekUp()");
            queue_command(std::move(result), [this]() {
                return this->tuner_handler->seek_up();
            });
        }),
        sdbus::registerMethod("SeekDown").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result) {
            logger.debug("SeekDown()");
            queue_command(std::move(result), [this]() {
                return this->tuner_handler->seek_down();
            });
        }),
        sdbus::registerMethod("StopSeek").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result) {
            logger.debug("StopSeek()");
            queue_command(std::move(result), [this]() {
                return this->tuner_handler->stop_seek();
            });
        }),
#endif
        sdbus::registerMethod("StopScan").withOutputParamNames("success").implementedAs([this](sdbus::Result<bool> result) {
            logger.debug("StopScan()");
            queue_command(std::move(result), [this]() {
                return this->tuner_handler->stop_scan();
            });
        })
    ).forInterface(std::string(SCAN_INTERFACE));
#endif


#if DAB_ENABLE_DIAGNOSTICS
    object->addVTable(
        sdbus::registerProperty("FICRevision").withGetter([this]() {
            return this->tuner_handler->get_fic_revision();
        }),
        sdbus::registerMethod("GetFICFigures").withInputParamNames("since_sequence", "limit").withOutputParamNames("generation", "oldest_sequence", "revision", "figures").implementedAs([this](const uint64_t since_sequence, const uint16_t limit) {
            const auto history = this->tuner_handler->get_fic_figures(since_sequence, limit);
            std::vector<DBusFICFigure> figures;
            figures.reserve(history.figures.size());

            for (const auto &figure: history.figures) {
                std::vector<sdbus::Struct<std::string, std::string>> fields;
                fields.reserve(figure.fields.size());

                for (const auto &[name, value]: figure.fields) {
                    fields.emplace_back(make_dbus_struct(name, value));
                }

                figures.emplace_back(make_dbus_struct(figure.sequence, figure.received, figure.type, figure.extension, figure.flags, figure.name, figure.status, figure.data, std::move(fields)));
            }

            return std::make_tuple(history.generation, history.oldest, history.revision, std::move(figures));
        }),
        sdbus::registerProperty("HardwareStatus").withGetter([this]() {
            const auto status = this->tuner_handler->get_hardware_status();
            return make_dbus_struct(status.usb.last_error, std::string(libusb_error_name(status.usb.last_error)), status.usb.last_error_time, status.usb.errors, status.usb.timeouts, status.last_data_time, status.recovery_attempts, status.recovery_successes, status.recovery_reason, status.recovering);
        }),
        sdbus::registerProperty("FICStatistics").withGetter([this]() {
            const auto statistics = this->tuner_handler->get_fic_statistics();
            const std::vector<uint32_t> fig_types(statistics.fig_types.begin(), statistics.fig_types.end());
            const std::vector<uint32_t> fig0_extensions(statistics.fig0_extensions.begin(), statistics.fig0_extensions.end());
            const std::vector<uint32_t> fig5_extensions(statistics.fig5_extensions.begin(), statistics.fig5_extensions.end());
            return make_dbus_struct(statistics.fibs_received, statistics.fibs_valid, statistics.fibs_crc_errors, statistics.figs_received, fig_types, fig0_extensions, fig5_extensions);
        }).withUpdateBehavior(sdbus::Flags::EMITS_NO_SIGNAL)
#if DAB_ENABLE_SLIDESHOW
        , sdbus::registerProperty("SlideShowStatistics").withGetter([this]() {
            const auto statistics = this->tuner_handler->get_slideshow_statistics();
            return make_dbus_struct(statistics.groups_received, statistics.groups_accepted, statistics.group_crc_errors, statistics.segments, statistics.objects, statistics.header_parses, statistics.transport_id, statistics.received_segments, statistics.total_segments, statistics.total_segments_known, statistics.first_image_received, statistics.first_image_ms, statistics.cache_hit);
        })
#endif
#if DAB_ENABLE_PACKET
        , sdbus::registerProperty("DataPathStatistics").withGetter([this]() {
            const auto statistics = this->tuner_handler->get_data_path_statistics();
            return make_dbus_struct(statistics.msc_bytes, statistics.packets_valid, statistics.packet_crc_errors, statistics.packets_addressed, statistics.data_groups, statistics.packet_continuity_errors, statistics.fec_frames, statistics.fec_errors, statistics.mot_groups_received, statistics.mot_groups_accepted, statistics.mot_crc_errors, statistics.mot_segments, statistics.mot_directories, statistics.mot_objects, statistics.spi_objects, statistics.spi_updates);
        })
#endif
    ).forInterface(std::string(DIAGNOSTICS_INTERFACE));
#endif


    tuner_handler->set_property_change_callback(partial(&DBusHandler::property_change_event, this));
    command_thread = std::thread(&DBusHandler::process_commands, this);
    logger.debug(std::format("Object registered at /de/multilan/dabdaemon/tuner{}", id));
}

void DBusHandler::emit_interfaces_added_signal() {
    object->emitInterfacesAddedSignal();
}

void DBusHandler::emit_interfaces_removed_signal() {
    object->emitInterfacesRemovedSignal();
}

DBusHandler::~DBusHandler() {
    tuner_handler->set_property_change_callback(nullptr);
    tuner_handler->stop();
    std::deque<Command> pending_commands;

    {
        const std::lock_guard lock(command_mutex);
        commands_running = false;
        pending_commands.swap(commands);
    }

    command_condition.notify_all();

    for (const auto &command: pending_commands) {
        try {
            command.result.returnResults(false);
        } catch (const sdbus::Error &error) {
            logger.warning(std::format("Could not cancel pending command: {}", error.getMessage()));
        }
    }

    if (command_thread.joinable()) {
        command_thread.join();
    }

    object.reset();
    logger.debug("Object removed");
}

void DBusHandler::queue_command(sdbus::Result<bool> result, const std::function<bool()> &action) {
    {
        const std::lock_guard lock(command_mutex);

        if (commands_running && commands.size() < 16) {
            commands.push_back({std::move(result), action, std::chrono::steady_clock::now() + std::chrono::seconds(5)});
            command_condition.notify_one();
            return;
        }
    }

    logger.warning("Hardware command rejected: tuner is stopping or the command queue is full");
    result.returnResults(false);
}

void DBusHandler::process_commands() {
    while (true) {
        std::optional<Command> command;

        {
            std::unique_lock lock(command_mutex);
            command_condition.wait(lock, [this]() {
                return !commands_running || !commands.empty();
            });

            if (!commands_running) {
                return;
            }

            command.emplace(std::move(commands.front()));
            commands.pop_front();
        }

        bool success = false;

        try {
            if (std::chrono::steady_clock::now() < command->deadline) {
                success = command->action();
            } else {
                logger.warning("Hardware command expired while waiting in the queue");
            }
        } catch (const std::exception &error) {
            logger.error(std::format("Hardware command failed: {}", error.what()));
        }

        try {
            command->result.returnResults(success);
        } catch (const sdbus::Error &error) {
            logger.warning(std::format("Could not reply to hardware command: {}", error.getMessage()));
        }
    }
}

void DBusHandler::property_change_event(const std::vector<std::string> &properties) {
    // Reception and D-Bus methods may hold tuner locks while reporting changes.
    const std::lock_guard lock(pending_properties_mutex);
    pending_properties.insert(properties.begin(), properties.end());
}

void DBusHandler::emit_pending_property_changes() {
    std::set<std::string> properties;
    const auto revision = tuner_handler->get_ensembles_revision();
    if (revision != ensembles_revision) {
        ensembles_revision = revision;
        properties.emplace("Ensembles");
    }

    {
        const std::lock_guard lock(pending_properties_mutex);
        properties.insert(pending_properties.begin(), pending_properties.end());
        pending_properties.clear();
    }

    if (properties.empty()) {
        return;
    }

    std::map<std::string, std::vector<sdbus::PropertyName>> properties_by_interface;

    for (const auto &property: properties) {
        const auto interface = interface_for_property(property);

        if (interface.empty()) {
            if (!property_was_compiled_out(property)) {
                logger.warning(std::format("No D-Bus interface mapped for property {}", property));
            }
            continue;
        }

        properties_by_interface[std::string(interface)].emplace_back(property);
    }

    for (const auto &[interface, property_names]: properties_by_interface) {
        try {
            object->emitPropertiesChangedSignal(sdbus::InterfaceName{interface}, property_names);
        } catch (const sdbus::Error &error) {
            logger.warning(std::format("Could not emit PropertiesChanged on {}: {}", interface, error.getMessage()));
        }
    }
}
