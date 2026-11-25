#!/usr/bin/env python3

import asyncio
import os
import re
import shutil
import sys
from datetime import date, datetime, time as datetime_time, timedelta, timezone
from html import escape
from pathlib import Path
from time import monotonic

from dbus_next.aio import MessageBus
from dbus_next.constants import BusType

try:
    from PyQt6.QtCore import QAbstractTableModel, QBuffer, QDate, QIODevice, QProcess, QSize, Qt, QThread, QTimer, QUrl, pyqtSignal
    from PyQt6.QtGui import QFontDatabase, QImageReader, QPainter, QPalette, QPixmap
    from PyQt6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest
    from PyQt6.QtWidgets import (
        QAbstractItemView, QApplication, QCheckBox, QComboBox, QFormLayout, QGroupBox, QHBoxLayout,
        QDateEdit, QDialog, QDialogButtonBox, QHeaderView, QLabel, QLineEdit, QMainWindow, QPlainTextEdit, QPushButton, QProgressBar, QSpinBox, QSplitter, QStyle,
        QTabWidget, QTableView, QTextBrowser, QTreeWidget,
        QTreeWidgetItem, QVBoxLayout, QWidget,
    )
    QT_AVAILABLE = True
except ImportError:
    QT_AVAILABLE = False

SERVICE = "de.multilan.dabdaemon"
MANAGER_PATH = "/de/multilan/dabdaemon"
MANAGER_INTERFACE = "de.multilan.dabdaemon.Manager1"
TUNER_INTERFACE = "de.multilan.dabdaemon.Tuner1"
AUDIO_INTERFACE = "de.multilan.dabdaemon.Audio1"
METADATA_INTERFACE = "de.multilan.dabdaemon.Metadata1"
SIGNAL_INTERFACE = "de.multilan.dabdaemon.Signal1"
SLIDESHOW_INTERFACE = "de.multilan.dabdaemon.Slideshow1"
SPI_INTERFACE = "de.multilan.dabdaemon.SPI1"
JOURNALINE_INTERFACE = "de.multilan.dabdaemon.Journaline1"
TPEG_INTERFACE = "de.multilan.dabdaemon.TPEG1"
EWS_INTERFACE = "de.multilan.dabdaemon.EWS1"
FIDC_INTERFACE = "de.multilan.dabdaemon.FIDC1"
ANNOUNCEMENTS_INTERFACE = "de.multilan.dabdaemon.Announcements1"
SERVICE_LINKING_INTERFACE = "de.multilan.dabdaemon.ServiceLinking1"
FREQUENCY_INFO_INTERFACE = "de.multilan.dabdaemon.FrequencyInfo1"
SCAN_INTERFACE = "de.multilan.dabdaemon.Scan1"
DIAGNOSTICS_INTERFACE = "de.multilan.dabdaemon.Diagnostics1"
PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties"
OBJECT_MANAGER_INTERFACE = "org.freedesktop.DBus.ObjectManager"
MAX_LOGO_BYTES = 2 * 1024 * 1024
MAX_LOGO_DIMENSION = 2048
MAX_LOGO_IMAGES = 64
MAX_LOGO_REQUESTS = 8
MAX_FIC_FIGURES = 2048
MAX_JOURNALINE_HISTORY = 128
SCAN_TUNE_TIMEOUT = 5
SERVICE_TUNE_TIMEOUT = 15
AUDIO_CHANGE_ACTIONS = {
    "select_tuner", "set_frequency", "set_ensemble", "set_service",
    "close_service", "start_scan", "seek", "tune_scan_result",
}

TUNER_INTERFACES = (
    TUNER_INTERFACE,
    AUDIO_INTERFACE,
    METADATA_INTERFACE,
    SIGNAL_INTERFACE,
    SLIDESHOW_INTERFACE,
    SPI_INTERFACE,
    JOURNALINE_INTERFACE,
    TPEG_INTERFACE,
    EWS_INTERFACE,
    FIDC_INTERFACE,
    ANNOUNCEMENTS_INTERFACE,
    SERVICE_LINKING_INTERFACE,
    FREQUENCY_INFO_INTERFACE,
    SCAN_INTERFACE,
    DIAGNOSTICS_INTERFACE,
)

def demuxer_for_audio_codec(audio_codec):
    if audio_codec == "aac":
        return "loas"

    if audio_codec == "mp2":
        return "mp3"

    return ""


def mpv_arguments(audio_pipe, audio_codec):
    demuxer = demuxer_for_audio_codec(audio_codec)

    if not audio_pipe or not demuxer:
        return []

    return [
        "--no-video",
        "--really-quiet",
        "--no-terminal",
        "--cache=no",
        # Smooth the first 15 ms of decoded audio, starting with the first sample.
        "--af-add=lavfi=[afade=t=in:st=0:d=0.015:curve=hsin]",
        f"--demuxer-lavf-format={demuxer}",
        audio_pipe,
    ]


def ensemble_for_frequency(frequency, channels):
    for ensemble, value in channels:
        if value == frequency:
            return ensemble

    return "-"


def format_fic_figure(figure):
    sequence, received, fig_type, extension, flags, name, status, data, fields = figure
    return {
        "sequence": sequence, "received": received, "type": fig_type, "extension": extension,
        "flags": flags, "name": name, "status": status, "data": bytes(data), "fields": list(fields),
    }


def fic_figure_key(figure):
    subjects = tuple(sorted({name.rsplit(".", 1)[0] for name, _ in figure["fields"] if "." in name}))
    return figure["type"], figure["extension"], figure["flags"], subjects


def fic_hex_dump(data):
    lines = []
    for offset in range(0, len(data), 16):
        part = data[offset:offset + 16]
        printable = "".join(chr(value) if 32 <= value < 127 else "." for value in part)
        lines.append(f'{offset:04X}  {part.hex(" ").upper():47}  {printable}')
    return "\n".join(lines)


def format_hardware_status(status):
    if status is None:
        return None

    names = (
        "last_usb_error", "last_usb_error_name", "last_usb_error_time", "usb_errors", "usb_timeouts",
        "last_data_time", "recovery_attempts", "recovery_successes", "recovery_reason", "recovering",
    )
    return dict(zip(names, status))


def format_hardware_time(timestamp):
    if not timestamp:
        return "-"

    return datetime.fromtimestamp(timestamp / 1000).astimezone().strftime("%Y-%m-%d %H:%M:%S %Z")


def format_signal_status(signal_status):
    if len(signal_status) != 10:
        raise ValueError(f"SignalStatus has {len(signal_status)} fields, expected 10. Rebuild and restart the daemon and use the matching dabctl.py.")

    ofdm_lock, fec_lock, fec_sync, signal_quality_valid, signal_quality, antenna_level, msc_cer_valid, msc_cer, fic_crc_error_rate_valid, fic_crc_error_rate = signal_status
    return {
        "ofdm_lock": ofdm_lock,
        "fec_lock": fec_lock,
        "fec_sync": fec_sync,
        "signal_quality_valid": signal_quality_valid,
        "signal_quality": signal_quality,
        "antenna_level": antenna_level,
        "msc_cer_valid": msc_cer_valid,
        "msc_cer": msc_cer,
        "fic_crc_error_rate_valid": fic_crc_error_rate_valid,
        "fic_crc_error_rate": fic_crc_error_rate,
    }



def format_audio_statistics(statistics):
    if statistics is None:
        return None

    keys = ("sampling_rate", "channels", "sbr", "frames_received", "frames_output", "frames_discarded", "rs_corrected", "rs_uncorrectable", "crc_errors", "sync_losses", "dropouts", "rs_available", "crc_available")
    return dict(zip(keys, statistics))


def format_data_path_statistics(statistics):
    if len(statistics) != 16:
        raise ValueError(f"DataPathStatistics has {len(statistics)} fields, expected 16. Rebuild and restart the daemon and use the matching dabctl.py.")

    fields = [
        "msc_bytes",
        "packets_valid",
        "packet_crc_errors",
        "packets_addressed",
        "data_groups",
        "packet_continuity_errors",
        "fec_frames",
        "fec_errors",
        "mot_groups_received",
        "mot_groups_accepted",
        "mot_crc_errors",
        "mot_segments",
        "mot_directories",
        "mot_objects",
        "spi_objects",
        "spi_updates",
    ]
    return dict(zip(fields, statistics))


def format_slideshow_statistics(statistics):
    fields = [
        "groups_received",
        "groups_accepted",
        "group_crc_errors",
        "segments",
        "objects",
        "header_parses",
        "transport_id",
        "received_segments",
        "total_segments",
        "total_segments_known",
        "first_image_received",
        "first_image_ms",
        "cache_hit",
    ]
    return dict(zip(fields, statistics))


def format_fidc_entry(entry):
    d1, d2, component_id, extension, entry_type, data = entry
    return {
        "d1": d1,
        "d2": d2,
        "component_id": component_id,
        "extension": extension,
        "type": entry_type,
        "data": list(data),
    }


def format_emergency_warning(status):
    supported, alerts = status
    result = []

    for alert in alerts:
        other_ensemble, ensemble_id, subchannel, phase, trigger_second_present, trigger_second, status_present, last, stage, incident_id, locations = alert
        result.append({
            "other_ensemble": other_ensemble,
            "ensemble_id": ensemble_id,
            "subchannel": subchannel,
            "phase": phase,
            "trigger_second_present": trigger_second_present,
            "trigger_second": trigger_second,
            "status_present": status_present,
            "last": last,
            "stage": stage,
            "incident_id": incident_id,
            "locations": [
                {
                    "nff": location[0],
                    "zone": location[1],
                    "sub_codes": location[2],
                    "digits": list(location[3]),
                    "sub_code_mask": location[4],
                }
                for location in locations
            ],
        })

    return {
        "supported": supported,
        "alerts": result,
    }



def format_journaline_status(status):
    configured, version, extended_header_length, point_of_entry, toc_present, toc_revision, object_count, timeout_minutes = status
    return {
        "configured": configured,
        "version": version,
        "extended_header_length": extended_header_length,
        "point_of_entry": point_of_entry,
        "toc_present": toc_present,
        "toc_revision": toc_revision,
        "object_count": object_count,
        "timeout_minutes": timeout_minutes,
    }


def format_journaline_index_entry(entry):
    object_id, object_type, static_content, revision, title = entry
    return {
        "id": object_id,
        "type": object_type,
        "static": static_content,
        "revision": revision,
        "title": title,
    }


def format_journaline_object(item):
    object_id, object_type, static_content, revision, title, body, links, list_items, general_links, language, expiry_present, expiry = item
    return {
        "id": object_id,
        "type": object_type,
        "static": static_content,
        "revision": revision,
        "title": title,
        "body": body,
        "links": [{"id": link[0], "text": link[1]} for link in links],
        "items": [list(columns) for columns in list_items],
        "general_links": [{"type": link[0], "address": link[1], "label": link[2]} for link in general_links],
        "language": language,
        "expiry_present": expiry_present,
        "expiry": expiry,
    }


def format_tpeg_status(status):
    if len(status) != 9:
        raise ValueError(f"TPEGStatus has {len(status)} fields, expected 9. Rebuild and restart the daemon and use the matching dabctl.py.")

    frames_received, groups_received, group_crc_errors, transport_crc_errors, directory_crc_errors, component_crc_errors, continuity_errors, malformed_frames, service_count = status
    return {
        "frames_received": frames_received,
        "groups_received": groups_received,
        "group_crc_errors": group_crc_errors,
        "transport_crc_errors": transport_crc_errors,
        "directory_crc_errors": directory_crc_errors,
        "component_crc_errors": component_crc_errors,
        "continuity_errors": continuity_errors,
        "malformed_frames": malformed_frames,
        "service_count": service_count,
    }


def format_tpeg_service(service):
    return f"{service[0]:02X}{service[1]:02X}{service[2]:02X}"


def format_tpeg_frame(frame):
    sequence, frame_type, service, encryption, components, data = frame
    return {
        "sequence": sequence,
        "type": frame_type,
        "service": format_tpeg_service(service),
        "encryption": encryption,
        "components": [
            {"id": component[0], "header_crc": component[1], "data": list(component[2])}
            for component in components
        ],
        "data": list(data),
    }


def tpeg_application_name(application_id):
    return {0: "SNI", 5: "TEC", 7: "TFP"}.get(application_id, f"0x{application_id:04X}")


def format_tpeg_message(message):
    service, component, application, message_id, version, expiry, generation, cancelled, priority, sequence, summary, fields, data = message
    return {
        "service": format_tpeg_service(service), "component": component,
        "application": application, "id": message_id, "version": version,
        "expiry": expiry, "generation": generation, "cancelled": cancelled,
        "priority": priority, "sequence": sequence, "summary": summary,
        "fields": [list(field) for field in fields], "data": bytes(data),
    }


def tpeg_message_key(message):
    return message["service"], message["component"], message["id"]


def tpeg_message_state(message, now=None):
    if message["cancelled"]:
        return "Cancelled"
    if message["expiry"] <= (datetime.now(timezone.utc).timestamp() if now is None else now):
        return "Expired"
    return "Active"


def format_service(service):
    service_id, label, short_label, ecc, subchannel, dab_plus, bitrate, protection, language, pty, announcement_support, announcements = service[:12]
    return {
        "id": service_id,
        "label": label,
        "short_label": short_label,
        "ecc": ecc,
        "subchannel": subchannel,
        "codec": "DAB+" if dab_plus else "DAB",
        "bitrate": bitrate,
        "protection": protection,
        "language": language,
        "pty": pty,
        "announcement_support": announcement_support,
        "announcements": announcements,
        "logo_path": service[12] if len(service) > 12 else "",
    }


def service_logo_paths(services, selected_service=None):
    entries = list(services)

    if selected_service:
        entries.append(selected_service)

    return {service[0]: service[12] for service in entries if len(service) > 12 and service[12]}


def format_user_application(application):
    application_type, name, data, xpad, xpad_type, conditional_access, conditional_access_organization_present, conditional_access_organization, data_groups, data_type, parameters = application
    return {
        "type": application_type,
        "name": name,
        "data": list(data),
        "xpad": xpad,
        "xpad_type": xpad_type,
        "conditional_access": conditional_access,
        "conditional_access_organization_present": conditional_access_organization_present,
        "conditional_access_organization": conditional_access_organization,
        "data_groups": data_groups,
        "data_type": data_type,
        "parameters": list(parameters),
    }


def format_service_component(service_component):
    if len(service_component) != 27:
        raise ValueError(f"ServiceComponents has {len(service_component)} fields, expected 27. Rebuild and restart the daemon and use the matching dabctl.py.")

    service_id, data_service, component_id, label, short_label, subchannel, transport_component_id, data, component_type, conditional_access, conditional_access_organization, fec_scheme, language, part_time, change_flags, change_day, change_hour, change_minute, change_second, transfer_service_id, transfer_ensemble_id, applications, application_labels, packet_address, data_groups, conditional_access_organization_present, user_applications = service_component
    return {
        "service_id": service_id,
        "data_service": data_service,
        "id": component_id,
        "label": label,
        "short_label": short_label,
        "subchannel": subchannel,
        "component_id": transport_component_id,
        "data": data,
        "type": component_type,
        "conditional_access": conditional_access,
        "conditional_access_organization": conditional_access_organization,
        "fec_scheme": fec_scheme,
        "language": language,
        "part_time": part_time,
        "change_flags": change_flags,
        "change_day": change_day,
        "change_hour": change_hour,
        "change_minute": change_minute,
        "change_second": change_second,
        "transfer_service_id": transfer_service_id,
        "transfer_ensemble_id": transfer_ensemble_id,
        "applications": applications,
        "application_labels": application_labels,
        "packet_address": packet_address,
        "data_groups": data_groups,
        "conditional_access_organization_present": conditional_access_organization_present,
        "user_applications": [format_user_application(application) for application in user_applications],
    }


def format_spi_multimedia(multimedia):
    url, path, mime_type, multimedia_type, width, height = multimedia
    return {
        "url": url,
        "path": path,
        "mime_type": mime_type,
        "type": multimedia_type,
        "width": width,
        "height": height,
    }


def format_spi_service_information(service):
    service_id, data_service, component_id, ecc, ensemble_id, short_name, medium_name, long_name, short_description, long_description, language, genres, multimedia = service
    return {
        "service_id": service_id,
        "data_service": data_service,
        "component_id": component_id,
        "ecc": ecc,
        "ensemble_id": ensemble_id,
        "short_name": short_name,
        "medium_name": medium_name,
        "long_name": long_name,
        "short_description": short_description,
        "long_description": long_description,
        "language": language,
        "genres": list(genres),
        "multimedia": [format_spi_multimedia(item) for item in multimedia],
    }


def format_service_logo(logo):
    service_id, ecc, ensemble_id, multimedia = logo
    return {
        "service_id": service_id,
        "ecc": ecc,
        "ensemble_id": ensemble_id,
        "multimedia": [format_spi_multimedia(item) for item in multimedia],
    }


def square_logo_candidates(logos, service_id, ensemble_id, ecc):
    result = []

    for logo in logos:
        if logo["service_id"] != service_id:
            continue
        if logo["ensemble_id"] and ensemble_id and logo["ensemble_id"] != ensemble_id:
            continue
        if logo["ecc"] and ecc and logo["ecc"] != ecc:
            continue

        for item in logo["multimedia"]:
            square = item["width"] > 0 and item["width"] == item["height"]

            if item["type"] == "logo_colour_square" or (item["type"] == "logo_unrestricted" and square):
                result.append(item)

    return sorted(result, key=lambda item: item["width"] or 32, reverse=True)


def asset_logo_candidates(assets, service):
    service_id = service[0]
    names = {"".join(character for character in name.casefold() if character.isalnum()) for name in service[1:3] if name}
    result = []

    for transport_id, content_name, path, mime_type in assets:
        match = re.fullmatch(r"([0-9a-fA-F]{4})_(.+)_([0-9]{1,4})x([0-9]{1,4})\.(?:png|jpg|jpeg)", Path(content_name).name, re.IGNORECASE)

        if not match or int(match[1], 16) != service_id:
            continue

        name = "".join(character for character in match[2].casefold() if character.isalnum())
        width, height = int(match[3]), int(match[4])

        if name not in names or width != height or width <= 0 or width > MAX_LOGO_DIMENSION:
            continue

        result.append({"url": content_name, "path": path, "mime_type": mime_type, "type": "logo_unrestricted", "width": width, "height": height})

    return sorted(result, key=lambda item: item["width"], reverse=True)


def format_spi_programme(programme):
    service_id, data_service, component_id, programme_id, short_id, short_name, medium_name, long_name, short_description, long_description, language, start_time, duration, actual_time, actual_duration, genres, multimedia = programme
    return {
        "service_id": service_id,
        "data_service": data_service,
        "component_id": component_id,
        "id": programme_id,
        "short_id": short_id,
        "short_name": short_name,
        "medium_name": medium_name,
        "long_name": long_name,
        "short_description": short_description,
        "long_description": long_description,
        "language": language,
        "start_time": start_time,
        "duration": duration,
        "actual_time": actual_time,
        "actual_duration": actual_duration,
        "genres": list(genres),
        "multimedia": [format_spi_multimedia(item) for item in multimedia],
    }


def format_spi_programme_group(group):
    group_id, short_id, short_name, medium_name, long_name, short_description, long_description, genres, multimedia = group
    return {
        "id": group_id,
        "short_id": short_id,
        "short_name": short_name,
        "medium_name": medium_name,
        "long_name": long_name,
        "short_description": short_description,
        "long_description": long_description,
        "genres": list(genres),
        "multimedia": [format_spi_multimedia(item) for item in multimedia],
    }


def format_scan_result(result):
    channel, frequency, ensemble_id, ecc, label, short_label, services = result
    return {
        "channel": channel,
        "frequency": frequency,
        "ensemble_id": ensemble_id,
        "ecc": ecc,
        "label": label,
        "short_label": short_label,
        "services": [format_service(service) for service in services],
    }


def format_ensemble(ensemble):
    block, name, short_name = ensemble

    if block and name:
        return f"{block}, {name}"

    return block or name or short_name or "-"


def format_date_time(date_time):
    year, month, day, hour, minute, second, milliseconds = date_time

    if year == 0:
        return "-"

    return f"{year:04d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}.{milliseconds:03d} UTC"


def format_slideshow_item(item):
    if len(item) != 15:
        raise ValueError("Unsupported SlideShowItems D-Bus layout. Rebuild and restart the daemon.")

    return {
        "transport_id": item[0],
        "content_name": item[1],
        "path": item[2],
        "trigger_time_present": item[3],
        "trigger_now": item[4],
        "trigger_time": item[5],
        "expire_time_present": item[6],
        "expire_time": item[7],
        "category_id": item[8],
        "slide_id": item[9],
        "category_title": item[10],
        "click_through_url": item[11],
        "alternative_location_url": item[12],
        "alert": item[13],
        "active": item[14],
    }




