# D-Bus API

| Item | Value |
| --- | --- |
| Bus | User/session bus |
| Service | `de.multilan.dabdaemon` |
| Manager | `/de/multilan/dabdaemon` |
| Tuner | `/de/multilan/dabdaemon/tuner<id>` |

All application properties are read-only. Optional interfaces depend on the
[build flags](building.md#feature-flags).

Interface names below use the prefix `de.multilan.dabdaemon.`.\
Struct signatures and field order are listed in [Data types](dbus-types.md).

## Manager1

| Property | Type | Meaning |
| --- | --- | --- |
| `Version` | `s` | Daemon version; currently `1.0`, constant |
| `Tuners` | `ao` | Available tuner paths |

The manager implements `org.freedesktop.DBus.ObjectManager`. Use `Tuners` or
`GetManagedObjects` to find receivers.

## Updates

- `PropertiesChanged`: Property updates
- `InterfacesAdded` / `InterfacesRemoved`: Tuner arrival and removal
- `NameOwnerChanged`: Daemon start and stop

Clear cached state when the daemon or tuner is replaced. A reconnected receiver
may reuse its old path.

`AudioPipe`, `BandIIIChannels`, `ScanModes` and manager `Version` are constant.
Poll `FICStatistics`; it does not emit updates. Revision properties indicate
changes to cached content.

## Commands

Control methods return `success: b`. `false` means the request failed or was
rejected. Property reads remain available while tuning.

Scan and seek methods return before reception finishes. Metadata and images
arrive through later property updates.

## Example

List receivers and inspect a tuner:

```sh
busctl --user get-property de.multilan.dabdaemon /de/multilan/dabdaemon \
  de.multilan.dabdaemon.Manager1 Tuners
busctl --user introspect de.multilan.dabdaemon /de/multilan/dabdaemon/tuner0
```

Tune `5C`, wait for `Services`, then select Dlf (`0xD210`):

```sh
busctl --user call de.multilan.dabdaemon /de/multilan/dabdaemon/tuner0 \
  de.multilan.dabdaemon.Tuner1 SetEnsemble s 5C
busctl --user get-property de.multilan.dabdaemon /de/multilan/dabdaemon/tuner0 \
  de.multilan.dabdaemon.Tuner1 Services
busctl --user call de.multilan.dabdaemon /de/multilan/dabdaemon/tuner0 \
  de.multilan.dabdaemon.Tuner1 SetService q 0xD210
busctl --user get-property de.multilan.dabdaemon /de/multilan/dabdaemon/tuner0 \
  de.multilan.dabdaemon.Audio1 AudioPipe
```

Watch updates:

```sh
busctl --user monitor de.multilan.dabdaemon
```

## Tuner1

Always present. `DataApplication` and `SetDataService` require SPI, Journaline or TPEG.

| Property | Type | Meaning |
| --- | --- | --- |
| `BandIIIChannels` | [BandIIIChannel[]](dbus-types.md#bandiiichannel) | Supported channel names and frequencies in kHz. |
| `DataApplication` | `s` | Active data application: SPI, Journaline, TPEG, or an empty string. |
| `Ensemble` | `as` | Three strings in order: channel, ensemble label, short label. |
| `EnsembleStatus` | [EnsembleStatus](dbus-types.md#ensemblestatus) | Ensemble identity, service counts, alarms, time offset, and change signalling. |
| `Frequency` | `u` | Tuned frequency in kHz. |
| `Service` | [Service](dbus-types.md#service) | Selected radio station; ID zero means no radio station is selected. |
| `ServiceComponents` | [ServiceComponent[]](dbus-types.md#servicecomponent) | Programme/data components, packet parameters, and user applications. |
| `Services` | [Service[]](dbus-types.md#service) | Programme services in the current ensemble, including local logo paths. |
| `State` | `s` | Initializing, Error, Disconnected, Idle, Tuned, Playing, Receiving, or Scanning. |

| Method | Arguments | Returns | Action |
| --- | --- | --- | --- |
| `CloseService` | — | `success: b` | Close the current main service. Automatic SPI reception may continue. |
| `SetDataService` | `service_id: u`, `data_service: b`, `component_id: y`, `application_type: q` | `success: b` | Select a packet component and application; see [Data reception](#data-reception). |
| `SetEnsemble` | `ensemble: s` | `success: b` | Tune a Band III channel name, for example 5C. |
| `SetFrequency` | `frequency_khz: u` | `success: b` | Tune a frequency in kHz; clears the current station selection. |
| `SetService` | `service_id: q` | `success: b` | Select a programme service ID from Services on the tuned ensemble. |

## Signal1

Always present.

| Property | Type | Meaning |
| --- | --- | --- |
| `SignalStatus` | [SignalStatus](dbus-types.md#signalstatus) | Locks, quality, antenna level, and valid/error-rate measurements. |

## Metadata1

Always present.

| Property | Type | Meaning |
| --- | --- | --- |
| `DLP` | [DLP](dbus-types.md#dlp) | DL+ title, artist, album, and item-running status. |
| `DLS` | `s` | Current radio text. |
| `DateTime` | [DateTime](dbus-types.md#datetime) | Broadcast DAB time in UTC. |

## Audio1

Requires AAC or MP2. `AudioStatistics` also requires diagnostics.

| Property | Type | Meaning |
| --- | --- | --- |
| `AudioCodec` | `s` | `aac`, `mp2` or empty. |
| `AudioPipe` | `s` | Absolute local FIFO path; constant for this tuner object. |
| `AudioStatistics` | [AudioStatistics](dbus-types.md#audiostatistics) | Format, frames, RS/CRC/sync errors, and interruptions; requires diagnostics. |

`AudioPipe` contains AAC in LOAS/LATM or MPEG Layer II frames, depending on
`AudioCodec`. Use one FIFO reader at a time. File and image paths are local to
the daemon.

Audio is muted during tuning, USB errors and poor reception. It resumes after
300 ms of stable reception. Switching stations also clears queued FIFO data.
Clients must discard their own playback buffers when switching.

## Slideshow1

Requires SlideShow.

| Property | Type | Meaning |
| --- | --- | --- |
| `SlideShow` | `s` | Absolute path to the active image, or an empty string. |
| `SlideShowItems` | [SlideShowItem[]](dbus-types.md#slideshowitem) | Received slides and their display metadata. |

## SPI1

Requires SPI.

| Property | Type | Meaning |
| --- | --- | --- |
| `SPIAssets` | [SPIAsset[]](dbus-types.md#spiasset) | Received image/assets with local paths. |
| `SPIObjects` | [SPIObject[]](dbus-types.md#spiobject) | Received SPI object metadata and decode status. |
| `SPIRevision` | `t` | Revision for received SPI information and assets. |
| `ServiceLogos` | [ServiceLogo[]](dbus-types.md#servicelogo) | Station identity and available multimedia references. |

| Method | Arguments | Returns | Action |
| --- | --- | --- | --- |
| `GetProgramme` | `service_id: u`, `data_service: b`, `component_id: y`, `timestamp: x` | [Programme[]](dbus-types.md#programme) | Cached programme(s) covering timestamp (Unix seconds). |
| `GetProgrammeGroups` | — | [ProgrammeGroup[]](dbus-types.md#programmegroup) | Cached programme groups. |
| `GetGroupProgrammes` | `group_id: s`, `short_id: u` | [Programme[]](dbus-types.md#programme) | Cached programmes belonging to a group or its subgroups, across all dates. |
| `GetSchedule` | `service_id: u`, `data_service: b`, `component_id: y`, `from: x`, `to: x` | [Programme[]](dbus-types.md#programme) | Cached programmes overlapping [from, to), in Unix seconds. |
| `GetServiceInformation` | `service_id: u`, `data_service: b`, `component_id: y` | [ServiceInformation[]](dbus-types.md#serviceinformation) | Cached station information matching the supplied identity. |

## Journaline1

Requires Journaline.

| Property | Type | Meaning |
| --- | --- | --- |
| `JournalineRevision` | `t` | Revision for cached Journaline status/content. |
| `JournalineStatus` | [JournalineStatus](dbus-types.md#journalinestatus) | Configuration, point of entry, and signalled object count. |

| Method | Arguments | Returns | Action |
| --- | --- | --- | --- |
| `GetJournalineIndex` | — | [JournalineIndexEntry[]](dbus-types.md#journalineindexentry) | Cached Journaline page summaries. |
| `GetJournalineObject` | `object_id: q` | [JournalineObject[]](dbus-types.md#journalineobject) | Zero or one cached page; an empty array means not available. |

## TPEG1

Requires TPEG.

Decodes SNI, TEC 3.x, TFP 1.x and TMC location references. Other content remains
available through `GetTPEGFrames`.

New message versions replace older ones. Canceled and expired messages remain
cached. `StopTPEG` keeps the cache; changing the ensemble or transport clears it.

| Property | Type | Meaning |
| --- | --- | --- |
| `TPEGReceiving` | `b` | Whether a TPEG transport is currently selected. |
| `TPEGRevision` | `t` | Revision for TPEG directory, frame and decoded-content updates. |
| `TPEGStatus` | [TPEGStatus](dbus-types.md#tpegstatus) | Reception, CRC, continuity, and malformed-frame counters. |

| Method | Arguments | Returns | Action |
| --- | --- | --- | --- |
| `GetTPEGFrames` | `since_sequence: t`, `limit: q` | [TPEGFrame[]](dbus-types.md#tpegframe) | Frames strictly after since_sequence; limit 0 uses 32, maximum 128. |
| `GetTPEGServices` | — | [TPEGServiceIdentifier[]](dbus-types.md#tpegserviceidentifier) | Service identifiers received in TPEG directory data. |
| `GetTPEGServiceInformation` | — | [TPEGServiceInformation[]](dbus-types.md#tpegserviceinformation) | SNI names, descriptions and application mappings. |
| `GetTPEGMessages` | — | [TPEGMessage[]](dbus-types.md#tpegmessage) | Latest received version of each decoded message, including cancellations and expired entries. |
| `GetTPEGContentStatus` | — | [TPEGContentStatus](dbus-types.md#tpegcontentstatus) | Message, payload CRC, malformed-content and unsupported-content counters. |
| `StopTPEG` | — | `success: b` | Stop TPEG only, retain messages and frames, and release its transport for SPI. |

## EWS1

Requires EWS.

| Property | Type | Meaning |
| --- | --- | --- |
| `EmergencyWarning` | [EmergencyWarningStatus](dbus-types.md#emergencywarningstatus) | Support status and currently signalled alerts. |

## FIDC1

Requires FIDC.

| Property | Type | Meaning |
| --- | --- | --- |
| `FIDC` | [FIDCEntry[]](dbus-types.md#fidcentry) | Received Fast Information Data Channel entries. |

## Announcements1

Requires announcements.

| Property | Type | Meaning |
| --- | --- | --- |
| `FMAnnouncementSupport` | [FMAnnouncementSupport[]](dbus-types.md#fmannouncementsupport) | Programme-service to FM/RDS PI references. |
| `FMAnnouncementSwitches` | [FMAnnouncementSwitch[]](dbus-types.md#fmannouncementswitch) | FM announcement switching references. |
| `OEAnnouncementSupport` | [OEAnnouncementSupport[]](dbus-types.md#oeannouncementsupport) | Other-ensemble announcement support. |
| `OEAnnouncementSwitches` | [OEAnnouncementSwitch[]](dbus-types.md#oeannouncementswitch) | Other-ensemble announcement switching references. |

## ServiceLinking1

Requires service linking.

| Property | Type | Meaning |
| --- | --- | --- |
| `OtherEnsembleServices` | [OtherEnsembleService[]](dbus-types.md#otherensembleservice) | Service references to alternative ensembles. |
| `ServiceLinks` | [ServiceLink[]](dbus-types.md#servicelink) | Signalled service linkage sets and identifiers. |

## FrequencyInfo1

Requires frequency information.

| Property | Type | Meaning |
| --- | --- | --- |
| `FrequencyInformation` | [FrequencyInformation[]](dbus-types.md#frequencyinformation) | Alternative DAB/FM/DRM/AMSS frequencies in kHz. |

## Scan1

Requires seek, fast scan or full scan. Seek methods require `DAB_ENABLE_SEEK`.

| Property | Type | Meaning |
| --- | --- | --- |
| `ScanModes` | `as` | Enabled modes: `fast`, `full`, `seek_up`, `seek_down`; constant. |
| `ScanMode` | `s` | Current or last mode; empty before the first scan or seek. |
| `ScanProgress` | [ScanProgress](dbus-types.md#scanprogress) | Channels checked, total channels and current frequency. |
| `ScanResults` | [ScanResult[]](dbus-types.md#scanresult) | Ensembles and the programme services collected so far. |
| `ScanState` | `s` | `Idle`, `Scanning`, `Stopping`, `Completed`, `Cancelled`, `NotFound` or `Failed`. |

| Method | Arguments | Returns | Action |
| --- | --- | --- | --- |
| `StartScan` | `mode: s` | `success: b` | Start fast or full scanning, if that mode is enabled. |
| `StopScan` | — | `success: b` | Request scan or seek cancellation. |
| `SeekUp` | — | `success: b` | Search upward from the next Band III channel and stay on the first received ensemble. |
| `SeekDown` | — | `success: b` | Search downward from the previous Band III channel and stay on the first received ensemble. |
| `StopSeek` | — | `success: b` | Cancel an active seek. |

`StartScan` and seek methods return when the request is accepted. Follow
`ScanState` and `ScanProgress` for the result.

- `fast`: Collect ensembles
- `full`: Collect ensembles and stations, including names and audio parameters
- `seek_up` / `seek_down`: Stop at the next synchronized ensemble; wrap at the band boundary

Scans return to the previous frequency and try to restore the station when finished or canceled.
Seek stays on the last tuned channel and keeps existing `ScanResults`.
Select a station with `SetService` after seeking.

`NotFound` means seek checked the whole band without a result. `Failed` means
tuning failed. Tuning and service selection are blocked while scanning or seeking.

## Diagnostics1

Requires diagnostics. Packet and SlideShow counters require the corresponding features.

| Property | Type | Meaning |
| --- | --- | --- |
| `DataPathStatistics` | [DataPathStatistics](dbus-types.md#datapathstatistics) | Active packet-data counters; requires SPI, Journaline, or TPEG. |
| `FICRevision` | `t` | Latest FIC history sequence; use GetFICFigures to fetch records. |
| `FICStatistics` | [FICStatistics](dbus-types.md#ficstatistics) | FIB/FIG counters; read periodically, as this property emits no updates. |
| `HardwareStatus` | [HardwareStatus](dbus-types.md#hardwarestatus) | USB errors, last data time, and recovery status. |
| `SlideShowStatistics` | [SlideShowStatistics](dbus-types.md#slideshowstatistics) | Image assembly and first-image timing; also requires SlideShow. |

| Method | Arguments | Returns | Action |
| --- | --- | --- | --- |
| `GetFICFigures` | `since_sequence: t`, `limit: q` | `generation: t`, `oldest_sequence: t`, `revision: t`, `figures: a(txynyssaya(ss))` | FIC history; see [History](#history). |

## Data reception

Use `ServiceComponents` to find a component and its `user_applications`.
Pass `service_id`, `data_service`, `id` (SCIdS) and the application's `type`
to `SetDataService`.

| Application | Type |
| --- | --- |
| SPI | `0x007` |
| TPEG | `0x004` |
| Journaline | `0x44A` |

X-PAD applications follow the selected audio service automatically.

During radio playback, packet TPEG uses the additional data channel and pauses
SPI. `StopTPEG` releases the channel and keeps received data. Packet Journaline
replaces radio playback. Without radio, packet data uses the main channel.

`StopTPEG` returns `false` when TPEG is inactive or a scan/seek is active.
It prevents automatic TPEG restart until explicitly selected or reception is reset.

## SPI queries

SPI queries return cached data. `component_id = 255` matches any SCIdS.
Query times are Unix seconds; `GetSchedule` uses `[from, to)`.

Pass a group's `id` and `short_id` from `GetProgrammeGroups` to
`GetGroupProgrammes`. Use an empty `group_id` for groups identified only by a
short ID. Zero is a valid short ID.

Group queries include subgroups and all dates. Referenced groups may appear
before their names and descriptions arrive.

## History

`GetFICFigures` returns `generation`, `oldest_sequence`, `revision` and `figures`.
Pass the last received sequence as `since_sequence` for the next page.

- History: 2,048 records; up to 512 per call
- `limit = 0`: Metadata only
- Changed `generation`: Clear the client's history
- `oldest_sequence > since_sequence + 1`: Older records were dropped

FIC sequences continue across reception resets. Clear the cursor when the tuner
is replaced.

`GetTPEGFrames` returns frames after `since_sequence`. History holds 128 frames;
`limit = 0` returns up to 32. Clear the cursor on a transport reset or tuner change.
`StopTPEG` keeps the history.