def status_from_properties(properties):
    signal_status = properties.get("SignalStatus", [False, False, False, False, 0, 0, False, 0, False, 0])
    scan_progress = properties.get("ScanProgress", [0, 0, "", 0])
    ensemble_status = properties.get("EnsembleStatus", [0, 0, 0, 0, False, False, False, 0, 0, 0, 0, False, 0])
    fic_statistics = properties.get("FICStatistics", [0, 0, 0, 0, [], [], []])
    emergency_warning = properties.get("EmergencyWarning", [False, []])
    service = properties.get("Service", [0, "", "", 0, 0, False, 0, "", "", "", [], []])
    dlp = properties.get("DLP", ["", "", "", False])
    journaline_status = properties.get("JournalineStatus", [False, 0, 0, 0, False, 0, 0, 0])
    tpeg_status = properties.get("TPEGStatus", [0, 0, 0, 0, 0, 0, 0, 0, 0])
    data_path_statistics = properties.get("DataPathStatistics", [0] * 16)
    slideshow_statistics = properties.get("SlideShowStatistics", [0] * 13)
    services = list(properties.get("Services", []))
    services.sort(key=lambda item: item[1].strip("\u2066\u2067\u2069").casefold())

    return {
        "state": properties.get("State", "Disconnected"),
        "frequency": properties.get("Frequency", 0),
        "signal_status": format_signal_status(signal_status),
        "hardware_status": format_hardware_status(properties.get("HardwareStatus")),
        "fic_revision": properties.get("FICRevision"),
        "scan_state": properties.get("ScanState", "Idle"),
        "scan_mode": properties.get("ScanMode", ""),
        "scan_modes": list(properties.get("ScanModes", ["fast", "full"])),
        "scan_progress": {
            "current": scan_progress[0],
            "total": scan_progress[1],
            "channel": scan_progress[2],
            "frequency": scan_progress[3],
        },
        "scan_results": [format_scan_result(result) for result in properties.get("ScanResults", [])],
        "date_time": properties.get("DateTime", [0, 0, 0, 0, 0, 0, 0]),
        "ensemble": properties.get("Ensemble", ["", "", ""]),
        "ensemble_status": {
            "id": ensemble_status[0],
            "service_count": ensemble_status[1],
            "services_found": ensemble_status[2],
            "change_flags": ensemble_status[3],
            "alarm_supported": ensemble_status[4],
            "alarm_active": ensemble_status[5],
            "occurrence_change": ensemble_status[6],
            "reconfiguration_counter": ensemble_status[7],
            "cif_counter": ensemble_status[8],
            "ecc": ensemble_status[9],
            "local_time_offset": ensemble_status[10],
            "lto_unique": ensemble_status[11],
            "international_table_id": ensemble_status[12],
        },
        "fic_statistics": {
            "fibs_received": fic_statistics[0],
            "fibs_valid": fic_statistics[1],
            "fibs_crc_errors": fic_statistics[2],
            "figs_received": fic_statistics[3],
            "fig_types": list(fic_statistics[4]),
            "fig0_extensions": list(fic_statistics[5]),
            "fig5_extensions": list(fic_statistics[6]),
        },
        "fidc": [format_fidc_entry(entry) for entry in properties.get("FIDC", [])],
        "emergency_warning": format_emergency_warning(emergency_warning),
        "service": service if service[0] != 0 else None,
        "audio_pipe": properties.get("AudioPipe", ""),
        "audio_codec": properties.get("AudioCodec", ""),
        "audio_statistics": format_audio_statistics(properties.get("AudioStatistics")),
        "data_application": properties.get("DataApplication", ""),
        "data_path_statistics": format_data_path_statistics(data_path_statistics),
        "slideshow": properties.get("SlideShow", ""),
        "slideshow_items": [format_slideshow_item(item) for item in properties.get("SlideShowItems", [])],
        "slideshow_statistics": format_slideshow_statistics(slideshow_statistics),
        "dls": properties.get("DLS", ""),
        "dlp": {
            "title": dlp[0],
            "artist": dlp[1],
            "album": dlp[2],
            "item_running": dlp[3],
        },
        "services": services,
        "service_components": list(properties.get("ServiceComponents", [])),
        "service_links": list(properties.get("ServiceLinks", [])),
        "frequency_information": list(properties.get("FrequencyInformation", [])),
        "other_ensemble_services": list(properties.get("OtherEnsembleServices", [])),
        "oe_announcement_support": list(properties.get("OEAnnouncementSupport", [])),
        "oe_announcement_switches": list(properties.get("OEAnnouncementSwitches", [])),
        "fm_announcement_support": list(properties.get("FMAnnouncementSupport", [])),
        "fm_announcement_switches": list(properties.get("FMAnnouncementSwitches", [])),
        "spi_revision": properties.get("SPIRevision", 0),
        "spi_assets": list(properties.get("SPIAssets", [])),
        "service_logos": [format_service_logo(item) for item in properties.get("ServiceLogos", [])],
        "logo_paths": service_logo_paths(services, service),
        "spi_objects": list(properties.get("SPIObjects", [])),
        "journaline_revision": properties.get("JournalineRevision", 0),
        "journaline_status": format_journaline_status(journaline_status),
        "tpeg_revision": properties.get("TPEGRevision", 0),
        "tpeg_receiving": properties.get("TPEGReceiving"),
        "tpeg_status": format_tpeg_status(tpeg_status),
        "channels": list(properties.get("BandIIIChannels", [])),
    }


def update_status_from_properties(properties, status, changed_properties):
    if status is None:
        return status_from_properties(properties)

    updated = dict(status)
    changed = set(changed_properties)

    if "State" in changed:
        updated["state"] = properties.get("State", "Disconnected")
    if "Frequency" in changed:
        updated["frequency"] = properties.get("Frequency", 0)
    if "SignalStatus" in changed:
        updated["signal_status"] = format_signal_status(properties.get("SignalStatus", [False, False, False, False, 0, 0, False, 0, False, 0]))
    if "HardwareStatus" in changed:
        updated["hardware_status"] = format_hardware_status(properties.get("HardwareStatus"))
    if "FICRevision" in changed:
        updated["fic_revision"] = properties.get("FICRevision")
    if "ScanState" in changed:
        updated["scan_state"] = properties.get("ScanState", "Idle")
    if "ScanMode" in changed:
        updated["scan_mode"] = properties.get("ScanMode", "")
    if "ScanProgress" in changed:
        scan_progress = properties.get("ScanProgress", [0, 0, "", 0])
        updated["scan_progress"] = {
            "current": scan_progress[0],
            "total": scan_progress[1],
            "channel": scan_progress[2],
            "frequency": scan_progress[3],
        }
    if "ScanResults" in changed:
        updated["scan_results"] = [format_scan_result(result) for result in properties.get("ScanResults", [])]
    if "DateTime" in changed:
        updated["date_time"] = properties.get("DateTime", [0, 0, 0, 0, 0, 0, 0])
    if "Ensemble" in changed:
        updated["ensemble"] = properties.get("Ensemble", ["", "", ""])
    if "EnsembleStatus" in changed:
        ensemble_status = properties.get("EnsembleStatus", [0, 0, 0, 0, False, False, False, 0, 0, 0, 0, False, 0])
        updated["ensemble_status"] = {
            "id": ensemble_status[0],
            "service_count": ensemble_status[1],
            "services_found": ensemble_status[2],
            "change_flags": ensemble_status[3],
            "alarm_supported": ensemble_status[4],
            "alarm_active": ensemble_status[5],
            "occurrence_change": ensemble_status[6],
            "reconfiguration_counter": ensemble_status[7],
            "cif_counter": ensemble_status[8],
            "ecc": ensemble_status[9],
            "local_time_offset": ensemble_status[10],
            "lto_unique": ensemble_status[11],
            "international_table_id": ensemble_status[12],
        }
    if "FICStatistics" in changed:
        fic_statistics = properties.get("FICStatistics", [0, 0, 0, 0, [], [], []])
        updated["fic_statistics"] = {
            "fibs_received": fic_statistics[0],
            "fibs_valid": fic_statistics[1],
            "fibs_crc_errors": fic_statistics[2],
            "figs_received": fic_statistics[3],
            "fig_types": list(fic_statistics[4]),
            "fig0_extensions": list(fic_statistics[5]),
            "fig5_extensions": list(fic_statistics[6]),
        }
    if "FIDC" in changed:
        updated["fidc"] = [format_fidc_entry(entry) for entry in properties.get("FIDC", [])]
    if "EmergencyWarning" in changed:
        updated["emergency_warning"] = format_emergency_warning(properties.get("EmergencyWarning", [False, []]))
    if "Service" in changed:
        service = properties.get("Service", [0, "", "", 0, 0, False, 0, "", "", "", [], []])
        updated["service"] = service if service[0] != 0 else None
    if "AudioPipe" in changed:
        updated["audio_pipe"] = properties.get("AudioPipe", "")
    if "AudioCodec" in changed:
        updated["audio_codec"] = properties.get("AudioCodec", "")
    if "AudioStatistics" in changed:
        updated["audio_statistics"] = format_audio_statistics(properties.get("AudioStatistics"))
    if "DataApplication" in changed:
        updated["data_application"] = properties.get("DataApplication", "")
    if "DataPathStatistics" in changed:
        updated["data_path_statistics"] = format_data_path_statistics(properties.get("DataPathStatistics", [0] * 16))
    if "SlideShow" in changed:
        updated["slideshow"] = properties.get("SlideShow", "")
    if "SlideShowItems" in changed:
        updated["slideshow_items"] = [format_slideshow_item(item) for item in properties.get("SlideShowItems", [])]
    if "SlideShowStatistics" in changed:
        updated["slideshow_statistics"] = format_slideshow_statistics(properties.get("SlideShowStatistics", [0] * 13))
    if "DLS" in changed:
        updated["dls"] = properties.get("DLS", "")
    if "DLP" in changed:
        dlp = properties.get("DLP", ["", "", "", False])
        updated["dlp"] = {
            "title": dlp[0],
            "artist": dlp[1],
            "album": dlp[2],
            "item_running": dlp[3],
        }
    if "Services" in changed:
        services = list(properties.get("Services", []))
        services.sort(key=lambda item: item[1].strip("\u2066\u2067\u2069").casefold())
        updated["services"] = services
    if changed.intersection({"Services", "Service"}):
        updated["logo_paths"] = service_logo_paths(updated["services"], updated["service"])
    if "ServiceComponents" in changed:
        updated["service_components"] = list(properties.get("ServiceComponents", []))
    if "ServiceLinks" in changed:
        updated["service_links"] = list(properties.get("ServiceLinks", []))
    if "FrequencyInformation" in changed:
        updated["frequency_information"] = list(properties.get("FrequencyInformation", []))
    if "OtherEnsembleServices" in changed:
        updated["other_ensemble_services"] = list(properties.get("OtherEnsembleServices", []))
    if "OEAnnouncementSupport" in changed:
        updated["oe_announcement_support"] = list(properties.get("OEAnnouncementSupport", []))
    if "OEAnnouncementSwitches" in changed:
        updated["oe_announcement_switches"] = list(properties.get("OEAnnouncementSwitches", []))
    if "FMAnnouncementSupport" in changed:
        updated["fm_announcement_support"] = list(properties.get("FMAnnouncementSupport", []))
    if "FMAnnouncementSwitches" in changed:
        updated["fm_announcement_switches"] = list(properties.get("FMAnnouncementSwitches", []))
    if "SPIRevision" in changed:
        updated["spi_revision"] = properties.get("SPIRevision", 0)
    if "SPIAssets" in changed:
        updated["spi_assets"] = list(properties.get("SPIAssets", []))
    if "ServiceLogos" in changed:
        updated["service_logos"] = [format_service_logo(item) for item in properties.get("ServiceLogos", [])]
    if "SPIObjects" in changed:
        updated["spi_objects"] = list(properties.get("SPIObjects", []))
    if "JournalineRevision" in changed:
        updated["journaline_revision"] = properties.get("JournalineRevision", 0)
    if "JournalineStatus" in changed:
        updated["journaline_status"] = format_journaline_status(properties.get("JournalineStatus", [False, 0, 0, 0, False, 0, 0, 0]))
    if "TPEGRevision" in changed:
        updated["tpeg_revision"] = properties.get("TPEGRevision", 0)
    if "TPEGReceiving" in changed:
        updated["tpeg_receiving"] = properties.get("TPEGReceiving")
    if "TPEGStatus" in changed:
        updated["tpeg_status"] = format_tpeg_status(properties.get("TPEGStatus", [0, 0, 0, 0, 0, 0, 0, 0, 0]))
    if "BandIIIChannels" in changed:
        updated["channels"] = list(properties.get("BandIIIChannels", []))

    return updated


def display_sid(service_id, data_service=False):
    width = 8 if data_service else 4
    return f"0x{service_id:0{width}X}"


def packet_transport_openable(component, application):
    if application["xpad"] or application["conditional_access"] or component["conditional_access"] or component["subchannel"] == 0xFF or component["packet_address"] == 0xFFFF or not component["data_groups"] or component["fec_scheme"] > 1:
        return False

    if application["type"] == 0x44A:
        return len(application["parameters"]) >= 2

    if application["type"] == 0x004:
        return component["type"] == 5 and component["fec_scheme"] == 1

    return False


def service_following_details(status, service_id):
    ensemble = status["ensemble_status"]
    services = [format_service(service) for service in status["services"]]
    service = next((item for item in services if item["id"] == service_id), None)
    ecc = service["ecc"] if service and service["ecc"] else ensemble["ecc"]

    def service_name(identifier, country):
        for item in services:
            if item["id"] == identifier and (item["ecc"] or ensemble["ecc"]) == country:
                return item["label"] or item["short_label"]

        for result in status["scan_results"]:
            for item in result["services"]:
                if item["id"] == identifier and (item["ecc"] or result["ecc"]) == country:
                    return item["label"] or item["short_label"]

        return ""

    def service_ensembles(identifier, country):
        identifiers = set()

        if country == ensemble["ecc"]:
            for linked_id, data_service, conditional_access_id, ensemble_ids in status["other_ensemble_services"]:
                if linked_id == identifier and not data_service:
                    identifiers.update(ensemble_ids)

        if ensemble["id"] and any(item["id"] == identifier and (item["ecc"] or ensemble["ecc"]) == country for item in services):
            identifiers.add(ensemble["id"])

        return identifiers

    frequencies = []

    for ensemble_id in sorted(service_ensembles(service_id, ecc)):
        label = (status["ensemble"][1] or status["ensemble"][2]) if ensemble_id == ensemble["id"] else ""

        if not label:
            label = next((item["label"] or item["short_label"] for item in status["scan_results"] if item["ensemble_id"] == ensemble_id and item["ecc"] == ecc), "")

        entries = {
            (frequency, continuous, control)
            for other_ensemble, identifier, system, continuous, frequency, control in status["frequency_information"]
            if system == "DAB" and identifier == ensemble_id
        }

        if not entries and ensemble_id != ensemble["id"]:
            entries.add((0, False, 0))

        for frequency, continuous, control in sorted(entries):
            if ensemble_id == ensemble["id"] and frequency == status["frequency"]:
                continue

            frequencies.append({
                "ensemble_id": ensemble_id,
                "label": label,
                "frequency": frequency,
                "continuous": continuous,
                "adjacent": (control & 1) == 0,
            })

    # An LSN alone is not unique: hard/soft and national/international sets are separate databases.
    linkage_sets = {
        (linkage_set, hard, international)
        for linkage_set, active, hard, shared, international, system, identifier, country in status["service_links"]
        if linkage_set and system == "DAB" and identifier == service_id and (country or ensemble["ecc"]) == ecc
    }
    links = []
    seen = set()

    for linkage_set, active, hard, shared, international, system, identifier, country in status["service_links"]:
        if (linkage_set, hard, international) not in linkage_sets:
            continue

        country = (country or ensemble["ecc"]) if system in {"DAB", "FM-RDS"} else country

        if system == "DAB" and identifier == service_id and country == ecc:
            continue

        key = (linkage_set, hard, international, system, identifier, country)

        if key in seen:
            continue

        seen.add(key)
        linked_frequencies = set()
        ensemble_ids = service_ensembles(identifier, country) if system == "DAB" else set()

        if ensemble["id"] in ensemble_ids and status["frequency"]:
            linked_frequencies.add(status["frequency"])

        for other_ensemble, frequency_id, frequency_system, continuous, frequency, control in status["frequency_information"]:
            if system == "DAB" and frequency_system == "DAB" and frequency_id in ensemble_ids:
                linked_frequencies.add(frequency)
            elif system == "FM-RDS" and frequency_system == "FM-RDS" and frequency_id == identifier:
                linked_frequencies.add(frequency)
            elif system in {"DRM", "DRM/AMSS"} and frequency_system in {"DRM", "AMSS"} and (system == "DRM/AMSS" or frequency_system == system) and (control << 16 | frequency_id) == identifier:
                linked_frequencies.add(frequency)

        links.append({
            "linkage_set": linkage_set,
            "active": active,
            "hard": hard,
            "international": international,
            "type": system,
            "id": identifier,
            "ecc": country,
            "label": service_name(identifier, country) if system == "DAB" else "",
            "frequencies": sorted(linked_frequencies),
        })

    links.sort(key=lambda item: (not item["active"], not item["hard"], item["linkage_set"], item["type"], item["id"], item["ecc"]))
    return {"frequencies": frequencies, "links": links}


def parse_spi_datetime(value):
    if not value:
        return None

    try:
        return datetime.fromisoformat(value)
    except (TypeError, ValueError):
        return None


def programme_effective_start(programme):
    return programme.get("actual_time") or programme.get("start_time") or ""


def programme_effective_duration(programme):
    if programme.get("actual_time") and programme.get("actual_duration"):
        return int(programme["actual_duration"])

    return int(programme.get("duration") or 0)


def programme_title(programme):
    return programme.get("long_name") or programme.get("medium_name") or programme.get("short_name") or programme.get("id") or "-"


def programme_description(programme):
    return programme.get("long_description") or programme.get("short_description") or "-"


def format_duration(seconds):
    seconds = max(0, int(seconds or 0))

    if seconds == 0:
        return "-"

    hours, remainder = divmod(seconds, 3600)
    minutes, remaining_seconds = divmod(remainder, 60)
    parts = []

    if hours:
        parts.append(f"{hours} h")
    if minutes:
        parts.append(f"{minutes} min")
    if remaining_seconds and not hours:
        parts.append(f"{remaining_seconds} s")

    return " ".join(parts)


def epg_query_range(day):
    if isinstance(day, str):
        try:
            day = date.fromisoformat(day)
        except ValueError:
            day = datetime.now().astimezone().date()

    if not isinstance(day, date):
        day = datetime.now().astimezone().date()

    start = datetime.combine(day, datetime_time.min).astimezone()
    end = datetime.combine(day + timedelta(days=1), datetime_time.min).astimezone()
    return int(start.timestamp()), int(end.timestamp())


def epg_programme_row(programme, service_name, now_timestamp=None):
    start_value = programme_effective_start(programme)
    start = parse_spi_datetime(start_value)
    duration = programme_effective_duration(programme)
    end = start + timedelta(seconds=duration) if start is not None and duration else None

    if now_timestamp is None:
        now_timestamp = int(datetime.now().timestamp())

    state = ""
    if start is not None and duration:
        start_timestamp = int(start.timestamp())
        if start_timestamp <= now_timestamp < start_timestamp + duration:
            state = "NOW"

    return [
        state,
        start.strftime("%H:%M") if start is not None else "-",
        end.strftime("%H:%M") if end is not None else "-",
        service_name,
        programme_title(programme),
        format_duration(duration),
        ", ".join(programme.get("genres", [])) or "-",
        programme_description(programme),
    ]


def epg_programme_sort_key(programme):
    start = parse_spi_datetime(programme_effective_start(programme))
    timestamp = int(start.timestamp()) if start is not None else 0x7FFFFFFFFFFFFFFF
    return timestamp, int(programme.get("service_id") or 0), programme_title(programme).casefold()


def epg_programme_key(programme):
    return (
        int(programme.get("service_id") or 0),
        bool(programme.get("data_service")),
        int(programme.get("component_id") or 0),
        str(programme.get("id") or ""),
        int(programme.get("short_id") or 0),
        programme_effective_start(programme),
    )


def programme_detail_text(programme, service_name):
    start = programme_effective_start(programme) or "-"
    lines = [
        programme_title(programme),
        f"Service: {service_name}",
        f"Time: {start} · {format_duration(programme_effective_duration(programme))}",
    ]

    if programme.get("actual_time") and programme.get("start_time") and programme["actual_time"] != programme["start_time"]:
        lines.append(f'Scheduled: {programme["start_time"]} · {format_duration(programme.get("duration", 0))}')

    if programme.get("language"):
        lines.append(f'Language: {programme["language"]}')
    if programme.get("genres"):
        lines.append(f'Genre: {", ".join(programme["genres"])}')

    lines.append(f'Programme ID: {programme.get("id") or programme.get("short_id") or "-"}')
    description = programme_description(programme)
    if description != "-":
        lines.extend(["", description])

    media = [item.get("path") or item.get("url") for item in programme.get("multimedia", []) if item.get("path") or item.get("url")]
    if media:
        lines.extend(["", "Media:", *media])

    return "\n".join(lines)


def summarize_tpeg_errors(status):
    return status["group_crc_errors"] + status["transport_crc_errors"] + status["directory_crc_errors"] + status["component_crc_errors"] + status["continuity_errors"] + status["malformed_frames"]


if QT_AVAILABLE:
    class FastTableModel(QAbstractTableModel):
        def __init__(self, headers, parent=None):
            super().__init__(parent)
            self.headers = list(headers)
            self.rows = []
            self.user_data = []
            self.decorations = {}
            self.tooltips = {}

        def rowCount(self, parent=None):
            return 0 if parent is not None and parent.isValid() else len(self.rows)

        def columnCount(self, parent=None):
            return 0 if parent is not None and parent.isValid() else len(self.headers)

        def data(self, index, role=Qt.ItemDataRole.DisplayRole):
            if not index.isValid() or index.row() >= len(self.rows):
                return None

            if role == Qt.ItemDataRole.DisplayRole:
                row = self.rows[index.row()]
                return str(row[index.column()]) if index.column() < len(row) else ""

            if role == Qt.ItemDataRole.UserRole and index.row() < len(self.user_data):
                return self.user_data[index.row()]

            if role == Qt.ItemDataRole.DecorationRole:
                return self.decorations.get((index.row(), index.column()))

            if role == Qt.ItemDataRole.ToolTipRole:
                return self.tooltips.get((index.row(), index.column()))

            return None

        def headerData(self, section, orientation, role=Qt.ItemDataRole.DisplayRole):
            if role != Qt.ItemDataRole.DisplayRole:
                return None

            if orientation == Qt.Orientation.Horizontal and section < len(self.headers):
                return self.headers[section]

            return section + 1

        def set_rows(self, rows, user_data=None, decorations=None):
            self.beginResetModel()
            self.rows = list(rows)
            self.user_data = list(user_data) if user_data is not None else [None] * len(self.rows)
            self.decorations = dict(decorations or {})
            self.tooltips = {}
            self.endResetModel()

        def set_decorations(self, decorations):
            self.decorations = dict(decorations)

            if self.rows and self.headers:
                self.dataChanged.emit(self.index(0, 0), self.index(len(self.rows) - 1, len(self.headers) - 1), [Qt.ItemDataRole.DecorationRole])

        def row_user_data(self, row):
            if row < 0 or row >= len(self.user_data):
                return None

            return self.user_data[row]

        def set_tooltips(self, tooltips):
            self.tooltips = dict(tooltips)

            if self.rows and self.headers:
                self.dataChanged.emit(self.index(0, 0), self.index(len(self.rows) - 1, len(self.headers) - 1), [Qt.ItemDataRole.ToolTipRole])


    class DABWorker(QThread):
        status_changed = pyqtSignal(object, object)
        audio_change_started = pyqtSignal(int)
        audio_change_finished = pyqtSignal(int, bool)
        tuner_list_changed = pyqtSignal(object)
        tuner_changed = pyqtSignal(str)
        interfaces_changed = pyqtSignal(object)
        message = pyqtSignal(str)
        connected_changed = pyqtSignal(bool)
        spi_loaded = pyqtSignal(object)
        spi_group_loaded = pyqtSignal(object)
        journaline_loaded = pyqtSignal(object)
        journaline_object_loaded = pyqtSignal(object)
        tpeg_loaded = pyqtSignal(object)
        fic_loaded = pyqtSignal(object)

        def __init__(self, parent=None):
            super().__init__(parent)
            self.loop = None
            self.bus = None
            self.manager = None
            self.object_manager = None
            self.tuner = None
            self.properties = None
            self.interfaces = {}
            self.available_interfaces = set()
            self.tuner_path = ""
            self.cache = {}
            self.pending_changes = set()
            self.emit_task = None
            self.busy_actions = set()
            self.pending_actions = {}
            self.status_cache = None
            self.scan_tune_task = None
            self.scan_tune_target = None
            self.audio_change_sequence = 0

        def run(self):
            self.loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self.loop)
            self.loop.create_task(self._connect())
            self.loop.run_forever()

            pending = asyncio.all_tasks(self.loop)
            for task in pending:
                task.cancel()

            if pending:
                self.loop.run_until_complete(asyncio.gather(*pending, return_exceptions=True))

            self.loop.close()

        def invoke(self, action, *args):
            if self.loop is None or not self.loop.is_running():
                return

            change_id = None
            if action in AUDIO_CHANGE_ACTIONS:
                self.audio_change_sequence += 1
                change_id = self.audio_change_sequence
                # Emitted in the GUI thread, before the D-Bus request is queued.
                self.audio_change_started.emit(change_id)

            def track_audio_change(task):
                if change_id is not None:
                    # A done callback also runs if a scan tune is cancelled before starting.
                    task.add_done_callback(lambda _: asyncio.create_task(self._complete_audio_change(change_id)))

            def schedule():
                method = getattr(self, f"_action_{action}", None)
                if method is None:
                    self.message.emit(f"Unknown action: {action}")
                    if change_id is not None:
                        self.audio_change_finished.emit(change_id, False)
                    return

                if action == "tune_scan_result":
                    if self.scan_tune_target == args and self.scan_tune_task is not None and not self.scan_tune_task.done():
                        track_audio_change(self.scan_tune_task)
                        return
                    self._cancel_scan_tune()
                    self.scan_tune_target = args
                    self.scan_tune_task = asyncio.create_task(self._run_action(method, args))
                    track_audio_change(self.scan_tune_task)
                    return

                if action in {"set_frequency", "set_ensemble", "set_service", "set_data_service", "close_service", "start_scan", "seek", "select_tuner"}:
                    self._cancel_scan_tune()

                if action in {"load_spi", "load_spi_group", "load_journaline", "load_tpeg", "load_fic"}:
                    self._schedule_coalesced_action(action, method, args)
                    return

                task = asyncio.create_task(self._run_action(method, args))
                track_audio_change(task)

            self.loop.call_soon_threadsafe(schedule)

        async def _run_action(self, method, args):
            await self._guard(method(*args))

        async def _complete_audio_change(self, change_id):
            refreshed = False
            try:
                properties = self.properties
                if properties is None or change_id != self.audio_change_sequence:
                    return

                values = {}
                for interface in (TUNER_INTERFACE, AUDIO_INTERFACE, SCAN_INTERFACE):
                    if interface in self.available_interfaces:
                        values.update(await properties.call_get_all(interface))

                if properties is not self.properties or change_id != self.audio_change_sequence:
                    return

                self.cache.update({name: value.value for name, value in values.items()})
                refreshed = self._emit_status(set(values))
            except Exception as error:
                self.message.emit(f"Could not refresh audio status: {error}")
            finally:
                self.audio_change_finished.emit(change_id, refreshed)

        def _cancel_scan_tune(self):
            if self.scan_tune_task is not None and not self.scan_tune_task.done():
                self.scan_tune_task.cancel()
            self.scan_tune_target = None

        def _schedule_coalesced_action(self, action, method, args):
            if action in self.busy_actions:
                self.pending_actions[action] = args
                return

            self.busy_actions.add(action)
            asyncio.create_task(self._run_coalesced_action(action, method, args))

        async def _run_coalesced_action(self, action, method, args):
            try:
                await self._guard(method(*args))
            finally:
                self.busy_actions.discard(action)
                pending_args = self.pending_actions.pop(action, None)

                if pending_args is not None:
                    self._schedule_coalesced_action(action, method, pending_args)

        def stop_backend(self):
            if self.loop is None or not self.loop.is_running():
                return

            async def shutdown():
                if self.properties is not None:
                    try:
                        self.properties.off_properties_changed(self._property_change_event)
                    except Exception:
                        pass

                if self.object_manager is not None:
                    try:
                        self.object_manager.off_interfaces_added(self._interfaces_added_event)
                    except Exception:
                        pass
                    try:
                        self.object_manager.off_interfaces_removed(self._interfaces_removed_event)
                    except Exception:
                        pass

                if self.bus is not None:
                    self.bus.disconnect()

                self.loop.stop()

            self.loop.call_soon_threadsafe(lambda: asyncio.create_task(shutdown()))

        async def _guard(self, awaitable):
            try:
                await awaitable
            except asyncio.CancelledError:
                raise
            except Exception as error:
                self.message.emit(str(error))

        async def _connect(self):
            try:
                self.bus = await MessageBus(bus_type=BusType.SESSION).connect()
                introspection = await self.bus.introspect(SERVICE, MANAGER_PATH)
                proxy = self.bus.get_proxy_object(SERVICE, MANAGER_PATH, introspection)
                self.manager = proxy.get_interface(MANAGER_INTERFACE)
                self.object_manager = proxy.get_interface(OBJECT_MANAGER_INTERFACE)
                self.object_manager.on_interfaces_added(self._interfaces_added_event)
                self.object_manager.on_interfaces_removed(self._interfaces_removed_event)
                self.connected_changed.emit(True)
                await self._refresh_tuners(select_first=True)
            except Exception as error:
                self.connected_changed.emit(False)
                self.message.emit(f"Could not connect to DAB daemon: {error}")

        def _interfaces_added_event(self, object_path, interfaces):
            path = str(object_path)

            if TUNER_INTERFACE in interfaces or path == self.tuner_path:
                asyncio.create_task(self._guard(self._refresh_tuners(select_first=True)))

        def _interfaces_removed_event(self, object_path, interfaces):
            path = str(object_path)

            if TUNER_INTERFACE in interfaces and path == self.tuner_path:
                self.tuner = None

            if TUNER_INTERFACE in interfaces or path == self.tuner_path:
                asyncio.create_task(self._guard(self._refresh_tuners(select_first=True)))

        async def _refresh_tuners(self, select_first=False):
            if self.manager is None:
                await self._connect()
                return

            tuners = sorted(str(path) for path in await self.manager.get_tuners())
            self.tuner_list_changed.emit(tuners)

            if self.tuner_path and self.tuner_path in tuners and self.tuner is not None:
                return

            if tuners and select_first:
                await self._select_tuner(tuners[0])
            elif not tuners:
                await self._clear_tuner()

        async def _clear_tuner(self):
            self._cancel_scan_tune()
            if self.properties is not None:
                try:
                    self.properties.off_properties_changed(self._property_change_event)
                except Exception:
                    pass

            self.tuner = None
            self.properties = None
            self.interfaces = {}
            self.available_interfaces = set()
            self.tuner_path = ""
            self.cache = {}
            self.status_cache = None
            self.pending_changes.clear()
            self.interfaces_changed.emit([])
            self.tuner_changed.emit("")
            self.status_changed.emit(None, [])

        async def _select_tuner(self, path):
            if path == self.tuner_path and self.tuner is not None:
                return

            self._cancel_scan_tune()
            if self.properties is not None:
                try:
                    self.properties.off_properties_changed(self._property_change_event)
                except Exception:
                    pass

            introspection = await self.bus.introspect(SERVICE, path)
            proxy = self.bus.get_proxy_object(SERVICE, path, introspection)
            introspected_interfaces = {interface.name for interface in introspection.interfaces}
            self.available_interfaces = set(TUNER_INTERFACES).intersection(introspected_interfaces)
            self.interfaces = {
                name: proxy.get_interface(name)
                for name in self.available_interfaces
            }
            self.tuner = self.interfaces.get(TUNER_INTERFACE)

            if self.tuner is None:
                raise RuntimeError(f"{path} does not provide {TUNER_INTERFACE}")

            self.properties = proxy.get_interface(PROPERTIES_INTERFACE)
            self.properties.on_properties_changed(self._property_change_event)
            self.cache = {}

            for interface_name in TUNER_INTERFACES:
                if interface_name not in self.available_interfaces:
                    continue

                raw = await self.properties.call_get_all(interface_name)

                for name, value in raw.items():
                    self.cache[name] = value.value

            self.status_cache = None
            self.tuner_path = path
            self.interfaces_changed.emit(sorted(self.available_interfaces))
            self.tuner_changed.emit(path)
            self._emit_status(set(self.cache.keys()))

        def _property_change_event(self, interface_name, changed_properties, invalidated_properties):
            if interface_name not in self.available_interfaces:
                return

            changed = set()

            for name, value in changed_properties.items():
                self.cache[name] = value.value
                changed.add(name)

            if changed:
                self._queue_status_emit(changed)

            if invalidated_properties:
                asyncio.create_task(self._refresh_invalidated(interface_name, list(invalidated_properties)))

        async def _refresh_invalidated(self, interface_name, names):
            changed = set()

            for name in names:
                value = await self.properties.call_get(interface_name, name)
                self.cache[name] = value.value
                changed.add(name)

            self._queue_status_emit(changed)

        def _queue_status_emit(self, changed):
            self.pending_changes.update(changed)

            if self.emit_task is None or self.emit_task.done():
                self.emit_task = asyncio.create_task(self._coalesced_emit())

        async def _coalesced_emit(self):
            await asyncio.sleep(0.05)
            changed = set(self.pending_changes)
            self.pending_changes.clear()
            self._emit_status(changed)

        def _emit_status(self, changed):
            try:
                self.status_cache = update_status_from_properties(self.cache, self.status_cache, changed)
            except Exception as error:
                self.message.emit(f"Could not decode D-Bus status: {error}")
                return False

            self.status_changed.emit(self.status_cache, list(changed))
            return True

        def _feature_interface(self, interface_name):
            return self.interfaces.get(interface_name)

        async def _action_refresh_tuners(self):
            await self._refresh_tuners(select_first=True)

        async def _action_select_tuner(self, path):
            await self._select_tuner(path)

        async def _action_set_frequency(self, frequency):
            if self.tuner is None:
                return

            success = await self.tuner.call_set_frequency(int(frequency))
            self.message.emit("Frequency set." if success else "Could not set frequency.")

        async def _action_set_ensemble(self, ensemble):
            if self.tuner is None:
                return

            success = await self.tuner.call_set_ensemble(str(ensemble))
            self.message.emit("Ensemble set." if success else "Could not set ensemble.")

        async def _action_set_service(self, service_id):
            if self.tuner is None:
                return

            success = await self.tuner.call_set_service(int(service_id))
            self.message.emit("Service opened." if success else "Could not open service.")

        async def _action_tune_scan_result(self, frequency, service_id, tuner_path):
            tuner = self.tuner
            properties = self.properties
            if tuner is None or properties is None or tuner_path != self.tuner_path:
                return

            def current():
                return tuner is self.tuner and properties is self.properties and tuner_path == self.tuner_path

            scan = self._feature_interface(SCAN_INTERFACE)
            if scan is not None:
                try:
                    async with asyncio.timeout(SCAN_TUNE_TIMEOUT):
                        state = (await properties.call_get(SCAN_INTERFACE, "ScanState")).value
                        if not current():
                            return
                        if state == "Scanning":
                            await scan.call_stop_scan()
                        while state in {"Scanning", "Stopping"}:
                            if not current():
                                return
                            await asyncio.sleep(0.1)
                            state = (await properties.call_get(SCAN_INTERFACE, "ScanState")).value
                except TimeoutError:
                    self.message.emit("Could not stop the scan before tuning.")
                    return

            if not current():
                return
            success = await tuner.call_set_frequency(int(frequency))
            if not current():
                return
            if not success:
                self.message.emit("Could not set frequency.")
                return
            if service_id is None:
                self.message.emit("Frequency set.")
                return

            try:
                async with asyncio.timeout(SERVICE_TUNE_TIMEOUT):
                    while current():
                        # Wait for a complete live list; older daemons do not expose ServicesReady.
                        values = await properties.call_get_all(TUNER_INTERFACE)
                        if not current() or values["Frequency"].value != frequency:
                            return
                        ready = values.get("ServicesReady")
                        if (ready is None or ready.value) and any(service[0] == service_id for service in values["Services"].value):
                            success = await tuner.call_set_service(int(service_id))
                            if current():
                                self.message.emit("Service opened." if success else "Could not open service.")
                            return
                        await asyncio.sleep(0.1)
            except TimeoutError:
                self.message.emit(f"Service {display_sid(service_id)} was not found on {frequency} kHz.")

        async def _action_close_service(self):
            if self.tuner is None:
                return

            success = await self.tuner.call_close_service()
            self.message.emit("Service closed." if success else "Could not close service.")

        async def _action_start_scan(self, mode):
            scan = self._feature_interface(SCAN_INTERFACE)

            if scan is None:
                self.message.emit("Scan interface is not available.")
                return

            success = await scan.call_start_scan(str(mode))
            self.message.emit(f"{mode.capitalize()} scan started." if success else f"Could not start {mode} scan.")

        async def _action_seek(self, direction):
            scan = self._feature_interface(SCAN_INTERFACE)
            method = getattr(scan, "call_seek_up" if direction == "up" else "call_seek_down", None)

            if method is None:
                self.message.emit("Seek is not available in this daemon.")
                return

            success = await method()
            self.message.emit(f"Seek {direction} started." if success else "Could not start seek.")

        async def _action_stop_seek(self):
            scan = self._feature_interface(SCAN_INTERFACE)
            method = getattr(scan, "call_stop_seek", None)

            if method is None:
                return

            success = await method()
            self.message.emit("Seek stop requested." if success else "No active seek.")

        async def _action_stop_scan(self):
            scan = self._feature_interface(SCAN_INTERFACE)

            if scan is None:
                self.message.emit("Scan interface is not available.")
                return

            success = await scan.call_stop_scan()
            self.message.emit("Scan stop requested." if success else "No active scan.")

        async def _action_set_data_service(self, service_id, data_service, component_id, application_type):
            if self.tuner is None:
                return

            success = await self.tuner.call_set_data_service(int(service_id), bool(data_service), int(component_id), int(application_type))
            self.message.emit("Data service opened." if success else "Could not open data service.")

        async def _action_load_spi(self, detail_index=0, epg_service_id=-1, epg_date="", view_epoch=0):
            spi = self._feature_interface(SPI_INTERFACE)

            if spi is None:
                return

            context = self.tuner_path, self.cache.get("Frequency", 0)
            detail_index = int(detail_index)
            service_components = list(self.cache.get("ServiceComponents", []))
            services = list(self.cache.get("Services", []))
            revision = self.cache.get("SPIRevision", 0)
            transports = []
            transport_rows = []

            for raw_component in service_components:
                component = format_service_component(raw_component)
                applications = [application for application in component["user_applications"] if application["type"] == 0x007]

                if not applications:
                    continue

                packet = component["packet_address"] != 0xFFFF and component["subchannel"] != 0xFF and component["data_groups"] and any(not application["xpad"] for application in applications)
                transport = {
                    "service_id": component["service_id"],
                    "data_service": component["data_service"],
                    "component_id": component["id"],
                    "subchannel": component["subchannel"],
                    "packet_address": component["packet_address"],
                    "transport": "Packet" if packet else "X-PAD",
                    "application_types": [application["xpad_type"] for application in applications if application["xpad"]],
                    "openable": bool(packet),
                    "application_type": 0x007,
                }
                transports.append(transport)
                transport_rows.append([
                    display_sid(transport["service_id"], transport["data_service"]),
                    transport["component_id"],
                    transport["transport"],
                    transport["subchannel"] if transport["subchannel"] != 0xFF else "-",
                    transport["packet_address"] if transport["packet_address"] != 0xFFFF else "-",
                    ", ".join(str(value) for value in transport["application_types"]) or "-",
                ])

            rows = []
            user_data = []

            if detail_index == 0:
                queries = [spi.call_get_service_information(service[0], False, 0xFF) for service in services]
                results = await asyncio.gather(*queries) if queries else []

                for result in results:
                    for raw_item in result:
                        service = format_spi_service_information(raw_item)
                        name = service["long_name"] or service["medium_name"] or service["short_name"] or "-"
                        description = service["long_description"] or service["short_description"] or "-"
                        images = "\n".join(item["path"] or item["url"] for item in service["multimedia"])
                        rows.append([
                            display_sid(service["service_id"], service["data_service"]),
                            name,
                            description,
                            ", ".join(service["genres"]) or "-",
                            images or "-",
                        ])
            elif detail_index == 1:
                query_from, query_to = epg_query_range(epg_date)
                service_names = {int(service[0]): service[1] or service[2] or display_sid(service[0]) for service in services}

                if int(epg_service_id) >= 0:
                    service_ids = [int(epg_service_id)]
                else:
                    service_ids = sorted({int(service[0]) for service in services})

                queries = [spi.call_get_schedule(service_id, False, 0xFF, query_from, query_to) for service_id in service_ids]
                results = await asyncio.gather(*queries) if queries else []
                programmes = []

                for result in results:
                    programmes.extend(format_spi_programme(raw_item) for raw_item in result)

                programmes.sort(key=epg_programme_sort_key)
                now_timestamp = int(datetime.now().timestamp())

                for programme in programmes:
                    service_name = service_names.get(programme["service_id"], display_sid(programme["service_id"], programme["data_service"]))
                    rows.append(epg_programme_row(programme, service_name, now_timestamp))
                    user_data.append(programme)
            elif detail_index == 2:
                raw_groups = await spi.call_get_programme_groups()

                for raw_item in raw_groups:
                    group = format_spi_programme_group(raw_item)
                    name = group["long_name"] or group["medium_name"] or group["short_name"] or "-"
                    description = group["long_description"] or group["short_description"] or "-"
                    group_id = group["id"] or str(group["short_id"])
                    rows.append([group_id, name, description])
                    user_data.append(group)
            elif detail_index == 3:
                objects_variant = await self.properties.call_get(SPI_INTERFACE, "SPIObjects")
                objects = list(objects_variant.value)
                self.cache["SPIObjects"] = objects
                subtype_names = {(7, 0): "SI", (7, 1): "PI", (7, 2): "GI"}

                for item in objects:
                    type_subtype = f"{item[2]}/{item[3]}"
                    subtype_name = subtype_names.get((item[2], item[3]))

                    if subtype_name:
                        type_subtype += f" {subtype_name}"

                    compression = "-" if item[6] < 0 else str(item[6])
                    profile = ", ".join(f"0x{value:02X}" for value in item[7]) or "-"
                    rows.append([
                        f"0x{item[0]:04X}",
                        type_subtype,
                        f"{item[1]:,}",
                        item[4] or "-",
                        item[5] or "-",
                        compression,
                        profile,
                        item[8] or "-",
                    ])
            elif detail_index == 4:
                assets_variant = await self.properties.call_get(SPI_INTERFACE, "SPIAssets")
                assets = list(assets_variant.value)
                self.cache["SPIAssets"] = assets
                rows = [
                    [f"0x{item[0]:04X}", item[1] or "-", item[3] or "-", item[2]]
                    for item in assets
                ]

            if context != (self.tuner_path, self.cache.get("Frequency", 0)) or spi is not self._feature_interface(SPI_INTERFACE):
                return

            self.spi_loaded.emit({
                "context": context,
                "view_epoch": view_epoch,
                "revision": revision,
                "transports": transports,
                "transport_rows": transport_rows,
                "detail_index": detail_index,
                "detail_rows": rows,
                "detail_user_data": user_data,
                "epg_service_id": int(epg_service_id),
                "epg_date": epg_date,
            })

        async def _action_load_spi_group(self, view_epoch, request_id, group_id, short_id):
            spi = self._feature_interface(SPI_INTERFACE)

            if spi is None:
                return

            context = self.tuner_path, self.cache.get("Frequency", 0)
            supported = hasattr(spi, "call_get_group_programmes")
            raw_programmes = await spi.call_get_group_programmes(group_id, short_id) if supported else []

            if context != (self.tuner_path, self.cache.get("Frequency", 0)) or spi is not self._feature_interface(SPI_INTERFACE):
                return

            programmes = [format_spi_programme(item) for item in raw_programmes]
            programmes.sort(key=epg_programme_sort_key)
            service_names = {int(service[0]): service[1] or service[2] or display_sid(service[0]) for service in self.cache.get("Services", [])}
            rows = []
            now_timestamp = int(datetime.now().timestamp())

            for programme in programmes:
                service_name = service_names.get(programme["service_id"], display_sid(programme["service_id"], programme["data_service"]))
                row = epg_programme_row(programme, service_name, now_timestamp)
                start = parse_spi_datetime(programme_effective_start(programme))
                row.insert(1, start.strftime("%Y-%m-%d") if start else "-")
                rows.append(row)

            self.spi_group_loaded.emit({
                "context": context,
                "view_epoch": view_epoch,
                "request_id": request_id,
                "group_key": (group_id, short_id),
                "supported": supported,
                "rows": rows,
                "programmes": programmes,
            })

        def _journaline_context(self):
            service = self.cache.get("Service")
            configured = self.cache.get("JournalineStatus", [False])[0]
            return self.tuner_path, self.cache.get("Frequency", 0), service[0] if service else 0, configured

        async def _action_load_journaline(self, view_epoch):
            journaline = self._feature_interface(JOURNALINE_INTERFACE)

            if journaline is None:
                return

            service_components = list(self.cache.get("ServiceComponents", []))
            revision = self.cache.get("JournalineRevision", 0)
            status = format_journaline_status(self.cache.get("JournalineStatus", [False, 0, 0, 0, False, 0, 0, 0]))
            context = self._journaline_context()
            raw_index = await journaline.call_get_journaline_index()

            if context != self._journaline_context() or journaline is not self._feature_interface(JOURNALINE_INTERFACE):
                return

            transports = []

            for raw_component in service_components:
                component = format_service_component(raw_component)
                applications = [application for application in component["user_applications"] if application["type"] == 0x44A]

                if not applications:
                    continue

                packet = any(not application["xpad"] for application in applications)
                transports.append({
                    "service_id": component["service_id"],
                    "data_service": component["data_service"],
                    "component_id": component["id"],
                    "subchannel": component["subchannel"],
                    "packet_address": component["packet_address"],
                    "transport": "Packet" if packet else "X-PAD",
                    "application_types": [application["xpad_type"] for application in applications if application["xpad"]],
                    "openable": any(packet_transport_openable(component, application) for application in applications),
                    "application_type": 0x44A,
                })

            self.journaline_loaded.emit({
                "path": context[0],
                "view_epoch": view_epoch,
                "revision": revision,
                "status": status,
                "transports": transports,
                "index": [format_journaline_index_entry(item) for item in raw_index],
            })

        async def _action_load_journaline_object(self, object_id, view_epoch, request_id=0):
            journaline = self._feature_interface(JOURNALINE_INTERFACE)

            if journaline is None:
                return

            context = self._journaline_context()
            objects = await journaline.call_get_journaline_object(int(object_id))

            if context != self._journaline_context() or journaline is not self._feature_interface(JOURNALINE_INTERFACE):
                return

            if not objects:
                self.message.emit(f"Journaline object 0x{int(object_id):04X} is not cached.")
                self.journaline_object_loaded.emit({"id": int(object_id), "path": context[0], "view_epoch": view_epoch, "request_id": request_id, "missing": True})
                return

            item = format_journaline_object(objects[0])
            item.update({"path": context[0], "view_epoch": view_epoch, "request_id": request_id})
            self.journaline_object_loaded.emit(item)

        async def _action_load_fic(self, since_sequence, view_epoch):
            diagnostics = self._feature_interface(DIAGNOSTICS_INTERFACE)
            path = self.tuner_path

            if diagnostics is None or "FICRevision" not in self.cache:
                return

            generation, oldest, revision, figures = await diagnostics.call_get_fic_figures(int(since_sequence), 512)

            if path != self.tuner_path or diagnostics is not self._feature_interface(DIAGNOSTICS_INTERFACE):
                return

            self.fic_loaded.emit({
                "path": path, "view_epoch": view_epoch, "generation": generation,
                "oldest": oldest, "revision": revision, "figures": [format_fic_figure(figure) for figure in figures],
            })

        async def _action_stop_tpeg(self):
            tpeg = self._feature_interface(TPEG_INTERFACE)
            if tpeg is None or "TPEGReceiving" not in self.cache:
                return
            success = await tpeg.call_stop_tpeg()
            self.message.emit("TPEG stopped." if success else "Could not stop TPEG.")

        async def _action_load_tpeg(self):
            tpeg = self._feature_interface(TPEG_INTERFACE)

            if tpeg is None:
                return

            context = self.tuner_path, self.cache.get("Frequency", 0)
            service_components = list(self.cache.get("ServiceComponents", []))
            revision = self.cache.get("TPEGRevision", 0)
            status = format_tpeg_status(self.cache.get("TPEGStatus", [0, 0, 0, 0, 0, 0, 0, 0, 0]))
            raw_services, raw_frames = await asyncio.gather(tpeg.call_get_tpeg_services(), tpeg.call_get_tpeg_frames(0, 128))
            information, messages, content_status = [], [], [0, 0, 0, 0]
            if hasattr(tpeg, "call_get_tpeg_messages"):
                information, messages, content_status = await asyncio.gather(
                    tpeg.call_get_tpeg_service_information(), tpeg.call_get_tpeg_messages(), tpeg.call_get_tpeg_content_status())
            if context != (self.tuner_path, self.cache.get("Frequency", 0)) or tpeg is not self._feature_interface(TPEG_INTERFACE):
                return
            transports = []

            for raw_component in service_components:
                component = format_service_component(raw_component)
                applications = [application for application in component["user_applications"] if application["type"] == 0x004 and not application["xpad"]]

                if not any(packet_transport_openable(component, application) for application in applications):
                    continue

                transports.append({
                    "service_id": component["service_id"],
                    "data_service": component["data_service"],
                    "component_id": component["id"],
                    "subchannel": component["subchannel"],
                    "packet_address": component["packet_address"],
                    "fec_scheme": component["fec_scheme"],
                    "openable": True,
                    "application_type": 0x004,
                })

            self.tpeg_loaded.emit({
                "context": context,
                "information": information,
                "messages": [format_tpeg_message(message) for message in messages],
                "content_status": content_status,
                "revision": revision,
                "status": status,
                "transports": transports,
                "services": [format_tpeg_service(service) for service in raw_services],
                "frames": [format_tpeg_frame(frame) for frame in raw_frames],
            })


    class DABWindow(QMainWindow):
        def __init__(self):
            super().__init__()
            self.status = None
            self.tuner_path = ""
            self.service_details_dialog = None
            self.service_details_id = None
            self.service_details_context = None
            self.available_interfaces = set()
            self.slide_path = ""
            self.logo_images = {}
            self.logo_requests = {}
            self.logo_failures = {}
            self.logo_network = QNetworkAccessManager(self)
            self.audio_target = None
            self.audio_stopping = False
            self.audio_pending_changes = set()
            self.audio_latest_change = 0
            self.audio_status_current = True
            self.closing = False
            self.spi_data = None
            self.spi_detail_rows = {}
            self.spi_detail_user_data = {}
            self.spi_detail_counts = {}
            self.epg_selected_key = None
            self.spi_view_epoch = 0
            self.spi_group_request_id = 0
            self.spi_group_selected_key = None
            self.spi_group_programme_key = None
            self.journaline_view_epoch = 0
            self.journaline_selected_object = None
            self.journaline_request_id = 0
            self.journaline_entry_point = None
            self.journaline_history = []
            self.journaline_history_position = -1
            self.journaline_navigation_index = None
            self.fic_figures = []
            self.fic_previous = {}
            self.fic_generation = None
            self.fic_sequence = 0
            self.fic_latest = 0
            self.fic_view_epoch = 0
            self.fic_missed = 0
            self.fic_follow = True
            self.fic_rendering = False
            self.audio_process = QProcess(self)
            self.audio_process.started.connect(self._audio_process_started)
            self.audio_process.finished.connect(self._audio_process_finished)
            self.audio_process.errorOccurred.connect(self._audio_process_error)
            self.audio_stop_timer = QTimer(self)
            self.audio_stop_timer.setSingleShot(True)
            self.audio_stop_timer.setInterval(750)
            self.audio_stop_timer.timeout.connect(self._kill_audio_process_if_needed)
            self.worker = DABWorker(self)
            self.worker.status_changed.connect(self.on_status_changed)
            self.worker.audio_change_started.connect(self._begin_audio_change)
            self.worker.audio_change_finished.connect(self._finish_audio_change)
            self.worker.tuner_list_changed.connect(self.on_tuner_list_changed)
            self.worker.tuner_changed.connect(self.on_tuner_changed)
            self.worker.interfaces_changed.connect(self.on_interfaces_changed)
            self.worker.message.connect(self.statusBar().showMessage)
            self.worker.connected_changed.connect(self.on_connected_changed)
            self.worker.spi_loaded.connect(self.on_spi_loaded)
            self.worker.spi_group_loaded.connect(self.on_spi_group_loaded)
            self.worker.journaline_loaded.connect(self.on_journaline_loaded)
            self.worker.journaline_object_loaded.connect(self.on_journaline_object_loaded)
            self.worker.tpeg_loaded.connect(self.on_tpeg_loaded)
            self.worker.fic_loaded.connect(self.on_fic_loaded)

            self.spi_refresh_timer = QTimer(self)
            self.spi_refresh_timer.setSingleShot(True)
            self.spi_refresh_timer.setInterval(500)
            self.spi_refresh_timer.timeout.connect(self._request_spi_load)
            self.journaline_refresh_timer = QTimer(self)
            self.journaline_refresh_timer.setSingleShot(True)
            self.journaline_refresh_timer.setInterval(500)
            self.journaline_refresh_timer.timeout.connect(self._request_journaline_load)
            self.tpeg_refresh_timer = QTimer(self)
            self.tpeg_refresh_timer.setSingleShot(True)
            self.tpeg_refresh_timer.setInterval(500)
            self.tpeg_refresh_timer.timeout.connect(lambda: self.worker.invoke("load_tpeg"))
            self.tpeg_expiry_timer = QTimer(self)
            self.tpeg_expiry_timer.setInterval(1000)
            self.tpeg_expiry_timer.timeout.connect(self._refresh_tpeg_expiry)
            self.tpeg_expiry_timer.start()

            self.setWindowTitle("DAB Daemon Control")
            self.resize(1380, 900)
            self._build_ui()
            self.fic_timer = QTimer(self)
            self.fic_timer.setInterval(250)
            self.fic_timer.timeout.connect(self._request_fic_load)
            self.fic_timer.start()
            self.on_interfaces_changed([])
            self.worker.start()

        def _build_ui(self):
            central = QWidget()
            root = QVBoxLayout(central)
            root.setContentsMargins(8, 8, 8, 8)
            root.setSpacing(8)

            top = QHBoxLayout()
            top.addWidget(QLabel("Tuner"))
            self.tuner_combo = QComboBox()
            self.tuner_combo.setMinimumWidth(300)
            self.tuner_combo.activated.connect(self.select_tuner)
            top.addWidget(self.tuner_combo)
            self.refresh_tuners_button = QPushButton("Refresh")
            self.refresh_tuners_button.clicked.connect(lambda: self.worker.invoke("refresh_tuners"))
            top.addWidget(self.refresh_tuners_button)
            top.addStretch(1)
            self.connection_label = QLabel("Connecting…")
            top.addWidget(self.connection_label)
            root.addLayout(top)

            self.tabs = QTabWidget()
            self.tabs.currentChanged.connect(self.on_main_tab_changed)
            self.overview_tab = self._build_overview_tab()
            self.services_tab = self._build_services_tab()
            self.data_tab = self._build_data_tab()
            self.data_tabs.currentChanged.connect(self.on_data_tab_changed)
            self.scan_tab = self._build_scan_tab()
            self.diagnostics_tab = self._build_diagnostics_tab()
            self.fic_tab = self._build_fic_tab()
            self.tabs.addTab(self.overview_tab, "Overview")
            self.tabs.addTab(self.services_tab, "Services")
            self.tabs.addTab(self.data_tab, "Data")
            self.tabs.addTab(self.scan_tab, "Scan")
            self.tabs.addTab(self.diagnostics_tab, "Diagnostics")
            self.tabs.addTab(self.fic_tab, "FIC inspector")
            root.addWidget(self.tabs)
            self.setCentralWidget(central)
            self.statusBar().showMessage("Starting…")

        def _build_overview_tab(self):
            page = QWidget()
            layout = QVBoxLayout(page)

            tuning = QGroupBox("Tuning")
            tuning_layout = QHBoxLayout(tuning)
            self.channel_combo = QComboBox()
            self.channel_combo.setMinimumWidth(200)
            self.tune_channel_button = QPushButton("Tune channel")
            self.tune_channel_button.clicked.connect(self.tune_channel)
            self.frequency_spin = QSpinBox()
            self.frequency_spin.setRange(100000, 300000)
            self.frequency_spin.setSuffix(" kHz")
            self.frequency_spin.setSingleStep(16)
            self.tune_frequency_button = QPushButton("Tune frequency")
            self.tune_frequency_button.clicked.connect(lambda: self.worker.invoke("set_frequency", self.frequency_spin.value()))
            tuning_layout.addWidget(QLabel("Channel"))
            tuning_layout.addWidget(self.channel_combo)
            tuning_layout.addWidget(self.tune_channel_button)
            tuning_layout.addSpacing(20)
            tuning_layout.addWidget(QLabel("Frequency"))
            tuning_layout.addWidget(self.frequency_spin)
            tuning_layout.addWidget(self.tune_frequency_button)
            tuning_layout.addSpacing(20)
            self.seek_up_button = QPushButton("Seek Up")
            self.seek_down_button = QPushButton("Seek Down")
            self.seek_stop_button = QPushButton("Stop Seek")
            self.seek_state = QLabel("Idle")
            self.seek_up_button.clicked.connect(lambda: self.worker.invoke("seek", "up"))
            self.seek_down_button.clicked.connect(lambda: self.worker.invoke("seek", "down"))
            self.seek_stop_button.clicked.connect(lambda: self.worker.invoke("stop_seek"))
            tuning_layout.addWidget(self.seek_up_button)
            tuning_layout.addWidget(self.seek_down_button)
            tuning_layout.addWidget(self.seek_stop_button)
            tuning_layout.addWidget(self.seek_state)
            tuning_layout.addStretch(1)
            layout.addWidget(tuning)
            self._update_tuning(None)

            splitter = QSplitter(Qt.Orientation.Horizontal)
            left = QWidget()
            left_layout = QVBoxLayout(left)
            left_layout.setContentsMargins(0, 0, 0, 0)
            radio = QGroupBox("Info")
            radio_layout = QHBoxLayout(radio)
            radio_layout.setSpacing(20)
            self.service_logo = QLabel("No logo")
            self.service_logo.setFixedSize(112, 112)
            self.service_logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.service_logo.setAccessibleName("Station logo")
            radio_layout.addWidget(self.service_logo, 0, Qt.AlignmentFlag.AlignTop)
            radio_form = QFormLayout()
            radio_form.setVerticalSpacing(6)
            self.radio_form = radio_form
            radio_layout.addLayout(radio_form, 1)
            self.state_value = QLabel("-")
            self.ensemble_value = QLabel("-")
            self.frequency_value = QLabel("-")
            self.service_value = QLabel("-")
            self.ensemble_value.setTextFormat(Qt.TextFormat.PlainText)
            self.service_value.setTextFormat(Qt.TextFormat.PlainText)
            self.time_value = QLabel("-")
            self.logo_path_value = QLabel("-")
            self.logo_path_value.setWordWrap(True)
            self.logo_path_value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            radio_form.addRow("State", self.state_value)
            radio_form.addRow("Ensemble", self.ensemble_value)
            radio_form.addRow("Frequency", self.frequency_value)
            radio_form.addRow("Service", self.service_value)
            radio_form.addRow("DAB time", self.time_value)
            radio_form.addRow("Logo path", self.logo_path_value)
            left_layout.addWidget(radio)

            playback = QGroupBox("Audio")
            self.audio_playback_group = playback
            playback_layout = QVBoxLayout(playback)
            playback_layout.setSpacing(10)
            playback_controls = QHBoxLayout()
            self.audio_auto_play = QCheckBox("Auto play")
            self.audio_auto_play.setChecked(True)
            self.audio_auto_play.toggled.connect(self._audio_auto_play_changed)
            self.audio_play_button = QPushButton("Play")
            self.audio_play_button.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_MediaPlay))
            self.audio_play_button.clicked.connect(self.start_audio_player)
            self.audio_stop_button = QPushButton("Stop")
            self.audio_stop_button.setIcon(self.style().standardIcon(QStyle.StandardPixmap.SP_MediaStop))
            self.audio_stop_button.clicked.connect(self.stop_audio_player)
            self.audio_player_state = QLabel("Stopped")
            playback_controls.addWidget(self.audio_play_button)
            playback_controls.addWidget(self.audio_stop_button)
            playback_controls.addStretch(1)
            playback_controls.addWidget(self.audio_auto_play)
            audio_form = QFormLayout()
            self.audio_form = audio_form
            audio_form.setVerticalSpacing(6)
            self.audio_value = QLabel("-")
            self.audio_bitrate_value = QLabel("-")
            self.audio_protection_value = QLabel("-")
            self.audio_pipe_value = QLabel("-")
            self.audio_pipe_value.setWordWrap(True)
            self.audio_pipe_value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            audio_form.addRow("Playback", self.audio_player_state)
            audio_form.addRow("Format", self.audio_value)
            audio_form.addRow("Bitrate", self.audio_bitrate_value)
            audio_form.addRow("Protection", self.audio_protection_value)
            self.audio_frames_value = QLabel("-")
            self.audio_errors_value = QLabel("-")
            self.audio_dropouts_value = QLabel("-")
            for label, value in (("Frames", self.audio_frames_value), ("Errors", self.audio_errors_value), ("Dropouts", self.audio_dropouts_value)):
                value.setWordWrap(True)
                audio_form.addRow(label, value)
                audio_form.setRowVisible(value, False)
            audio_form.addRow("Pipe", self.audio_pipe_value)
            playback_layout.addLayout(audio_form)
            playback_layout.addLayout(playback_controls)
            left_layout.addWidget(playback)

            signal = QGroupBox("Signal")
            self.signal_group = signal
            signal_form = QFormLayout(signal)
            self.signal_quality = QProgressBar()
            self.signal_quality.setRange(0, 100)
            self.signal_level = QLabel("-")
            self.signal_locks = QLabel("-")
            self.signal_msc_cer = QLabel("-")
            self.signal_fic_crc = QLabel("-")
            signal_form.addRow("Quality", self.signal_quality)
            signal_form.addRow("Antenna", self.signal_level)
            signal_form.addRow("OFDM / FEC / Sync", self.signal_locks)
            signal_form.addRow("MSC CER", self.signal_msc_cer)
            signal_form.addRow("FIC CRC", self.signal_fic_crc)
            left_layout.addWidget(signal)
            left_layout.addStretch(1)

            right = QWidget()
            right_layout = QVBoxLayout(right)
            right_layout.setContentsMargins(0, 0, 0, 0)
            now = QGroupBox("Now playing")
            self.metadata_group = now
            now_layout = QVBoxLayout(now)
            self.now_playing = QLabel("-")
            self.now_playing.setWordWrap(True)
            font = self.now_playing.font()
            font.setPointSize(font.pointSize() + 3)
            font.setBold(True)
            self.now_playing.setFont(font)
            self.album_value = QLabel("")
            self.album_value.setWordWrap(True)
            self.dls_value = QLabel("")
            self.dls_value.setWordWrap(True)
            now_layout.addWidget(self.now_playing)
            now_layout.addWidget(self.album_value)
            now_layout.addWidget(self.dls_value)
            right_layout.addWidget(now)

            slide = QGroupBox("SlideShow")
            self.slideshow_group = slide
            slide_layout = QVBoxLayout(slide)
            self.slide_image = QLabel("No slide")
            self.slide_image.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.slide_image.setMinimumHeight(260)
            self.slide_image.setScaledContents(False)
            self.slide_meta = QLabel("")
            self.slide_meta.setWordWrap(True)
            slide_layout.addWidget(self.slide_image, 1)
            slide_layout.addWidget(self.slide_meta)
            right_layout.addWidget(slide, 1)

            splitter.addWidget(left)
            splitter.addWidget(right)
            splitter.setStretchFactor(0, 1)
            splitter.setStretchFactor(1, 1)
            splitter.setSizes([600, 400])
            layout.addWidget(splitter, 1)
            return page

        def _build_services_tab(self):
            page = QWidget()
            layout = QVBoxLayout(page)
            buttons = QHBoxLayout()
            self.play_service_button = QPushButton("Open selected")
            self.play_service_button.clicked.connect(self.open_selected_service)
            self.close_service_button = QPushButton("Close service")
            self.close_service_button.clicked.connect(lambda: self.worker.invoke("close_service"))
            self.service_details_button = QPushButton("Details")
            self.service_details_button.clicked.connect(self.show_service_details)
            buttons.addWidget(self.play_service_button)
            buttons.addWidget(self.close_service_button)
            buttons.addWidget(self.service_details_button)
            buttons.addStretch(1)
            layout.addLayout(buttons)

            splitter = QSplitter(Qt.Orientation.Vertical)
            self.services_table = self._table(["Logo", "Service", "SID", "Codec", "kbit/s", "SubCh", "PTY", "Language", "Announcements"])
            self.services_table.setIconSize(QSize(40, 40))
            self.services_table.setColumnWidth(0, 56)
            self.services_table.setColumnWidth(1, 200)
            self.services_table.verticalHeader().setDefaultSectionSize(48)
            self.services_table.doubleClicked.connect(self.open_selected_service)
            self.components_table = self._table(["SID", "SCIdS", "Label", "Type", "SubCh", "Packet", "FEC", "Applications"])
            splitter.addWidget(self.services_table)
            splitter.addWidget(self.components_table)
            splitter.setStretchFactor(0, 2)
            splitter.setStretchFactor(1, 1)
            layout.addWidget(splitter, 1)
            return page

        def _build_data_tab(self):
            page = QWidget()
            layout = QVBoxLayout(page)

            data_path = QGroupBox("Data path")
            self.data_path_group = data_path
            data_path_layout = QVBoxLayout(data_path)
            self.data_path_main = QLabel("No data service")
            self.data_path_detail = QLabel("")
            self.data_path_main.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self.data_path_detail.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            data_path_layout.addWidget(self.data_path_main)
            data_path_layout.addWidget(self.data_path_detail)
            layout.addWidget(data_path)

            self.data_tabs = QTabWidget()
            self.spi_tab = self._build_spi_tab()
            self.journaline_tab = self._build_journaline_tab()
            self.tpeg_tab = self._build_tpeg_tab()
            self.data_tabs.addTab(self.spi_tab, "SPI / EPG")
            self.data_tabs.addTab(self.journaline_tab, "Journaline")
            self.data_tabs.addTab(self.tpeg_tab, "TPEG")
            layout.addWidget(self.data_tabs)
            return page

        def _build_spi_tab(self):
            page = QWidget()
            layout = QVBoxLayout(page)
            header = QHBoxLayout()
            self.spi_status = QLabel("No programme information")
            self.spi_refresh_button = QPushButton("Refresh")
            self.spi_refresh_button.clicked.connect(self._request_spi_load)
            header.addWidget(self.spi_status)
            header.addStretch(1)
            header.addWidget(self.spi_refresh_button)
            layout.addLayout(header)
            self.spi_transport_table = self._table(["SID", "SCIdS", "Transport", "SubCh", "Packet", "AppTy"])
            layout.addWidget(self.spi_transport_table)

            self.spi_detail_tabs = QTabWidget()
            self.spi_services_table = self._table(["SID", "Service", "Description", "Genre", "Images"])

            self.epg_page = QWidget()
            epg_layout = QVBoxLayout(self.epg_page)
            epg_controls = QHBoxLayout()
            self.epg_service_combo = QComboBox()
            self.epg_service_combo.addItem("All services", -1)
            self.epg_previous_button = QPushButton("<")
            self.epg_previous_button.setToolTip("Previous day")
            self.epg_date_edit = QDateEdit()
            self.epg_date_edit.setCalendarPopup(True)
            self.epg_date_edit.setDisplayFormat("yyyy-MM-dd")
            self.epg_date_edit.setDate(QDate.currentDate())
            self.epg_today_button = QPushButton("Today")
            self.epg_next_button = QPushButton(">")
            self.epg_next_button.setToolTip("Next day")
            self.epg_count_label = QLabel("No EPG loaded")
            epg_controls.addWidget(QLabel("Service"))
            epg_controls.addWidget(self.epg_service_combo, 1)
            epg_controls.addSpacing(12)
            epg_controls.addWidget(self.epg_previous_button)
            epg_controls.addWidget(self.epg_date_edit)
            epg_controls.addWidget(self.epg_today_button)
            epg_controls.addWidget(self.epg_next_button)
            epg_controls.addSpacing(12)
            epg_controls.addWidget(self.epg_count_label)
            epg_layout.addLayout(epg_controls)

            epg_splitter = QSplitter(Qt.Orientation.Vertical)
            self.spi_programmes_table = self._table(["Status", "Start", "End", "Service", "Programme", "Duration", "Genre", "Description"])
            self.epg_detail = QTextBrowser()
            self.epg_detail.setPlaceholderText("Select a programme to show details.")
            epg_splitter.addWidget(self.spi_programmes_table)
            epg_splitter.addWidget(self.epg_detail)
            epg_splitter.setStretchFactor(0, 3)
            epg_splitter.setStretchFactor(1, 1)
            epg_layout.addWidget(epg_splitter, 1)

            self.epg_previous_button.clicked.connect(lambda: self.epg_date_edit.setDate(self.epg_date_edit.date().addDays(-1)))
            self.epg_today_button.clicked.connect(lambda: self.epg_date_edit.setDate(QDate.currentDate()))
            self.epg_next_button.clicked.connect(lambda: self.epg_date_edit.setDate(self.epg_date_edit.date().addDays(1)))
            self.epg_service_combo.currentIndexChanged.connect(self.on_epg_filter_changed)
            self.epg_date_edit.dateChanged.connect(self.on_epg_filter_changed)
            self.spi_programmes_table.selectionModel().currentRowChanged.connect(self.on_epg_selection_changed)

            self.spi_groups_table = self._table(["ID", "Group", "Description"])
            self.spi_groups_page = QWidget()
            groups_layout = QVBoxLayout(self.spi_groups_page)
            groups_splitter = QSplitter(Qt.Orientation.Vertical)
            self.spi_group_programmes = self._table(["Status", "Date", "Start", "End", "Service", "Programme", "Duration", "Genre", "Description"])
            self.spi_group_detail = QTextBrowser()
            self.spi_group_detail.setPlaceholderText("Select a programme to show details.")
            groups_splitter.addWidget(self.spi_groups_table)
            groups_splitter.addWidget(self.spi_group_programmes)
            groups_splitter.addWidget(self.spi_group_detail)
            groups_splitter.setStretchFactor(0, 1)
            groups_splitter.setStretchFactor(1, 2)
            groups_splitter.setStretchFactor(2, 1)
            groups_layout.addWidget(groups_splitter)
            self.spi_groups_table.selectionModel().currentRowChanged.connect(self.on_spi_group_selection_changed)
            self.spi_group_programmes.selectionModel().currentRowChanged.connect(self.on_spi_group_programme_changed)
            self.spi_objects_table = self._table(["Transport ID", "Type/Subtype", "Bytes", "Content name", "MIME", "Compression", "Profile", "Decode"])
            self.spi_assets_table = self._table(["Transport ID", "Content name", "MIME", "Path"])
            self.spi_detail_tabs.addTab(self.spi_services_table, "Services")
            self.spi_detail_tabs.addTab(self.epg_page, "EPG")
            self.spi_detail_tabs.addTab(self.spi_groups_page, "Groups")
            self.spi_detail_tabs.addTab(self.spi_objects_table, "Objects")
            self.spi_detail_tabs.addTab(self.spi_assets_table, "Assets")
            self.spi_detail_tabs.currentChanged.connect(self.on_spi_detail_tab_changed)
            layout.addWidget(self.spi_detail_tabs, 1)
            return page

        def _build_journaline_tab(self):
            page = QWidget()
            layout = QVBoxLayout(page)
            header = QHBoxLayout()
            self.journaline_back_button = QPushButton("Back")
            self.journaline_forward_button = QPushButton("Forward")
            self.journaline_home_button = QPushButton("Home")
            self.journaline_back_button.clicked.connect(lambda: self.navigate_journaline_history(-1))
            self.journaline_forward_button.clicked.connect(lambda: self.navigate_journaline_history(1))
            self.journaline_home_button.clicked.connect(self.open_journaline_home)
            for button in (self.journaline_back_button, self.journaline_forward_button, self.journaline_home_button):
                button.setEnabled(False)
                header.addWidget(button)
            self.journaline_status = QLabel("No Journaline entries")
            self.journaline_refresh_button = QPushButton("Refresh")
            self.journaline_refresh_button.clicked.connect(self._request_journaline_load)
            self.journaline_open_button = QPushButton("Receive selected")
            self.journaline_open_button.setToolTip("Stops radio playback.")
            self.journaline_open_button.hide()
            self.journaline_open_button.setEnabled(False)
            self.journaline_open_button.clicked.connect(lambda: self.open_data_transport(self.journaline_transport_table))
            header.addWidget(self.journaline_status)
            header.addStretch(1)
            header.addWidget(self.journaline_open_button)
            header.addWidget(self.journaline_refresh_button)
            layout.addLayout(header)
            self.journaline_transport_table = self._table(["SID", "SCIdS", "Transport", "SubCh", "Packet", "AppTy"])
            self.journaline_transport_table.selectionModel().currentRowChanged.connect(lambda: self._update_transport_button(self.journaline_transport_table, self.journaline_open_button))
            layout.addWidget(self.journaline_transport_table)

            splitter = QSplitter(Qt.Orientation.Horizontal)
            self.journaline_objects_table = self._table(["ID", "Type", "Rev", "Static", "Title"])
            self.journaline_objects_table.doubleClicked.connect(self.load_selected_journaline_object)
            self.journaline_detail = QTextBrowser()
            self.journaline_detail.setOpenLinks(False)
            self.journaline_detail.setOpenExternalLinks(False)
            self.journaline_detail.anchorClicked.connect(self.open_journaline_link)
            splitter.addWidget(self.journaline_objects_table)
            splitter.addWidget(self.journaline_detail)
            splitter.setStretchFactor(0, 1)
            splitter.setStretchFactor(1, 2)
            layout.addWidget(splitter, 1)
            return page

        def _build_tpeg_tab(self):
            page = QWidget()
            layout = QVBoxLayout(page)
            header = QHBoxLayout()
            self.tpeg_status = QLabel("No TPEG frames")
            self.tpeg_services = QLabel("")
            self.tpeg_refresh_button = QPushButton("Refresh")
            self.tpeg_refresh_button.clicked.connect(lambda: self.worker.invoke("load_tpeg"))
            self.tpeg_open_button = QPushButton("Receive selected")
            self.tpeg_open_button.setToolTip("Pauses SPI reception.")
            self.tpeg_open_button.hide()
            self.tpeg_open_button.setEnabled(False)
            self.tpeg_open_button.clicked.connect(lambda: self.open_data_transport(self.tpeg_transport_table))
            self.tpeg_stop_button = QPushButton("Stop")
            self.tpeg_stop_button.setEnabled(False)
            self.tpeg_stop_button.hide()
            self.tpeg_stop_button.clicked.connect(lambda: self.worker.invoke("stop_tpeg"))
            header.addWidget(self.tpeg_status)
            header.addWidget(self.tpeg_services, 1)
            header.addWidget(self.tpeg_open_button)
            header.addWidget(self.tpeg_stop_button)
            header.addWidget(self.tpeg_refresh_button)
            layout.addLayout(header)
            self.tpeg_transport_table = self._table(["SID", "SCIdS", "SubCh", "Packet", "FEC"])
            self.tpeg_transport_table.selectionModel().currentRowChanged.connect(lambda: self._update_transport_button(self.tpeg_transport_table, self.tpeg_open_button))
            self.tpeg_transport_table.setMaximumHeight(100)
            self.tpeg_transport_table.hide()
            layout.addWidget(self.tpeg_transport_table)
            self.tpeg_content_tabs = QTabWidget()
            messages_page = QWidget()
            messages_layout = QVBoxLayout(messages_page)
            messages_layout.setContentsMargins(0, 0, 0, 0)
            filters = QHBoxLayout()
            self.tpeg_application_filter = QComboBox()
            for label, value in (("All applications", 0), ("TEC", 5), ("TFP", 7)):
                self.tpeg_application_filter.addItem(label, value)
            self.tpeg_search = QLineEdit()
            self.tpeg_search.setPlaceholderText("Search messages and fields")
            self.tpeg_application_filter.currentIndexChanged.connect(self._render_tpeg_messages)
            self.tpeg_search.textChanged.connect(self._render_tpeg_messages)
            filters.addWidget(self.tpeg_application_filter)
            filters.addWidget(self.tpeg_search, 1)
            messages_layout.addLayout(filters)
            self.tpeg_messages = []
            self.tpeg_expiry_states = []
            self.tpeg_rendering = False
            self.tpeg_displayed_message = None
            self.tpeg_messages_table = self._table(["Application", "Service", "SCID", "Message", "Version", "State", "Summary", "Expires"])
            message_header = self.tpeg_messages_table.horizontalHeader()
            message_header.setStretchLastSection(False)
            for column in (0, 1, 2, 3, 4, 5, 7):
                message_header.setSectionResizeMode(column, QHeaderView.ResizeMode.ResizeToContents)
            message_header.setSectionResizeMode(6, QHeaderView.ResizeMode.Stretch)
            self.tpeg_messages_table.selectionModel().currentRowChanged.connect(self._show_tpeg_message)
            details = QTabWidget()
            self.tpeg_fields_table = self._table(["Component", "Field", "Value"])
            for column in (0, 1):
                self.tpeg_fields_table.horizontalHeader().setSectionResizeMode(column, QHeaderView.ResizeMode.ResizeToContents)
            self.tpeg_message_raw = QPlainTextEdit()
            self.tpeg_message_raw.setReadOnly(True)
            self.tpeg_message_raw.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
            details.addTab(self.tpeg_fields_table, "Fields")
            details.addTab(self.tpeg_message_raw, "Raw message")
            splitter = QSplitter(Qt.Orientation.Vertical)
            splitter.addWidget(self.tpeg_messages_table)
            splitter.addWidget(details)
            splitter.setSizes([400, 250])
            messages_layout.addWidget(splitter, 1)
            self.tpeg_content_tabs.addTab(messages_page, "Messages")
            self.tpeg_information_table = self._table(["Service", "Name", "SCID", "COID", "Application", "Version", "Messages", "Encryption", "Description"])
            for column in range(8):
                self.tpeg_information_table.horizontalHeader().setSectionResizeMode(column, QHeaderView.ResizeMode.ResizeToContents)
            self.tpeg_content_tabs.addTab(self.tpeg_information_table, "Services")
            self.tpeg_frames_table = self._table(["Seq", "Type", "Service", "Enc", "Components", "Bytes"])
            self.tpeg_frames_table.selectionModel().currentRowChanged.connect(self._show_tpeg_frame)
            for column in range(5):
                self.tpeg_frames_table.horizontalHeader().setSectionResizeMode(column, QHeaderView.ResizeMode.ResizeToContents)
            self.tpeg_frame_raw = QPlainTextEdit()
            self.tpeg_frame_raw.setReadOnly(True)
            self.tpeg_frame_raw.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
            frames = QSplitter(Qt.Orientation.Vertical)
            frames.addWidget(self.tpeg_frames_table)
            frames.addWidget(self.tpeg_frame_raw)
            frames.setSizes([400, 250])
            self.tpeg_content_tabs.addTab(frames, "Frames")
            layout.addWidget(self.tpeg_content_tabs, 1)
            return page

        def _build_scan_tab(self):
            page = QWidget()
            layout = QVBoxLayout(page)
            controls = QHBoxLayout()
            self.scan_fast_button = QPushButton("Fast scan")
            self.scan_full_button = QPushButton("Full scan")
            self.scan_stop_button = QPushButton("Stop")
            self.scan_fast_button.clicked.connect(lambda: self.worker.invoke("start_scan", "fast"))
            self.scan_full_button.clicked.connect(lambda: self.worker.invoke("start_scan", "full"))
            self.scan_stop_button.clicked.connect(lambda: self.worker.invoke("stop_scan"))
            self.scan_stop_button.setEnabled(False)
            self.scan_state = QLabel("Idle")
            controls.addWidget(self.scan_fast_button)
            controls.addWidget(self.scan_full_button)
            controls.addWidget(self.scan_stop_button)
            controls.addSpacing(20)
            controls.addWidget(self.scan_state)
            controls.addStretch(1)
            layout.addLayout(controls)
            self.scan_progress = QProgressBar()
            self.scan_progress.setRange(0, 1)
            self.scan_progress.setValue(0)
            self.scan_progress.setFormat("")
            layout.addWidget(self.scan_progress)
            self.scan_results = QTreeWidget()
            self.scan_results.setHeaderLabels(["Channel / Service", "Frequency", "ID", "Codec", "kbit/s", "PTY"])
            self.scan_results.setAlternatingRowColors(True)
            self.scan_results.itemClicked.connect(self.tune_scan_result)
            layout.addWidget(self.scan_results, 1)
            return page

        def _build_fic_tab(self):
            page = QWidget()
            layout = QVBoxLayout(page)
            controls = QHBoxLayout()
            self.fic_type_filter = QComboBox()
            self.fic_type_filter.addItem("All", -1)
            for fig_type in range(8):
                self.fic_type_filter.addItem(str(fig_type), fig_type)
            self.fic_type_filter.addItem("FIB", 255)
            self.fic_extension_filter = QComboBox()
            self.fic_extension_filter.addItem("All", -1)
            for extension in range(32):
                self.fic_extension_filter.addItem(str(extension), extension)
            self.fic_search = QLineEdit()
            self.fic_search.setPlaceholderText("Search")
            self.fic_pause_button = QPushButton("Pause")
            self.fic_pause_button.setCheckable(True)
            self.fic_pause_button.toggled.connect(self._fic_pause_changed)
            clear_button = QPushButton("Clear")
            clear_button.clicked.connect(self.clear_fic_view)
            self.fic_count = QLabel("0 entries")
            controls.addWidget(QLabel("Type"))
            controls.addWidget(self.fic_type_filter)
            controls.addWidget(QLabel("Extension"))
            controls.addWidget(self.fic_extension_filter)
            controls.addWidget(self.fic_search, 1)
            controls.addWidget(self.fic_count)
            controls.addWidget(self.fic_pause_button)
            controls.addWidget(clear_button)
            layout.addLayout(controls)

            splitter = QSplitter(Qt.Orientation.Vertical)
            self.fic_history_table = self._table(["Seq", "Time", "FIG", "Content", "Bytes", "Status"])
            self.fic_history_table.setColumnWidth(1, 125)
            self.fic_history_table.setColumnWidth(3, 260)
            self.fic_history_table.selectionModel().currentRowChanged.connect(self._fic_selection_changed)
            details = QWidget()
            detail_layout = QVBoxLayout(details)
            detail_layout.setContentsMargins(0, 0, 0, 0)
            self.fic_comparison = QLabel("")
            detail_layout.addWidget(self.fic_comparison)
            detail_splitter = QSplitter(Qt.Orientation.Horizontal)
            self.fic_fields_table = self._table(["Field", "Previous", "Value", "Change"])
            self.fic_fields_table.setColumnWidth(0, 230)
            self.fic_fields_table.setColumnWidth(1, 160)
            self.fic_fields_table.horizontalHeader().setStretchLastSection(False)
            self.fic_fields_table.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
            self.fic_raw = QPlainTextEdit()
            self.fic_raw.setReadOnly(True)
            self.fic_raw.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
            detail_splitter.addWidget(self.fic_fields_table)
            detail_splitter.addWidget(self.fic_raw)
            detail_splitter.setSizes([650, 550])
            detail_layout.addWidget(detail_splitter, 1)
            splitter.addWidget(self.fic_history_table)
            splitter.addWidget(details)
            splitter.setSizes([400, 400])
            layout.addWidget(splitter, 1)
            self.fic_type_filter.currentIndexChanged.connect(self._render_fic_figures)
            self.fic_extension_filter.currentIndexChanged.connect(self._render_fic_figures)
            self.fic_search.textChanged.connect(self._render_fic_figures)
            return page

        def _build_diagnostics_tab(self):
            page = QWidget()
            layout = QVBoxLayout(page)
            self.diagnostics_hardware_box = QGroupBox("Hardware")
            hardware_form = QFormLayout(self.diagnostics_hardware_box)
            hardware_form.setVerticalSpacing(6)
            self.diag_usb_error = QLabel("-")
            self.diag_usb_error_time = QLabel("-")
            self.diag_usb_errors = QLabel("-")
            self.diag_last_data = QLabel("-")
            self.diag_recovery = QLabel("-")
            self.diag_recovery_reason = QLabel("-")
            self.diag_recovery_reason.setWordWrap(True)
            hardware_form.addRow("Last USB error", self.diag_usb_error)
            hardware_form.addRow("Error time", self.diag_usb_error_time)
            hardware_form.addRow("Errors / timeouts", self.diag_usb_errors)
            hardware_form.addRow("Last FIC / MSC data", self.diag_last_data)
            hardware_form.addRow("Recovery", self.diag_recovery)
            hardware_form.addRow("Last recovery reason", self.diag_recovery_reason)
            layout.addWidget(self.diagnostics_hardware_box)
            top = QSplitter(Qt.Orientation.Horizontal)

            ensemble_box = QGroupBox("Ensemble")
            self.diagnostics_ensemble_box = ensemble_box
            ensemble_form = QFormLayout(ensemble_box)
            self.diag_ensemble_id = QLabel("-")
            self.diag_ecc = QLabel("-")
            self.diag_services = QLabel("-")
            self.diag_alarm = QLabel("-")
            self.diag_lto = QLabel("-")
            self.diag_reconfig = QLabel("-")
            self.diag_cif = QLabel("-")
            ensemble_form.addRow("ID", self.diag_ensemble_id)
            ensemble_form.addRow("ECC", self.diag_ecc)
            ensemble_form.addRow("Services", self.diag_services)
            ensemble_form.addRow("Alarm", self.diag_alarm)
            ensemble_form.addRow("LTO", self.diag_lto)
            ensemble_form.addRow("Reconfiguration", self.diag_reconfig)
            ensemble_form.addRow("CIF", self.diag_cif)

            fic_box = QGroupBox("FIC")
            self.diagnostics_fic_box = fic_box
            fic_form = QFormLayout(fic_box)
            self.diag_fib = QLabel("-")
            self.diag_figs = QLabel("-")
            self.diag_fig0 = QLabel("-")
            self.diag_fig5 = QLabel("-")
            self.diag_following = QLabel("-")
            fic_form.addRow("FIB", self.diag_fib)
            fic_form.addRow("FIGs", self.diag_figs)
            fic_form.addRow("FIG 0 extensions", self.diag_fig0)
            fic_form.addRow("FIG 5 / FIDC", self.diag_fig5)
            fic_form.addRow("Following / announcements", self.diag_following)

            top.addWidget(ensemble_box)
            top.addWidget(fic_box)
            layout.addWidget(top)

            self.diagnostics_slideshow_box = QGroupBox("SlideShow")
            slideshow_form = QFormLayout(self.diagnostics_slideshow_box)
            self.diag_slideshow_groups = QLabel("-")
            self.diag_slideshow_segments = QLabel("-")
            self.diag_slideshow_objects = QLabel("-")
            self.diag_slideshow_first_image = QLabel("-")
            slideshow_form.addRow("Groups", self.diag_slideshow_groups)
            slideshow_form.addRow("Image segments", self.diag_slideshow_segments)
            slideshow_form.addRow("Objects / headers", self.diag_slideshow_objects)
            slideshow_form.addRow("First image", self.diag_slideshow_first_image)
            layout.addWidget(self.diagnostics_slideshow_box)

            tables = QSplitter(Qt.Orientation.Vertical)
            self.diagnostics_ews_box = QGroupBox("Emergency warnings (EWS)")
            ews_layout = QVBoxLayout(self.diagnostics_ews_box)
            self.ews_table = self._table(["Source", "Phase", "Stage", "Incident", "Locations"])
            ews_layout.addWidget(self.ews_table)
            self.diagnostics_fidc_box = QGroupBox("Fast Information Data Channel (FIDC)")
            fidc_layout = QVBoxLayout(self.diagnostics_fidc_box)
            self.fidc_table = self._table(["Ext", "TCId", "Type", "D1/D2", "Data"])
            fidc_layout.addWidget(self.fidc_table)
            tables.addWidget(self.diagnostics_ews_box)
            tables.addWidget(self.diagnostics_fidc_box)
            layout.addWidget(tables, 1)
            return page

        def _table(self, headers):
            table = QTableView()
            table.setModel(FastTableModel(headers, table))
            table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
            table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
            table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
            table.setAlternatingRowColors(True)
            table.setWordWrap(False)
            table.verticalHeader().setVisible(False)
            table.verticalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
            table.verticalHeader().setDefaultSectionSize(24)
            table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
            table.horizontalHeader().setStretchLastSection(True)
            return table

        def _set_table_rows(self, table, rows, user_data=None, decorations=None):
            model = table.model()

            if isinstance(model, FastTableModel):
                model.set_rows(rows, user_data, decorations)

        def _selected_table_data(self, table):
            index = table.currentIndex()

            if not index.isValid():
                return None

            model = table.model()
            if not isinstance(model, FastTableModel):
                return None

            return model.row_user_data(index.row())

        def on_connected_changed(self, connected):
            self.connection_label.setText("Daemon connected" if connected else "Daemon disconnected")

        def _set_tab_visible(self, tab_widget, page, visible):
            index = tab_widget.indexOf(page)

            if index >= 0:
                tab_widget.setTabVisible(index, bool(visible))

        def on_interfaces_changed(self, interfaces):
            previous = self.available_interfaces
            self.available_interfaces = set(interfaces)
            self._update_tuning(self.status)

            if SPI_INTERFACE in previous and SPI_INTERFACE not in self.available_interfaces:
                self._reset_spi_view()

            self._set_tab_visible(self.tabs, self.fic_tab, DIAGNOSTICS_INTERFACE in self.available_interfaces and self.status is not None and self.status.get("fic_revision") is not None)
            self.audio_playback_group.setVisible(AUDIO_INTERFACE in self.available_interfaces)
            self.signal_group.setVisible(SIGNAL_INTERFACE in self.available_interfaces)
            self.metadata_group.setVisible(METADATA_INTERFACE in self.available_interfaces)
            self.slideshow_group.setVisible(SLIDESHOW_INTERFACE in self.available_interfaces)
            self.service_logo.setVisible(SPI_INTERFACE in self.available_interfaces)
            self.radio_form.setRowVisible(self.logo_path_value, SPI_INTERFACE in self.available_interfaces)
            self.services_table.setColumnHidden(0, SPI_INTERFACE not in self.available_interfaces)
            self.services_table.verticalHeader().setDefaultSectionSize(48 if SPI_INTERFACE in self.available_interfaces else 24)
            self.data_path_group.setVisible(DIAGNOSTICS_INTERFACE in self.available_interfaces)

            self._set_tab_visible(self.data_tabs, self.spi_tab, SPI_INTERFACE in self.available_interfaces)
            self._set_tab_visible(self.data_tabs, self.journaline_tab, JOURNALINE_INTERFACE in self.available_interfaces)
            self._set_tab_visible(self.data_tabs, self.tpeg_tab, TPEG_INTERFACE in self.available_interfaces)
            self._set_tab_visible(self.tabs, self.data_tab, bool(self.available_interfaces.intersection({SPI_INTERFACE, JOURNALINE_INTERFACE, TPEG_INTERFACE})))
            self._set_tab_visible(self.tabs, self.scan_tab, SCAN_INTERFACE in self.available_interfaces and (self.status is None or bool(set(self.status["scan_modes"]).intersection({"fast", "full"}))))

            diagnostic_interfaces = {
                DIAGNOSTICS_INTERFACE,
                EWS_INTERFACE,
                FIDC_INTERFACE,
                ANNOUNCEMENTS_INTERFACE,
                SERVICE_LINKING_INTERFACE,
                FREQUENCY_INFO_INTERFACE,
            }
            self._set_tab_visible(self.tabs, self.diagnostics_tab, bool(self.available_interfaces.intersection(diagnostic_interfaces)))
            self.diagnostics_fic_box.setVisible(DIAGNOSTICS_INTERFACE in self.available_interfaces)
            self.diagnostics_hardware_box.setVisible(DIAGNOSTICS_INTERFACE in self.available_interfaces)
            self.diagnostics_slideshow_box.setVisible(DIAGNOSTICS_INTERFACE in self.available_interfaces and SLIDESHOW_INTERFACE in self.available_interfaces)
            self.diagnostics_ews_box.setVisible(EWS_INTERFACE in self.available_interfaces)
            self.diagnostics_fidc_box.setVisible(FIDC_INTERFACE in self.available_interfaces)
            self._update_service_details()

        def on_tuner_list_changed(self, tuners):
            current = self.tuner_path
            self.tuner_combo.blockSignals(True)
            self.tuner_combo.clear()

            for path in tuners:
                self.tuner_combo.addItem(path.rsplit("/", 1)[-1], path)

            if current:
                index = self.tuner_combo.findData(current)
                if index >= 0:
                    self.tuner_combo.setCurrentIndex(index)

            self.tuner_combo.blockSignals(False)

        def on_tuner_changed(self, path):
            if path != self.tuner_path:
                self._reset_fic_view()
                self._reset_journaline_view()
                self._reset_spi_view()
                self._reset_tpeg_view()
                self.status = None
                self._update_service_details()
                self._stop_audio_process()

            self.tuner_path = path
            index = self.tuner_combo.findData(path)
            if index >= 0:
                self.tuner_combo.blockSignals(True)
                self.tuner_combo.setCurrentIndex(index)
                self.tuner_combo.blockSignals(False)

        def select_tuner(self, index):
            path = self.tuner_combo.itemData(index)
            if path:
                self.worker.invoke("select_tuner", path)

        def tune_channel(self):
            ensemble = self.channel_combo.currentData()
            if ensemble:
                self.worker.invoke("set_ensemble", ensemble[0])

        def _update_tuning(self, status):
            modes = set(status["scan_modes"]) if status is not None else set()
            available = status is not None and SCAN_INTERFACE in self.available_interfaces
            active = status is not None and status["scan_state"] in {"Scanning", "Stopping"}
            seeking = status is not None and status["scan_mode"] in {"seek_up", "seek_down"}
            self.seek_up_button.setEnabled(available and "seek_up" in modes and not active)
            self.seek_down_button.setEnabled(available and "seek_down" in modes and not active)
            self.seek_stop_button.setEnabled(available and seeking and status["scan_state"] == "Scanning")
            self.tune_channel_button.setEnabled(status is not None and not active)
            self.tune_frequency_button.setEnabled(status is not None and not active)
            label = status["scan_state"] if seeking else "Scanning" if active else "Idle"

            if seeking and active and status["scan_progress"]["channel"]:
                label += " · " + status["scan_progress"]["channel"]

            self.seek_state.setText(label if status is not None else "No tuner")

            if hasattr(self, "scan_tab"):
                self._set_tab_visible(self.tabs, self.scan_tab, available and bool(modes.intersection({"fast", "full"})))
                self.scan_fast_button.setEnabled(available and "fast" in modes and not active)
                self.scan_full_button.setEnabled(available and "full" in modes and not active)

        def on_status_changed(self, status, changed_properties):
            previous = self.status
            self.status = status
            changed = set(changed_properties)
            self._update_tuning(status)
            self._set_tab_visible(self.tabs, self.fic_tab, DIAGNOSTICS_INTERFACE in self.available_interfaces and status is not None and status.get("fic_revision") is not None)

            if status is None or "Frequency" in changed:
                self._reset_fic_view()

            if status is None or previous is None or status["frequency"] != previous["frequency"]:
                self._reset_spi_view()
                self._reset_tpeg_view()

            journaline_changed = status is None or previous is None or (
                status["frequency"], status["service"][0] if status["service"] else 0
            ) != (
                previous["frequency"], previous["service"][0] if previous["service"] else 0
            ) or (
                previous["journaline_status"]["configured"] and not status["journaline_status"]["configured"]
            )
            if journaline_changed:
                self._reset_journaline_view()

            if status is None:
                self.state_value.setText("No tuner")
                self.tpeg_stop_button.setEnabled(False)
                for value in (self.audio_value, self.audio_bitrate_value, self.audio_protection_value, self.audio_pipe_value):
                    value.setText("-")
                self._update_audio_statistics(None, "")
                self.service_logo.clear()
                self.service_logo.setText("No logo")
                self.services_table.model().set_decorations({})
                self._update_service_details()
                self._stop_audio_process()
                return

            if not changed or changed.intersection({"AudioPipe", "AudioCodec", "Service", "Frequency", "ScanState"}):
                self.audio_status_current = True
                self._sync_audio_player()

            if not changed or "Services" in changed:
                self._update_epg_service_filter(status["services"])

            current_tab = self.tabs.currentWidget()

            if not changed or changed.intersection({"ServiceLogos", "SPIAssets", "Service", "Services", "Frequency", "EnsembleStatus"}):
                self._refresh_service_logos()

            following_properties = {"Services", "ServiceLinks", "FrequencyInformation", "OtherEnsembleServices", "Ensemble", "EnsembleStatus", "Frequency", "BandIIIChannels", "ScanResults"}
            if not changed or changed.intersection(following_properties):
                self._update_service_details()

            if current_tab is self.overview_tab:
                if "BandIIIChannels" in changed or not self.channel_combo.count():
                    self._update_channels(status["channels"])

                overview_properties = {"State", "Frequency", "DateTime", "Ensemble", "Service", "AudioPipe", "AudioCodec", "DLS", "DLP", "SlideShow", "SlideShowItems"}
                if not changed or changed.intersection(overview_properties):
                    self._update_overview(status)

                if not changed or "SignalStatus" in changed:
                    self._update_signal(status["signal_status"])

            if current_tab is self.services_tab:
                if not changed or changed.intersection({"Services", "Service"}):
                    self._update_services(status)

                if not changed or "ServiceComponents" in changed:
                    self._update_components(status["service_components"])

            if current_tab is self.data_tab and (not changed or changed.intersection({"DataApplication", "DataPathStatistics"})):
                self._update_data_path(status)

            if current_tab is self.scan_tab and (not changed or changed.intersection({"ScanState", "ScanMode", "ScanProgress", "ScanResults"})):
                self._update_scan(status, not changed or "ScanResults" in changed)

            diagnostic_properties = {"HardwareStatus", "EnsembleStatus", "FICStatistics", "SlideShowStatistics", "FIDC", "EmergencyWarning", "ServiceLinks", "FrequencyInformation", "OtherEnsembleServices", "OEAnnouncementSupport", "OEAnnouncementSwitches", "FMAnnouncementSupport", "FMAnnouncementSwitches"}
            if current_tab is self.diagnostics_tab and (not changed or changed.intersection(diagnostic_properties)):
                self._update_diagnostics(status)

            if "SPIRevision" in changed and self._data_tab_active(0):
                self.spi_refresh_timer.start()
            if (journaline_changed or "JournalineRevision" in changed) and self._data_tab_active(1):
                self.journaline_refresh_timer.start()
            if changed.intersection({"TPEGRevision", "TPEGStatus"}) and self._data_tab_active(2):
                self.tpeg_refresh_timer.start()
            self.tpeg_stop_button.setVisible(status["tpeg_receiving"] is not None)
            self.tpeg_stop_button.setEnabled(bool(status["tpeg_receiving"]) and status["scan_state"] not in {"Scanning", "Stopping"})

        def _audio_target_from_status(self):
            if not self.status or self.audio_pending_changes or not self.audio_status_current:
                return None

            if self.status.get("scan_state") in {"Scanning", "Stopping"}:
                return None

            audio_pipe = self.status.get("audio_pipe", "")
            audio_codec = self.status.get("audio_codec", "")

            if not mpv_arguments(audio_pipe, audio_codec):
                return None

            service = self.status.get("service")
            return audio_pipe, audio_codec, self.status.get("frequency", 0), service[0] if service else 0

        def _begin_audio_change(self, change_id):
            self.audio_pending_changes.add(change_id)
            self.audio_latest_change = change_id
            self.audio_status_current = False
            self._stop_audio_process(immediate=True)

        def _finish_audio_change(self, change_id, refreshed):
            self.audio_pending_changes.discard(change_id)
            if change_id == self.audio_latest_change:
                self.audio_status_current = refreshed
            self._sync_audio_player()

        def _audio_auto_play_changed(self, enabled):
            if enabled:
                self._sync_audio_player()
            else:
                self._stop_audio_process()

        def _sync_audio_player(self):
            target = self._audio_target_from_status()

            if not self.audio_auto_play.isChecked() or target is None:
                self._stop_audio_process()
                return

            if self.audio_process.state() != QProcess.ProcessState.NotRunning:
                if self.audio_target == target:
                    return

                self._stop_audio_process(immediate=True)
                return

            self._start_audio_process(target)

        def start_audio_player(self):
            if self._audio_target_from_status() is None:
                self.statusBar().showMessage("No active audio service.")
                return

            if not self.audio_auto_play.isChecked():
                self.audio_auto_play.setChecked(True)
                return

            self._sync_audio_player()

        def stop_audio_player(self):
            self.audio_auto_play.setChecked(False)
            self._stop_audio_process()

        def _start_audio_process(self, target):
            mpv = shutil.which("mpv")
            if not mpv:
                self.audio_player_state.setText("mpv not found")
                self.statusBar().showMessage("mpv was not found in PATH.")
                return

            audio_pipe, audio_codec = target[:2]
            arguments = mpv_arguments(audio_pipe, audio_codec)
            if not arguments:
                return

            self.audio_target = target
            self.audio_stopping = False
            self.audio_stop_timer.stop()
            self.audio_player_state.setText("Starting…")
            self.audio_process.setProgram(mpv)
            self.audio_process.setArguments(arguments)
            self.audio_process.start()

        def _stop_audio_process(self, immediate=False):
            self.audio_target = None

            if self.audio_process.state() == QProcess.ProcessState.NotRunning:
                self.audio_stopping = False
                self.audio_stop_timer.stop()
                self.audio_player_state.setText("Stopped")
                return

            if self.audio_stopping:
                if immediate:
                    self.audio_process.kill()
                return

            self.audio_stopping = True
            self.audio_player_state.setText("Stopping…")
            self.audio_stop_timer.start()
            if immediate:
                self.audio_process.kill()
            else:
                self.audio_process.terminate()

        def _kill_audio_process_if_needed(self):
            if self.audio_stopping and self.audio_process.state() != QProcess.ProcessState.NotRunning:
                self.audio_process.kill()

        def _audio_process_started(self):
            if self.audio_stopping:
                return

            codec = self.audio_target[1].upper() if self.audio_target else ""
            self.audio_player_state.setText(f"Playing {codec}" if codec else "Playing")

        def _audio_process_finished(self, exit_code, exit_status):
            self.audio_stopping = False
            self.audio_stop_timer.stop()
            self.audio_target = None
            self.audio_player_state.setText("Stopped")

            if self.closing or not self.audio_auto_play.isChecked():
                return

            if self._audio_target_from_status() is not None:
                QTimer.singleShot(250, self._sync_audio_player)

        def _audio_process_error(self, error):
            if self.closing or (self.audio_stopping and error == QProcess.ProcessError.Crashed):
                return

            self.audio_player_state.setText("Player error")
            self.statusBar().showMessage(f"mpv error: {self.audio_process.errorString()}")

        def _update_channels(self, channels):
            current_frequency = self.status["frequency"] if self.status else 0
            self.channel_combo.blockSignals(True)
            self.channel_combo.clear()

            for channel, frequency in channels:
                self.channel_combo.addItem(f"{channel} · {frequency} kHz", (channel, frequency))

            if current_frequency:
                for index in range(self.channel_combo.count()):
                    data = self.channel_combo.itemData(index)
                    if data and data[1] == current_frequency:
                        self.channel_combo.setCurrentIndex(index)
                        break

            self.channel_combo.blockSignals(False)

        def _update_overview(self, status):
            self.state_value.setText(status["state"])
            self.ensemble_value.setText(format_ensemble(status["ensemble"]))
            frequency = status["frequency"]
            self.frequency_value.setText(f"{frequency} kHz" if frequency else "-")

            if frequency and not self.frequency_spin.hasFocus():
                self.frequency_spin.setValue(frequency)

            service = format_service(status["service"]) if status["service"] else None
            self.service_value.setText(f'{service["label"]} ({display_sid(service["id"])})' if service else "-")
            self.logo_path_value.setText(status["logo_paths"].get(service["id"], "-") if service else "-")
            self._refresh_service_logos()
            self.time_value.setText(format_date_time(status["date_time"]))
            self._update_audio_statistics(status.get("audio_statistics"), status["audio_codec"])
            self.audio_bitrate_value.setText(f'{service["bitrate"]} kbit/s' if service and status["audio_codec"] and service["bitrate"] else "-")
            self.audio_protection_value.setText(service["protection"] if service and status["audio_codec"] and service["protection"] else "-")
            self.audio_pipe_value.setText(status["audio_pipe"] or "-")

            dlp = status["dlp"]
            title_parts = [part for part in [dlp["artist"], dlp["title"]] if part]
            self.now_playing.setText(" — ".join(title_parts) if title_parts else (status["dls"] or "-"))
            self.album_value.setText(f'Album: {dlp["album"]}' if dlp["album"] else "")
            self.dls_value.setText(status["dls"] if title_parts and status["dls"] else "")

            active_slide = next((item for item in status["slideshow_items"] if item["active"]), None)
            slide_path = active_slide["path"] if active_slide and active_slide["path"] else status["slideshow"]
            metadata = []

            if active_slide:
                if active_slide["category_id"] or active_slide["slide_id"]:
                    metadata.append(f'{active_slide["category_id"]}/{active_slide["slide_id"]}')
                if active_slide["category_title"]:
                    metadata.append(active_slide["category_title"])
                if active_slide["content_name"]:
                    metadata.append(active_slide["content_name"])
                if active_slide["alert"]:
                    metadata.append(f'alert {active_slide["alert"]}')

            self.slide_meta.setText(" · ".join(metadata))
            self._set_slide(slide_path)

        def _read_logo(self, source):
            reader = QImageReader(source)
            size = reader.size()

            if size.width() != size.height() or size.width() <= 0 or size.width() > MAX_LOGO_DIMENSION:
                return QPixmap()

            reader.setAutoTransform(True)
            image = reader.read()

            if image.isNull():
                return QPixmap()

            return QPixmap.fromImage(image).scaled(112, 112, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)

        def _logo_with_border(self, pixmap, size=112):
            framed = QPixmap(size, size)
            framed.fill(Qt.GlobalColor.transparent)
            painter = QPainter(framed)
            painter.drawPixmap(1, 1, pixmap.scaled(size - 2, size - 2, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
            painter.setPen(self.palette().color(QPalette.ColorRole.Mid))
            painter.drawRect(0, 0, size - 1, size - 1)
            painter.end()
            return framed

        def _store_logo(self, source, pixmap):
            self.logo_images[source] = pixmap

            while len(self.logo_images) > MAX_LOGO_IMAGES:
                self.logo_images.pop(next(iter(self.logo_images)))

        def _service_logo_pixmap(self, service_id):
            if not self.status or service_id is None:
                return QPixmap()

            ensemble = self.status["ensemble_status"]
            service = next((item for item in self.status["services"] if item[0] == service_id), None)
            ecc = service[3] if service and service[3] else ensemble["ecc"]
            candidates = square_logo_candidates(self.status["service_logos"], service_id, ensemble["id"], ecc)

            if not candidates:
                if service:
                    # Some ensembles send SID/name-based logo files before the SI object.
                    candidates = asset_logo_candidates(self.status["spi_assets"], service)

            paths = [self.status["logo_paths"].get(service_id, "")] + [item["path"] for item in candidates]

            for path in paths:

                if not path or not os.path.isfile(path):
                    continue

                try:
                    modified = os.stat(path).st_mtime_ns
                except OSError:
                    continue

                source = (self.tuner_path, self.status["frequency"], path, modified)

                if source not in self.logo_images:
                    self._store_logo(source, self._read_logo(path))

                pixmap = self.logo_images[source]

                if not pixmap.isNull():
                    return pixmap

            for item in candidates:
                source = item["url"]

                if source in self.logo_images:
                    return self.logo_images[source]

                self._request_logo(source)

            return QPixmap()

        def _request_logo(self, source):
            url = QUrl(source)

            if url.scheme() not in {"http", "https"} or not url.host():
                return
            if source in self.logo_requests or len(self.logo_requests) >= MAX_LOGO_REQUESTS:
                return
            if self.logo_failures.get(source, 0) > monotonic():
                return

            request = QNetworkRequest(url)
            request.setTransferTimeout(10000)
            request.setMaximumRedirectsAllowed(3)
            reply = self.logo_network.get(request)
            reply.setReadBufferSize(MAX_LOGO_BYTES + 1)
            self.logo_requests[source] = reply

            def check_size(received, total):
                if received > MAX_LOGO_BYTES or total > MAX_LOGO_BYTES:
                    reply.abort()

            reply.downloadProgress.connect(check_size)
            reply.finished.connect(lambda: self._logo_download_finished(source, reply))

        def _logo_download_finished(self, source, reply):
            self.logo_requests.pop(source, None)
            pixmap = QPixmap()

            if not self.closing and reply.error() == QNetworkReply.NetworkError.NoError:
                data = reply.readAll()

                if len(data) <= MAX_LOGO_BYTES:
                    buffer = QBuffer()
                    buffer.setData(data)
                    buffer.open(QIODevice.OpenModeFlag.ReadOnly)
                    pixmap = self._read_logo(buffer)

            if not pixmap.isNull():
                self._store_logo(source, pixmap)
                self.logo_failures.pop(source, None)
            else:
                self.logo_failures[source] = monotonic() + 60

                while len(self.logo_failures) > MAX_LOGO_IMAGES:
                    self.logo_failures.pop(next(iter(self.logo_failures)))

            reply.deleteLater()

            if not self.closing:
                self._refresh_service_logos()

        def _refresh_service_logos(self):
            if self.closing or not self.status:
                return

            service_id = self.status["service"][0] if self.status["service"] else None
            pixmap = self._service_logo_pixmap(service_id)
            self.service_logo.clear()
            self.service_logo.setToolTip(self.status["logo_paths"].get(service_id, ""))

            if pixmap.isNull():
                self.service_logo.setText("No logo")
            else:
                self.service_logo.setPixmap(self._logo_with_border(pixmap))

            model = self.services_table.model()
            service_ids = {service[0] for service in self.status["services"]}
            decorations = {}
            tooltips = {}

            for row, service_id in enumerate(model.user_data):
                if service_id not in service_ids:
                    continue

                pixmap = self._service_logo_pixmap(service_id)

                if not pixmap.isNull():
                    decorations[(row, 0)] = self._logo_with_border(pixmap, 40)

                tooltips[(row, 0)] = self.status["logo_paths"].get(service_id, "")
                tooltips[(row, 1)] = model.data(model.index(row, 1))

            model.set_decorations(decorations)
            model.set_tooltips(tooltips)
            self._update_service_details_logo()

        def _set_slide(self, path):
            if path == self.slide_path:
                return

            self.slide_path = path
            if not path or not os.path.isfile(path):
                self.slide_image.setPixmap(QPixmap())
                self.slide_image.setText(path or "No slide")
                return

            pixmap = QPixmap(path)
            if pixmap.isNull():
                self.slide_image.setPixmap(QPixmap())
                self.slide_image.setText(path)
                return

            self.slide_image.setText("")
            self.slide_image.setPixmap(pixmap.scaled(700, 420, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))

        def _update_signal(self, signal):
            if signal["signal_quality_valid"]:
                self.signal_quality.setValue(signal["signal_quality"])
                self.signal_quality.setFormat(f'{signal["signal_quality"]} %')
            else:
                self.signal_quality.setValue(0)
                self.signal_quality.setFormat("-")

            self.signal_level.setText(f'{signal["antenna_level"]}/6' if signal["msc_cer_valid"] else "-")
            self.signal_locks.setText(f'{"Yes" if signal["ofdm_lock"] else "No"} / {"Yes" if signal["fec_lock"] else "No"} / {"Yes" if signal["fec_sync"] else "No"}')
            self.signal_msc_cer.setText(f'{signal["msc_cer"] / 100:.2f} %' if signal["msc_cer_valid"] else "-")
            self.signal_fic_crc.setText(f'{signal["fic_crc_error_rate"] / 100:.2f} %' if signal["fic_crc_error_rate_valid"] else "-")

        def _update_services(self, status):
            rows = []
            ids = []
            active_id = status["service"][0] if status["service"] else None
            selected_id = self._selected_table_data(self.services_table)

            for raw in status["services"]:
                service = format_service(raw)
                rows.append([
                    "",
                    service["label"] or service["short_label"] or "-",
                    display_sid(service["id"]),
                    service["codec"],
                    service["bitrate"],
                    service["subchannel"],
                    service["pty"] or "-",
                    service["language"] or "-",
                    ", ".join(service["announcements"]) or "-",
                ])
                ids.append(service["id"])

            self._set_table_rows(self.services_table, rows, ids)
            self._refresh_service_logos()

            selected_id = selected_id if selected_id in ids else active_id

            if selected_id is not None:
                for row, service_id in enumerate(ids):
                    if service_id == selected_id:
                        self.services_table.selectRow(row)
                        break

        def _update_components(self, components):
            rows = []

            for raw in components:
                component = format_service_component(raw)
                applications = ", ".join(f'{application["name"] or "0x%03X" % application["type"]}' for application in component["user_applications"])
                rows.append([
                    display_sid(component["service_id"], component["data_service"]),
                    component["id"],
                    component["label"] or component["short_label"] or "-",
                    component["type"],
                    component["subchannel"] if component["subchannel"] != 0xFF else "-",
                    component["packet_address"] if component["packet_address"] != 0xFFFF else "-",
                    component["fec_scheme"],
                    applications or "-",
                ])

            self._set_table_rows(self.components_table, rows)

        def open_selected_service(self):
            service_id = self._selected_table_data(self.services_table)

            if service_id is not None:
                self.worker.invoke("set_service", service_id)

        def _build_service_details(self):
            self.service_details_dialog = QDialog(self)
            self.service_details_dialog.setWindowTitle("Service details")
            self.service_details_dialog.resize(1120, 720)
            layout = QVBoxLayout(self.service_details_dialog)
            header = QHBoxLayout()
            header.setSpacing(20)
            self.service_details_logo = QLabel("No logo")
            self.service_details_logo.setFixedSize(112, 112)
            self.service_details_logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
            header.addWidget(self.service_details_logo)

            self.service_details_form = QFormLayout()
            self.service_details_form.setVerticalSpacing(6)
            self.service_details_name = QLabel("-")
            self.service_details_identity = QLabel("-")
            self.service_details_ensemble = QLabel("-")
            self.service_details_audio = QLabel("-")
            self.service_details_logo_path = QLabel("-")

            for label in (self.service_details_name, self.service_details_identity, self.service_details_ensemble, self.service_details_audio, self.service_details_logo_path):
                label.setTextFormat(Qt.TextFormat.PlainText)
                label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
                label.setWordWrap(True)

            self.service_details_form.addRow("Service", self.service_details_name)
            self.service_details_form.addRow("Identity", self.service_details_identity)
            self.service_details_form.addRow("Ensemble", self.service_details_ensemble)
            self.service_details_form.addRow("Audio", self.service_details_audio)
            self.service_details_form.addRow("Logo path", self.service_details_logo_path)
            header.addLayout(self.service_details_form, 1)
            layout.addLayout(header)
            layout.addSpacing(12)

            splitter = QSplitter(Qt.Orientation.Vertical)
            frequencies = QGroupBox("Alternative frequencies and ensembles")
            frequencies_layout = QVBoxLayout(frequencies)
            self.service_frequencies_note = QLabel("")
            self.service_frequencies_note.setWordWrap(True)
            frequencies_layout.addWidget(self.service_frequencies_note)
            self.service_frequencies_table = self._table(["Ensemble", "EId", "Channel", "Frequency", "Area", "Continuous output"])

            for column, width in enumerate((240, 90, 80, 130, 160)):
                self.service_frequencies_table.setColumnWidth(column, width)

            frequencies_layout.addWidget(self.service_frequencies_table)
            splitter.addWidget(frequencies)

            links = QGroupBox("Linked services")
            links_layout = QVBoxLayout(links)
            self.service_links_note = QLabel("")
            self.service_links_note.setWordWrap(True)
            links_layout.addWidget(self.service_links_note)
            self.service_links_table = self._table(["Service", "System", "ID / ECC", "Relationship", "Status", "Set", "Scope", "Frequencies"])

            for column, width in enumerate((160, 90, 125, 180, 85, 70, 110)):
                self.service_links_table.setColumnWidth(column, width)

            links_layout.addWidget(self.service_links_table)
            splitter.addWidget(links)
            layout.addWidget(splitter, 1)
            buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
            buttons.rejected.connect(self.service_details_dialog.reject)
            layout.addWidget(buttons)

        def show_service_details(self):
            service_id = self._selected_table_data(self.services_table)

            if not self.status or not any(service[0] == service_id for service in self.status["services"]):
                return

            if self.service_details_dialog is None:
                self._build_service_details()

            self.service_details_id = service_id
            self.service_details_context = (self.tuner_path, self.status["frequency"], self.status["ensemble_status"]["id"])
            self.service_details_dialog.show()
            self._update_service_details()
            self.service_details_dialog.raise_()
            self.service_details_dialog.activateWindow()

        def _update_service_details_logo(self):
            if self.service_details_dialog is None or not self.service_details_dialog.isVisible():
                return

            enabled = SPI_INTERFACE in self.available_interfaces
            self.service_details_logo.setVisible(enabled)
            self.service_details_form.setRowVisible(self.service_details_logo_path, enabled)
            pixmap = self._service_logo_pixmap(self.service_details_id)
            self.service_details_logo.clear()

            if pixmap.isNull():
                self.service_details_logo.setText("No logo")
            else:
                self.service_details_logo.setPixmap(self._logo_with_border(pixmap))

            path = self.status["logo_paths"].get(self.service_details_id, "") if self.status else ""
            self.service_details_logo_path.setText(path or "-")
            self.service_details_logo.setToolTip(path)

        def _update_service_details(self):
            if self.service_details_dialog is None or not self.service_details_dialog.isVisible():
                return

            service = next((format_service(item) for item in self.status["services"] if item[0] == self.service_details_id), None) if self.status else None

            if service is None:
                self.service_details_name.setText("Service unavailable")
                self.service_details_identity.setText(display_sid(self.service_details_id))
                self.service_details_ensemble.setText("-")
                self.service_details_audio.setText("-")
                self.service_details_logo.clear()
                self.service_details_logo.setText("No logo")
                self.service_details_logo.setToolTip("")
                self.service_details_logo_path.setText("-")
                self.service_frequencies_note.setText("No current service data.")
                self.service_links_note.setText("No current service data.")
                self.service_frequencies_note.setVisible(True)
                self.service_links_note.setVisible(True)
                self._set_table_rows(self.service_frequencies_table, [])
                self._set_table_rows(self.service_links_table, [])
                return

            tuner_path, frequency, ensemble_id = self.service_details_context
            ensemble = self.status["ensemble_status"]

            if tuner_path != self.tuner_path or frequency != self.status["frequency"] or (ensemble_id and ensemble["id"] and ensemble_id != ensemble["id"]):
                self.service_details_dialog.hide()
                return

            if not ensemble_id and ensemble["id"]:
                self.service_details_context = (tuner_path, frequency, ensemble["id"])

            ecc = service["ecc"] or ensemble["ecc"]
            self.service_details_name.setText(service["label"] or service["short_label"] or "-")
            self.service_details_identity.setText(f'{display_sid(service["id"])} · ECC 0x{ecc:02X}' if ecc else display_sid(service["id"]))
            self.service_details_ensemble.setText(f'{format_ensemble(self.status["ensemble"])} · 0x{ensemble["id"]:04X} · {frequency / 1000:.3f} MHz')
            self.service_details_audio.setText(f'{service["codec"]} · {service["bitrate"]} kbit/s · {service["protection"] or "-"} · {service["language"] or "-"}')
            self._update_service_details_logo()
            details = service_following_details(self.status, self.service_details_id)
            rows = []

            for item in details["frequencies"]:
                rows.append([
                    item["label"] or "-",
                    f'0x{item["ensemble_id"]:04X}',
                    ensemble_for_frequency(item["frequency"], self.status["channels"]),
                    f'{item["frequency"] / 1000:.3f} MHz' if item["frequency"] else "Not signalled",
                    ("Adjacent" if item["adjacent"] else "Not adjacent") if item["frequency"] else "-",
                    ("Possible" if item["continuous"] else "Not expected") if item["frequency"] else "-",
                ])

            self._set_table_rows(self.service_frequencies_table, rows)
            self.service_frequencies_table.model().set_tooltips({(row, column): str(value) for row, values in enumerate(rows) for column, value in enumerate(values)})
            frequency_note = "" if rows else "No alternative frequencies or ensembles signalled yet."

            if FREQUENCY_INFO_INTERFACE not in self.available_interfaces:
                frequency_note = "Frequency information is disabled in this daemon."

            self.service_frequencies_note.setText(frequency_note)
            self.service_frequencies_note.setVisible(bool(frequency_note))
            rows = []

            for item in details["links"]:
                identifier = f'0x{item["id"]:06X}' if item["type"] in {"DRM", "DRM/AMSS"} else display_sid(item["id"])

                if item["ecc"]:
                    identifier += f' / {item["ecc"]:02X}'

                rows.append([
                    item["label"] or "-",
                    item["type"],
                    identifier,
                    "Same programme (hard)" if item["hard"] else "Related programme (soft)",
                    "Active" if item["active"] else "Inactive",
                    f'0x{item["linkage_set"]:03X}',
                    "International" if item["international"] else "National",
                    ", ".join(f'{value / 1000:.3f} MHz' for value in item["frequencies"]) or "Not signalled",
                ])

            self._set_table_rows(self.service_links_table, rows)
            self.service_links_table.model().set_tooltips({(row, column): str(value) for row, values in enumerate(rows) for column, value in enumerate(values)})
            links_note = "" if rows else "No linked services signalled for this service yet."

            if SERVICE_LINKING_INTERFACE not in self.available_interfaces:
                links_note = "Service linking is disabled in this daemon."

            self.service_links_note.setText(links_note)
            self.service_links_note.setVisible(bool(links_note))

        def tune_scan_result(self, item, column):
            target = item.data(0, Qt.ItemDataRole.UserRole)
            if target and self.status is not None and self.tuner_path:
                self.worker.invoke("tune_scan_result", target[0], target[1], self.tuner_path)

        def _update_scan(self, status, update_results=True):
            state = status["scan_state"]
            mode = status["scan_mode"]
            progress = status["scan_progress"]
            active = state in {"Scanning", "Stopping"}
            self.scan_fast_button.setEnabled(not active and "fast" in status["scan_modes"])
            self.scan_full_button.setEnabled(not active and "full" in status["scan_modes"])
            self.scan_stop_button.setEnabled(state == "Scanning")
            if state in {"Idle", "Cancelled"}:
                state = "Idle"
                mode = ""
                progress = {"current": 0, "total": 0, "channel": "", "frequency": 0}
            self.scan_state.setText(f'{state}{" · " + mode if mode else ""}')
            total = max(1, progress["total"])
            self.scan_progress.setRange(0, total)
            self.scan_progress.setValue(min(progress["current"], total))
            label = progress["channel"]
            if progress["frequency"]:
                label += f' · {progress["frequency"]} kHz'
            self.scan_progress.setFormat(f'{progress["current"]}/{progress["total"]} · {label}' if progress["total"] else "")

            if not update_results:
                return

            selected = self.scan_results.currentItem()
            selected_target = selected.data(0, Qt.ItemDataRole.UserRole) if selected is not None else None
            self.scan_results.setUpdatesEnabled(False)
            self.scan_results.clear()
            for result in status["scan_results"]:
                ensemble = QTreeWidgetItem([
                    result["label"] or result["short_label"] or result["channel"],
                    f'{result["frequency"]} kHz',
                    f'0x{result["ensemble_id"]:04X}',
                    "",
                    "",
                    "",
                ])
                ensemble.setData(0, Qt.ItemDataRole.UserRole, (result["frequency"], None))
                self.scan_results.addTopLevelItem(ensemble)
                if ensemble.data(0, Qt.ItemDataRole.UserRole) == selected_target:
                    self.scan_results.setCurrentItem(ensemble)

                for service in result["services"]:
                    child = QTreeWidgetItem([
                        service["label"] or service["short_label"] or "-",
                        "",
                        display_sid(service["id"]),
                        service["codec"],
                        str(service["bitrate"]),
                        service["pty"] or "-",
                    ])
                    child.setData(0, Qt.ItemDataRole.UserRole, (result["frequency"], service["id"]))
                    ensemble.addChild(child)
                    if child.data(0, Qt.ItemDataRole.UserRole) == selected_target:
                        self.scan_results.setCurrentItem(child)

            self.scan_results.expandAll()
            self.scan_results.setUpdatesEnabled(True)

        def _request_fic_load(self):
            if self.tabs.currentWidget() is not self.fic_tab or self.fic_pause_button.isChecked():
                return

            if self.status and self.status.get("fic_revision") is not None:
                self.worker.invoke("load_fic", self.fic_sequence, self.fic_view_epoch)

        def _reset_fic_view(self):
            self.fic_generation = None
            self.fic_sequence = 0
            self.fic_latest = 0
            self.clear_fic_view()

        def clear_fic_view(self):
            self.fic_view_epoch += 1
            self.fic_sequence = self.fic_latest
            self.fic_figures = []
            self.fic_previous = {}
            self.fic_missed = 0
            self.fic_follow = True
            self._render_fic_figures()

        def _fic_pause_changed(self, paused):
            self.fic_pause_button.setText("Resume" if paused else "Pause")
            if not paused:
                self._request_fic_load()

        def on_fic_loaded(self, data):
            if data["path"] != self.tuner_path or data["view_epoch"] != self.fic_view_epoch or self.fic_pause_button.isChecked():
                return

            changed = False
            if self.fic_generation != data["generation"]:
                self.fic_figures = []
                self.fic_previous = {}
                self.fic_generation = data["generation"]
                self.fic_sequence = data["oldest"] - 1
                self.fic_missed = 0
                self.fic_follow = True
                changed = True
            elif data["oldest"] > self.fic_sequence + 1:
                self.fic_missed += data["oldest"] - self.fic_sequence - 1
                self.fic_sequence = data["oldest"] - 1
                changed = True

            self.fic_latest = data["revision"]
            incoming = [figure for figure in data["figures"] if figure["sequence"] > self.fic_sequence]
            if incoming:
                self.fic_sequence = incoming[-1]["sequence"]
                self.fic_figures = (self.fic_figures + incoming)[-MAX_FIC_FIGURES:]
                self.fic_previous = {}
                latest = {}
                for figure in self.fic_figures:
                    key = fic_figure_key(figure)
                    self.fic_previous[figure["sequence"]] = latest.get(key)
                    latest[key] = figure
                changed = True
            elif not data["figures"]:
                self.fic_sequence = max(self.fic_sequence, data["revision"])

            if changed:
                self._render_fic_figures()

        def _render_fic_figures(self, *args):
            selected = self._selected_table_data(self.fic_history_table)
            selected_sequence = selected["sequence"] if selected else None
            scroll = self.fic_history_table.verticalScrollBar().value()
            fig_type = self.fic_type_filter.currentData()
            extension = self.fic_extension_filter.currentData()
            search = self.fic_search.text().strip().casefold()
            figures = []
            for figure in self.fic_figures:
                if fig_type != -1 and figure["type"] != fig_type:
                    continue
                if extension != -1 and figure["extension"] != extension:
                    continue
                if search:
                    text = " ".join([figure["name"], figure["status"], figure["data"].hex(" "), figure["data"].hex()] + [f"{name} {value}" for name, value in figure["fields"]])
                    if search not in text.casefold():
                        continue
                figures.append(figure)

            rows = []
            for figure in figures:
                received = datetime.fromtimestamp(figure["received"] / 1000).strftime("%H:%M:%S.%f")[:-3]
                label = "FIB" if figure["type"] == 255 else str(figure["type"])
                if figure["extension"] >= 0:
                    label += f'/{figure["extension"]}'
                rows.append([figure["sequence"], received, label, figure["name"], len(figure["data"]), figure["status"]])

            self.fic_rendering = True
            self._set_table_rows(self.fic_history_table, rows, figures)
            if figures:
                row = len(figures) - 1 if self.fic_follow else next((index for index, figure in enumerate(figures) if figure["sequence"] == selected_sequence), len(figures) - 1)
                self.fic_history_table.selectRow(row)
                if self.fic_follow:
                    self.fic_history_table.scrollToBottom()
                else:
                    self.fic_history_table.verticalScrollBar().setValue(scroll)
            self.fic_rendering = False
            count = f"{len(figures)} / {len(self.fic_figures)} entries"
            if self.fic_missed:
                count += f" · {self.fic_missed} missed"
            self.fic_count.setText(count)
            self._show_fic_figure()

        def _fic_selection_changed(self, current, previous):
            if self.fic_rendering:
                return
            self.fic_follow = current.row() == self.fic_history_table.model().rowCount() - 1
            self._show_fic_figure()

        def _show_fic_figure(self):
            figure = self._selected_table_data(self.fic_history_table)
            if figure is None:
                self.fic_comparison.clear()
                self._set_table_rows(self.fic_fields_table, [])
                self.fic_raw.clear()
                return

            previous = self.fic_previous.get(figure["sequence"])
            old_fields = dict(previous["fields"]) if previous else {}
            fields = dict(figure["fields"])
            rows = []
            for name, value in figure["fields"]:
                change = ""
                if previous and old_fields.get(name) != value:
                    change = "Changed" if name in old_fields else "Added"
                rows.append([name, old_fields.get(name, "-"), value, change])
            for name, value in old_fields.items():
                if name not in fields:
                    rows.append([name, value, "-", "Removed"])
            self._set_table_rows(self.fic_fields_table, rows)
            comparison = f'#{figure["sequence"]}'
            raw = f'Current #{figure["sequence"]}\n{fic_hex_dump(figure["data"])}'
            if previous:
                comparison += f' · compared with #{previous["sequence"]}'
                raw += f'\n\nPrevious #{previous["sequence"]}\n{fic_hex_dump(previous["data"])}'
                offsets = [f"{offset:04X}" for offset in range(max(len(figure["data"]), len(previous["data"]))) if figure["data"][offset:offset + 1] != previous["data"][offset:offset + 1]]
                raw += "\n\nChanged offsets: " + (", ".join(offsets) or "-")
            self.fic_comparison.setText(comparison)
            self.fic_raw.setPlainText(raw)

        def _update_diagnostics(self, status):
            hardware = status["hardware_status"]
            self.diagnostics_hardware_box.setVisible(DIAGNOSTICS_INTERFACE in self.available_interfaces and hardware is not None)

            if hardware is not None:
                error = hardware["last_usb_error"]
                self.diag_usb_error.setText(f'{hardware["last_usb_error_name"]} ({error})' if error else "No USB errors")
                self.diag_usb_error_time.setText(format_hardware_time(hardware["last_usb_error_time"]))
                self.diag_usb_errors.setText(f'{hardware["usb_errors"]} / {hardware["usb_timeouts"]}')
                self.diag_last_data.setText(format_hardware_time(hardware["last_data_time"]))
                recovery_state = "Active" if hardware["recovering"] else "Idle"
                self.diag_recovery.setText(f'{recovery_state} · {hardware["recovery_successes"]}/{hardware["recovery_attempts"]} successful')
                self.diag_recovery_reason.setText(hardware["recovery_reason"] or "-")

            slideshow = status["slideshow_statistics"]
            self.diag_slideshow_groups.setText(f'{slideshow["groups_accepted"]}/{slideshow["groups_received"]} accepted · {slideshow["group_crc_errors"]} CRC errors')

            if slideshow["total_segments_known"]:
                missing = max(0, slideshow["total_segments"] - slideshow["received_segments"])
                self.diag_slideshow_segments.setText(f'0x{slideshow["transport_id"]:04X} · {slideshow["received_segments"]}/{slideshow["total_segments"]} received · {missing} missing')
            elif slideshow["received_segments"]:
                self.diag_slideshow_segments.setText(f'0x{slideshow["transport_id"]:04X} · {slideshow["received_segments"]} received · total pending')
            else:
                self.diag_slideshow_segments.setText("Waiting for image data")

            self.diag_slideshow_objects.setText(f'{slideshow["objects"]} images · {slideshow["header_parses"]} headers · {slideshow["segments"]} segments')

            if slideshow["first_image_received"]:
                source = "cache" if slideshow["cache_hit"] else "broadcast"
                self.diag_slideshow_first_image.setText(f'{slideshow["first_image_ms"] / 1000:.3f} s · {source}')
            else:
                self.diag_slideshow_first_image.setText("Waiting for first image")

            ensemble = status["ensemble_status"]
            self.diag_ensemble_id.setText(f'0x{ensemble["id"]:04X}' if ensemble["id"] else "-")
            self.diag_ecc.setText(f'0x{ensemble["ecc"]:02X}' if ensemble["ecc"] else "-")
            self.diag_services.setText(f'{ensemble["services_found"]}/{ensemble["service_count"]}' if ensemble["service_count"] else str(len(status["services"])))
            self.diag_alarm.setText(f'{"supported" if ensemble["alarm_supported"] else "not supported"} · {"ACTIVE" if ensemble["alarm_active"] else "inactive"}')
            lto = ensemble["local_time_offset"]
            self.diag_lto.setText(f'{"+" if lto >= 0 else "-"}{abs(lto) // 60:02d}:{abs(lto) % 60:02d}')
            self.diag_reconfig.setText(str(ensemble["reconfiguration_counter"]))
            self.diag_cif.setText(str(ensemble["cif_counter"]))

            fic = status["fic_statistics"]
            self.diag_fib.setText(f'{fic["fibs_valid"]}/{fic["fibs_received"]} valid · {fic["fibs_crc_errors"]} CRC errors')
            self.diag_figs.setText(str(fic["figs_received"]))
            self.diag_fig0.setText(", ".join(str(index) for index, count in enumerate(fic["fig0_extensions"]) if count) or "-")
            self.diag_fig5.setText(", ".join(str(index) for index, count in enumerate(fic["fig5_extensions"]) if count) or "-")
            following = len(status["service_links"]) + len(status["frequency_information"]) + len(status["other_ensemble_services"])
            announcements = len(status["oe_announcement_support"]) + len(status["oe_announcement_switches"]) + len(status["fm_announcement_support"]) + len(status["fm_announcement_switches"])
            self.diag_following.setText(f'{following} refs · {announcements} announcements')

            ews_rows = []
            for alert in status["emergency_warning"]["alerts"]:
                source = f'OE 0x{alert["ensemble_id"]:04X}' if alert["other_ensemble"] else f'SubCh {alert["subchannel"]}'
                locations = ", ".join(f'{location["nff"]}:{location["zone"]}' for location in alert["locations"]) or "-"
                ews_rows.append([source, alert["phase"], alert["stage"] or "-", alert["incident_id"], locations])
            self._set_table_rows(self.ews_table, ews_rows)

            fidc_rows = []
            for entry in status["fidc"]:
                data = " ".join(f'{value:02X}' for value in entry["data"])
                fidc_rows.append([entry["extension"], entry["component_id"], entry["type"], f'{int(entry["d1"])}/{int(entry["d2"])}', data or "-"])
            self._set_table_rows(self.fidc_table, fidc_rows)

        def on_main_tab_changed(self, index):
            if self.status is None:
                return

            page = self.tabs.widget(index)
            if page is self.overview_tab:
                self._update_channels(self.status["channels"])
                self._update_overview(self.status)
                self._update_signal(self.status["signal_status"])
            elif page is self.services_tab:
                self._update_services(self.status)
                self._update_components(self.status["service_components"])
            elif page is self.data_tab:
                self.on_data_tab_changed(self.data_tabs.currentIndex())
            elif page is self.scan_tab:
                self._update_scan(self.status, True)
            elif page is self.diagnostics_tab:
                self._update_diagnostics(self.status)
            elif page is self.fic_tab:
                self._request_fic_load()

        def on_data_tab_changed(self, index):
            if self.tabs.currentWidget() is not self.data_tab:
                return

            if self.status is not None:
                self._update_data_path(self.status)

            if index == 0:
                self._request_spi_load()
            elif index == 1:
                self._request_journaline_load()
            elif index == 2:
                self.worker.invoke("load_tpeg")

        def _reset_spi_view(self):
            self.spi_refresh_timer.stop()
            self.spi_view_epoch += 1
            self.spi_group_request_id += 1
            self.spi_data = None
            self.spi_detail_rows.clear()
            self.spi_detail_user_data.clear()
            self.spi_detail_counts.clear()
            self.epg_selected_key = None
            self.spi_group_selected_key = None
            self.spi_group_programme_key = None
            self.spi_status.setText("No programme information")
            self.epg_count_label.setText("No EPG loaded")
            self.epg_detail.clear()
            self.spi_group_detail.clear()

            for table in (self.spi_transport_table, self.spi_services_table, self.spi_programmes_table, self.spi_groups_table, self.spi_group_programmes, self.spi_objects_table, self.spi_assets_table):
                self._set_table_rows(table, [])

        def _request_spi_load(self):
            if not hasattr(self, "spi_detail_tabs"):
                return

            detail_index = self.spi_detail_tabs.currentIndex()

            if detail_index == 1:
                service_id = self.epg_service_combo.currentData()
                service_id = -1 if service_id is None else int(service_id)
                epg_date = self.epg_date_edit.date().toString("yyyy-MM-dd")
                self.worker.invoke("load_spi", detail_index, service_id, epg_date, self.spi_view_epoch)
                return

            self.worker.invoke("load_spi", detail_index, -1, "", self.spi_view_epoch)

        def _data_tab_active(self, index):
            return self.tabs.currentWidget() is self.data_tab and self.data_tabs.currentIndex() == index

        def _update_data_path(self, status):
            statistics = status["data_path_statistics"]
            application = status["data_application"] or "-"
            self.data_path_main.setText(
                f'{application} · MSC {statistics["msc_bytes"]:,} B · packets OK {statistics["packets_valid"]:,} · CRC bad {statistics["packet_crc_errors"]:,} · address {statistics["packets_addressed"]:,} · groups {statistics["data_groups"]:,}'
            )
            self.data_path_detail.setText(
                f'continuity {statistics["packet_continuity_errors"]:,} · FEC {statistics["fec_frames"]:,}/{statistics["fec_errors"]:,} errors · MOT groups {statistics["mot_groups_received"]:,}/{statistics["mot_groups_accepted"]:,} accepted · MOT CRC bad {statistics["mot_crc_errors"]:,} · segments {statistics["mot_segments"]:,} · directories {statistics["mot_directories"]:,} · objects {statistics["mot_objects"]:,} · SPI objects {statistics["spi_objects"]:,} · SPI updates {statistics["spi_updates"]:,}'
            )

        def on_spi_loaded(self, data):
            if self.status is None or data.get("context") != (self.tuner_path, self.status["frequency"]) or data.get("view_epoch") != self.spi_view_epoch:
                return

            self.spi_data = data
            detail_index = data["detail_index"]
            self._set_table_rows(self.spi_transport_table, data["transport_rows"], data["transports"])

            if detail_index == 1:
                current_service_id = self.epg_service_combo.currentData()
                current_service_id = -1 if current_service_id is None else int(current_service_id)
                current_date = self.epg_date_edit.date().toString("yyyy-MM-dd")

                if data.get("epg_service_id", -1) != current_service_id or data.get("epg_date", "") != current_date:
                    return

            self.spi_detail_rows[detail_index] = data["detail_rows"]
            self.spi_detail_user_data[detail_index] = data.get("detail_user_data", [])
            self.spi_detail_counts[detail_index] = len(data["detail_rows"])
            counts = []

            for index, name in ((0, "service"), (1, "programme")):
                if index in self.spi_detail_counts:
                    count = self.spi_detail_counts[index]
                    counts.append(f'{count} {name}{"s" if count != 1 else ""}')

            self.spi_status.setText(" · ".join(counts) or "No programme information")

            if detail_index == 1:
                date_text = data.get("epg_date") or self.epg_date_edit.date().toString("yyyy-MM-dd")
                count = len(data["detail_rows"])
                self.epg_count_label.setText(f'{count} programme{"s" if count != 1 else ""} · {date_text}')

            if self.spi_detail_tabs.currentIndex() == detail_index:
                self._render_spi_detail(detail_index)

        def on_spi_detail_tab_changed(self, index):
            self._render_spi_detail(index)

            if self._data_tab_active(0):
                self._request_spi_load()

        def on_spi_group_selection_changed(self, current, _previous):
            self.spi_group_request_id += 1
            self._set_table_rows(self.spi_group_programmes, [])
            self.spi_group_detail.clear()

            if not current.isValid() or self.status is None:
                return

            group = self.spi_groups_table.model().row_user_data(current.row())

            if not isinstance(group, dict):
                return

            key = group["id"], group["short_id"]

            if key != self.spi_group_selected_key:
                self.spi_group_programme_key = None

            self.spi_group_selected_key = key
            self.worker.invoke("load_spi_group", self.spi_view_epoch, self.spi_group_request_id, *key)

        def on_spi_group_loaded(self, data):
            if self.status is None or data["context"] != (self.tuner_path, self.status["frequency"]):
                return

            if data["view_epoch"] != self.spi_view_epoch or data["request_id"] != self.spi_group_request_id or data["group_key"] != self.spi_group_selected_key:
                return

            self.spi_group_programmes.setVisible(data["supported"])
            self.spi_group_detail.setVisible(data["supported"])
            self.spi_group_detail.clear()
            self._set_table_rows(self.spi_group_programmes, data["rows"], data["programmes"])
            target_row = 0

            for row, programme in enumerate(data["programmes"]):
                if epg_programme_key(programme) == self.spi_group_programme_key:
                    target_row = row
                    break

            if data["programmes"]:
                self.spi_group_programmes.selectRow(target_row)
            else:
                self.spi_group_programme_key = None

        def on_spi_group_programme_changed(self, current, _previous):
            if not current.isValid():
                self.spi_group_detail.clear()
                return

            model = self.spi_group_programmes.model()
            programme = model.row_user_data(current.row())

            if not isinstance(programme, dict):
                return

            self.spi_group_programme_key = epg_programme_key(programme)
            self.spi_group_detail.setPlainText(programme_detail_text(programme, model.rows[current.row()][4]))

        def on_epg_filter_changed(self, *_):
            self.epg_selected_key = None
            self.epg_detail.clear()
            self.spi_detail_rows.pop(1, None)
            self.spi_detail_user_data.pop(1, None)
            self._set_table_rows(self.spi_programmes_table, [])

            if self._data_tab_active(0) and self.spi_detail_tabs.currentIndex() == 1:
                self.epg_count_label.setText("Loading…")
                self._request_spi_load()

        def _update_epg_service_filter(self, services):
            current_service = self.epg_service_combo.currentData()
            self.epg_service_combo.blockSignals(True)
            self.epg_service_combo.clear()
            self.epg_service_combo.addItem("All services", -1)

            for raw_service in services:
                service = format_service(raw_service)
                label = service["label"] or service["short_label"] or display_sid(service["id"])
                self.epg_service_combo.addItem(f'{label} · {display_sid(service["id"])}', service["id"])

            index = self.epg_service_combo.findData(current_service)
            self.epg_service_combo.setCurrentIndex(index if index >= 0 else 0)
            self.epg_service_combo.blockSignals(False)

        def on_epg_selection_changed(self, current, _previous):
            if not current.isValid():
                self.epg_detail.clear()
                return

            model = self.spi_programmes_table.model()
            if not isinstance(model, FastTableModel):
                return

            programme = model.row_user_data(current.row())
            if not isinstance(programme, dict):
                self.epg_detail.clear()
                return

            self.epg_selected_key = epg_programme_key(programme)
            row = model.rows[current.row()] if current.row() < len(model.rows) else []
            service_name = row[3] if len(row) > 3 else display_sid(programme["service_id"], programme["data_service"])
            self.epg_detail.setPlainText(programme_detail_text(programme, service_name))

        def _render_spi_detail(self, index):
            rows = self.spi_detail_rows.get(index)

            if rows is None:
                return

            tables = [
                self.spi_services_table,
                self.spi_programmes_table,
                self.spi_groups_table,
                self.spi_objects_table,
                self.spi_assets_table,
            ]

            if index == 2:
                self.spi_group_request_id += 1
                self._set_table_rows(self.spi_group_programmes, [])
                self.spi_group_detail.clear()

            if 0 <= index < len(tables):
                self._set_table_rows(tables[index], rows, self.spi_detail_user_data.get(index))

            if index == 2:
                target_row = 0

                for row, group in enumerate(self.spi_detail_user_data.get(index, [])):
                    key = self.spi_group_selected_key
                    matches = key is not None and (group["id"] == key[0] if group["id"] and key[0] else group["short_id"] == key[1])

                    if matches:
                        target_row = row
                        break

                if rows:
                    self.spi_groups_table.selectRow(target_row)
                else:
                    self.spi_group_selected_key = None
                    self.spi_group_programme_key = None

            if index == 1:
                programmes = self.spi_detail_user_data.get(index, [])
                target_row = None

                if self.epg_selected_key is not None:
                    for row, programme in enumerate(programmes):
                        if epg_programme_key(programme) == self.epg_selected_key:
                            target_row = row
                            break

                if target_row is None:
                    for row, values in enumerate(rows):
                        if values and values[0] == "NOW":
                            target_row = row
                            break

                if target_row is not None:
                    self.spi_programmes_table.selectRow(target_row)
                    self.spi_programmes_table.scrollTo(self.spi_programmes_table.model().index(target_row, 0), QAbstractItemView.ScrollHint.PositionAtCenter)

        def _request_journaline_load(self):
            if self.status is not None and JOURNALINE_INTERFACE in self.available_interfaces:
                self.worker.invoke("load_journaline", self.journaline_view_epoch)

        def _reset_journaline_view(self):
            self.journaline_refresh_timer.stop()
            self.journaline_view_epoch += 1
            self.journaline_selected_object = None
            self.journaline_entry_point = None
            self.journaline_history = []
            self.journaline_history_position = -1
            self.journaline_navigation_index = None
            self._update_journaline_navigation()
            self.journaline_status.setText("No Journaline entries")
            self._set_table_rows(self.journaline_transport_table, [])
            self._update_transport_button(self.journaline_transport_table, self.journaline_open_button)
            self._set_table_rows(self.journaline_objects_table, [])
            self.journaline_detail.clear()

        def on_journaline_loaded(self, data):
            if data["path"] != self.tuner_path or data["view_epoch"] != self.journaline_view_epoch:
                return

            status = data["status"]
            self.journaline_entry_point = status["point_of_entry"] if status["configured"] else None
            count = status["object_count"]
            self.journaline_status.setText(f'{count} {"entry" if count == 1 else "entries"}' if count else "No Journaline entries")
            transport_rows = []
            for item in data["transports"]:
                transport_rows.append([
                    display_sid(item["service_id"], item["data_service"]),
                    item["component_id"],
                    item["transport"],
                    item["subchannel"] if item["subchannel"] != 0xFF else "-",
                    item["packet_address"] if item["packet_address"] != 0xFFFF else "-",
                    ", ".join(str(value) for value in item["application_types"]) or "-",
                ])
            self._set_transport_rows(self.journaline_transport_table, transport_rows, data["transports"], self.journaline_open_button)

            rows = []
            ids = []
            for item in data["index"]:
                rows.append([f'0x{item["id"]:04X}', item["type"], item["revision"], "Yes" if item["static"] else "No", item["title"] or "-"])
                ids.append(item["id"])
            selected_id = self._selected_table_data(self.journaline_objects_table)
            self._set_table_rows(self.journaline_objects_table, rows, ids)
            if selected_id in ids:
                self.journaline_objects_table.selectRow(ids.index(selected_id))
            if self.journaline_selected_object is not None and self.journaline_selected_object not in ids:
                self.journaline_selected_object = None
                self.journaline_request_id += 1
                self.journaline_history = []
                self.journaline_history_position = -1
                self.journaline_navigation_index = None
                self.journaline_detail.clear()
            self._update_journaline_navigation()

        def load_selected_journaline_object(self):
            object_id = self._selected_table_data(self.journaline_objects_table)

            if object_id is not None:
                self.open_journaline_object(object_id)

        def _update_journaline_navigation(self):
            self.journaline_back_button.setEnabled(self.journaline_history_position > 0)
            self.journaline_forward_button.setEnabled(0 <= self.journaline_history_position < len(self.journaline_history) - 1)
            self.journaline_home_button.setEnabled(self.journaline_entry_point is not None and self.journaline_entry_point in self.journaline_objects_table.model().user_data)

        def open_journaline_object(self, object_id, history_index=None):
            self.journaline_selected_object = object_id
            self.journaline_navigation_index = history_index
            self.journaline_request_id += 1
            self.worker.invoke("load_journaline_object", object_id, self.journaline_view_epoch, self.journaline_request_id)

        def open_journaline_home(self):
            if self.journaline_entry_point is not None:
                self.open_journaline_object(self.journaline_entry_point)

        def open_journaline_link(self, url):
            match = re.fullmatch(r"journaline:(\d+)", url.toString())
            if match and 0 <= int(match[1]) <= 0xFFFF:
                self.open_journaline_object(int(match[1]))

        def navigate_journaline_history(self, direction):
            position = self.journaline_history_position + direction
            if 0 <= position < len(self.journaline_history):
                self.open_journaline_object(self.journaline_history[position], position)

        def on_journaline_object_loaded(self, item):
            if item["path"] != self.tuner_path or item["view_epoch"] != self.journaline_view_epoch or item["id"] != self.journaline_selected_object:
                return
            if item["request_id"] != self.journaline_request_id:
                return
            if item.get("missing"):
                self.journaline_selected_object = self.journaline_history[self.journaline_history_position] if self.journaline_history_position >= 0 else None
                self.journaline_navigation_index = None
                return

            if self.journaline_navigation_index is not None:
                self.journaline_history_position = self.journaline_navigation_index
            elif self.journaline_history_position < 0 or self.journaline_history[self.journaline_history_position] != item["id"]:
                self.journaline_history = (self.journaline_history[:self.journaline_history_position + 1] + [item["id"]])[-MAX_JOURNALINE_HISTORY:]
                self.journaline_history_position = len(self.journaline_history) - 1
            self.journaline_navigation_index = None
            ids = self.journaline_objects_table.model().user_data
            if item["id"] in ids:
                self.journaline_objects_table.selectRow(ids.index(item["id"]))
            self._update_journaline_navigation()

            sections = [f'<h2>{escape(item["title"] or "Journaline")}</h2>', f'<p><b>ID:</b> 0x{item["id"]:04X}<br><b>Language:</b> {escape(item["language"] or "-")}</p>']
            if item["body"]:
                sections.append(f'<p>{escape(item["body"]).replace(chr(10), "<br>")}</p>')
            if item["links"]:
                sections.append("<h3>Links</h3><ul>" + "".join(f'<li><a href="journaline:{link["id"]}">0x{link["id"]:04X} {escape(link["text"])}</a></li>' for link in item["links"]) + "</ul>")
            if item["items"]:
                sections.append("<h3>List</h3><ul>" + "".join(f'<li>{escape(" · ".join(columns))}</li>' for columns in item["items"]) + "</ul>")
            if item["general_links"]:
                sections.append("<h3>External</h3><ul>" + "".join(f'<li>{escape(link["address"])} {escape(link["label"])}</li>' for link in item["general_links"]) + "</ul>")
            self.journaline_detail.setHtml("".join(sections))

        def _update_audio_statistics(self, statistics, codec):
            parts = [codec.upper()] if codec else []
            if statistics and codec:
                if statistics["sampling_rate"]:
                    parts.append(f'{statistics["sampling_rate"] / 1000:g} kHz')
                if statistics["channels"]:
                    parts.append("Mono" if statistics["channels"] == 1 else "Stereo")
                if statistics["sbr"]:
                    parts.append("SBR")
                self.audio_frames_value.setText(f'{statistics["frames_received"]:,} received · {statistics["frames_output"]:,} output · {statistics["frames_discarded"]:,} discarded')
                rs = f'{statistics["rs_corrected"]:,} corrected / {statistics["rs_uncorrectable"]:,} uncorrectable' if statistics["rs_available"] else "–"
                crc = f'{statistics["crc_errors"]:,}' if statistics["crc_available"] else "–"
                self.audio_errors_value.setText(f'RS {rs} · CRC {crc} · Sync {statistics["sync_losses"]:,}')
                self.audio_dropouts_value.setText(f'{statistics["dropouts"]:,}')

            self.audio_value.setText(" · ".join(parts) or "-")
            for value in (self.audio_frames_value, self.audio_errors_value, self.audio_dropouts_value):
                self.audio_form.setRowVisible(value, statistics is not None)
                if not codec:
                    value.setText("-")

        def _reset_tpeg_view(self):
            self.tpeg_refresh_timer.stop()
            self.tpeg_displayed_message = None
            self.tpeg_messages = []
            self.tpeg_status.setText("No TPEG frames")
            self.tpeg_services.clear()
            for table in (self.tpeg_transport_table, self.tpeg_messages_table, self.tpeg_information_table, self.tpeg_fields_table, self.tpeg_frames_table):
                self._set_table_rows(table, [])
            self.tpeg_message_raw.clear()
            self.tpeg_frame_raw.clear()
            self.tpeg_open_button.hide()
            self.tpeg_transport_table.hide()

        def on_tpeg_loaded(self, data):
            if "context" in data and (self.status is None or data["context"] != (self.tuner_path, self.status["frequency"])):
                return
            status = data["status"]
            count = status["frames_received"]
            summary = f'{count} frame{"s" if count != 1 else ""}' if count else "No TPEG frames"
            crc_errors = sum(status[name] for name in ("group_crc_errors", "transport_crc_errors", "directory_crc_errors", "component_crc_errors"))
            if crc_errors:
                summary += f' · CRC {crc_errors:,}'
            if status["continuity_errors"]:
                summary += f' · continuity {status["continuity_errors"]:,}'
            if status["malformed_frames"]:
                summary += f' · malformed frames {status["malformed_frames"]:,}'

            content_status = data.get("content_status", [0, 0, 0, 0])
            messages = data.get("messages", [])
            summary += f' · {len(messages):,} messages'
            if content_status[1] or content_status[2]:
                summary += f' · data CRC {content_status[1]:,} · malformed {content_status[2]:,}'
            if content_status[3]:
                summary += f' · unsupported {content_status[3]:,}'
            self.tpeg_status.setText(summary)
            self.tpeg_services.setText("Services: " + (", ".join(data["services"]) or "-"))
            transport_rows = []
            for item in data["transports"]:
                transport_rows.append([display_sid(item["service_id"], item["data_service"]), item["component_id"], item["subchannel"], item["packet_address"], item["fec_scheme"]])
            self._set_transport_rows(self.tpeg_transport_table, transport_rows, data["transports"], self.tpeg_open_button)
            self.tpeg_transport_table.setVisible(bool(transport_rows))

            frame_rows = []
            for frame in data["frames"]:
                components = ", ".join(f'{component["id"]:02X}:{len(component["data"])}' for component in frame["components"]) or "-"
                frame_rows.append([frame["sequence"], frame["type"], frame["service"], frame["encryption"], components, len(frame["data"])])
            selected_frame = self._selected_table_data(self.tpeg_frames_table)
            selection = self.tpeg_frames_table.selectionModel()
            selection.blockSignals(True)
            try:
                self._set_table_rows(self.tpeg_frames_table, frame_rows, data["frames"])
                if selected_frame:
                    row = next((index for index, frame in enumerate(data["frames"]) if frame["sequence"] == selected_frame["sequence"]), -1)
                    if row >= 0:
                        self.tpeg_frames_table.selectRow(row)
            finally:
                selection.blockSignals(False)
            self._show_tpeg_frame()
            information_rows = []
            for service, name, description, version, encoding, applications in data.get("information", []):
                for component, content, application, major, minor, count, encryption, component_description in applications:
                    information_rows.append([format_tpeg_service(service), name, component, content, tpeg_application_name(application), f"{major}.{minor}", count, encryption, component_description or description])
                if not applications:
                    information_rows.append([format_tpeg_service(service), name, "-", "-", "-", "-", "-", "-", description])
            self._set_table_rows(self.tpeg_information_table, information_rows)
            self.tpeg_messages = messages
            self._render_tpeg_messages()

        def _refresh_tpeg_expiry(self):
            if self._data_tab_active(2) and [tpeg_message_state(message) for message in self.tpeg_messages] != self.tpeg_expiry_states:
                self._render_tpeg_messages()

        def _render_tpeg_messages(self, *args):
            self.tpeg_expiry_states = [tpeg_message_state(message) for message in self.tpeg_messages]
            scroll = self.tpeg_messages_table.verticalScrollBar().value()
            selected = self._selected_table_data(self.tpeg_messages_table)
            selected_key = tpeg_message_key(selected) if selected else None
            application = self.tpeg_application_filter.currentData()
            search = self.tpeg_search.text().strip().casefold()
            messages = []
            for message in self.tpeg_messages:
                if application and message["application"] != application:
                    continue
                if search:
                    text = " ".join([message["service"], str(message["id"]), message["summary"]] + [" ".join(field) for field in message["fields"]])
                    if search not in text.casefold():
                        continue
                messages.append(message)
            messages.sort(key=lambda message: (-message["sequence"], message["id"]))
            rows = [[tpeg_application_name(message["application"]), message["service"], message["component"], message["id"], message["version"], tpeg_message_state(message), message["summary"], datetime.fromtimestamp(message["expiry"], timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")] for message in messages]
            self.tpeg_rendering = True
            self._set_table_rows(self.tpeg_messages_table, rows, messages)
            if messages:
                row = next((index for index, message in enumerate(messages) if tpeg_message_key(message) == selected_key), 0)
                self.tpeg_messages_table.selectRow(row)
            self.tpeg_rendering = False
            self.tpeg_messages_table.verticalScrollBar().setValue(scroll)
            self._show_tpeg_message()

        def _show_tpeg_message(self, *args):
            if self.tpeg_rendering:
                return
            message = self._selected_table_data(self.tpeg_messages_table)
            signature = (tpeg_message_key(message), message["data"], message["fields"], message["priority"], message["generation"], tpeg_message_state(message)) if message else None
            if signature == self.tpeg_displayed_message:
                return
            self.tpeg_displayed_message = signature
            fields = []
            if message:
                fields = [["Message", "Priority", message["priority"]], ["Message", "State", tpeg_message_state(message)]]
                for component, name, value in message["fields"]:
                    if name.endswith(" (UTC seconds)"):
                        name = name.removesuffix(" (UTC seconds)")
                        value = datetime.fromtimestamp(int(value), timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
                    fields.append([component, name, value])
                if message["generation"]:
                    fields.insert(0, ["Message", "Generated", datetime.fromtimestamp(message["generation"], timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")])
            self._set_table_rows(self.tpeg_fields_table, fields)
            self.tpeg_message_raw.setPlainText(fic_hex_dump(message["data"]) if message else "")

        def _show_tpeg_frame(self, *args):
            frame = self._selected_table_data(self.tpeg_frames_table)
            text = ""
            if frame:
                text = fic_hex_dump(bytes(frame["data"]))
                for component in frame["components"]:
                    text += f'\n\nSCID {component["id"]}:\n' + fic_hex_dump(bytes(component["data"]))
            if self.tpeg_frame_raw.toPlainText() != text:
                self.tpeg_frame_raw.setPlainText(text)

        def _update_transport_button(self, table, button):
            transport = self._selected_table_data(table)
            openable = bool(transport and transport.get("openable"))
            button.setVisible(openable)
            button.setEnabled(openable)

        def _set_transport_rows(self, table, rows, transports, button):
            selected = self._selected_table_data(table)
            self._set_table_rows(table, rows, transports)

            for row, transport in enumerate(transports):
                if transport == selected:
                    table.selectRow(row)
                    break
            else:
                for row, transport in enumerate(transports):
                    if transport.get("openable"):
                        table.selectRow(row)
                        break

            self._update_transport_button(table, button)

        def open_data_transport(self, table):
            transport = self._selected_table_data(table)

            if not transport or not transport.get("openable"):
                self.statusBar().showMessage("No usable packet transport selected.")
                return

            if table is self.journaline_transport_table:
                self._reset_journaline_view()

            self.worker.invoke("set_data_service", transport["service_id"], transport["data_service"], transport["component_id"], transport["application_type"])

        def closeEvent(self, event):
            self.closing = True

            if self.service_details_dialog is not None:
                self.service_details_dialog.close()

            for reply in list(self.logo_requests.values()):
                reply.abort()

            self.audio_auto_play.blockSignals(True)
            self.audio_auto_play.setChecked(False)
            self._stop_audio_process()

            if self.audio_process.state() != QProcess.ProcessState.NotRunning:
                self.audio_process.waitForFinished(1000)
                if self.audio_process.state() != QProcess.ProcessState.NotRunning:
                    self.audio_process.kill()
                    self.audio_process.waitForFinished(500)

            self.worker.stop_backend()
            self.worker.wait(1500)
            super().closeEvent(event)


def main():
    if not QT_AVAILABLE:
        print("Missing dependency: PyQt6. Install with: python -m pip install -r tools/requirements.txt")
        return 1

    app = QApplication(sys.argv)
    app.setApplicationName("DAB Daemon Control")
    window = DABWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
