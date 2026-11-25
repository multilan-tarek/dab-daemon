# D-Bus data types

[API reference](dbus-api.md)

Fields are listed in D-Bus order; positions start at 0. Check presence and
validity flags before using the corresponding values. Empty strings and arrays
mean no data is available.

## Service

Signature: `(qssyybqsssasass)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `id` | `q` | Programme service ID; 0 if none selected |
| 1 | `label` | `s` | Label |
| 2 | `short_label` | `s` | Short label |
| 3 | `ecc` | `y` | Extended Country Code |
| 4 | `subchannel` | `y` | Audio subchannel ID |
| 5 | `dab_plus` | `b` | DAB+ AAC; false for MPEG Layer II |
| 6 | `bitrate` | `q` | Bitrate in kbit/s |
| 7 | `protection` | `s` | Protection |
| 8 | `language` | `s` | Language |
| 9 | `pty` | `s` | Programme type text |
| 10 | `announcement_support` | `as` | Supported announcement types |
| 11 | `announcements` | `as` | Currently signalled announcement types |
| 12 | `logo_path` | `s` | Absolute local image path; empty if unavailable |

Scan results have an empty `logo_path`.

## EnsembleEntry

Signature: `(suqyssa(qssyybqsssasass))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `channel` | `s` | Band III channel |
| 1 | `frequency` | `u` | Frequency in kHz |
| 2 | `ensemble_id` | `q` | Ensemble ID |
| 3 | `ecc` | `y` | Ensemble ECC |
| 4 | `label` | `s` | Ensemble name |
| 5 | `short_label` | `s` | Short name |
| 6 | `services` | `a(qssyybqsssasass)` | Last complete [Service](#service) list; empty if not yet known |

Station data is replaced only after complete reception. Stored service records
have an empty `logo_path`; the current `Service` and `Services` provide resolved logo paths.

## EnsembleStatus

Signature: `(qyyybbyqqynby)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `ensemble_id` | `q` | Ensemble ID |
| 1 | `service_count` | `y` | Service count |
| 2 | `services_found` | `y` | Services found |
| 3 | `change_flags` | `y` | Change flags |
| 4 | `alarm_supported` | `b` | Alarm supported |
| 5 | `alarm_active` | `b` | Alarm active |
| 6 | `occurrence_change` | `y` | Occurrence change |
| 7 | `reconfiguration_counter` | `q` | Reconfiguration counter |
| 8 | `cif_counter` | `q` | CIF counter |
| 9 | `ecc` | `y` | ECC |
| 10 | `local_time_offset` | `n` | UTC offset in minutes |
| 11 | `lto_unique` | `b` | Unique local time offset |
| 12 | `international_table_id` | `y` | International table ID |

## BandIIIChannel

Signature: `(su)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `channel` | `s` | Channel |
| 1 | `frequency` | `u` | Frequency in kHz |

## ServiceComponent

Signature: `(ubyssyqbybqysbyyyyyuqasasqbba(qsaybybbqbyay))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `service_id` | `u` | Service ID |
| 1 | `data_service` | `b` | Data service |
| 2 | `id` | `y` | SCIdS (component within the service) |
| 3 | `label` | `s` | Label |
| 4 | `short_label` | `s` | Short label |
| 5 | `subchannel` | `y` | Subchannel ID; 255 if unavailable |
| 6 | `component_id` | `q` | Transport component identifier |
| 7 | `data` | `b` | Data component indicator |
| 8 | `type` | `y` | Broadcast component type |
| 9 | `conditional_access` | `b` | Conditional access |
| 10 | `conditional_access_organization` | `q` | Conditional access organization |
| 11 | `fec_scheme` | `y` | FEC scheme |
| 12 | `language` | `s` | Language |
| 13 | `part_time` | `b` | Part time |
| 14 | `change_flags` | `y` | Change flags |
| 15 | `change_day` | `y` | Change day |
| 16 | `change_hour` | `y` | Change hour |
| 17 | `change_minute` | `y` | Change minute |
| 18 | `change_second` | `y` | Change second |
| 19 | `transfer_service_id` | `u` | Transfer service ID |
| 20 | `transfer_ensemble_id` | `q` | Transfer ensemble ID |
| 21 | `applications` | `as` | Applications |
| 22 | `application_labels` | `as` | Application labels |
| 23 | `packet_address` | `q` | Packet address; 65535 if unavailable |
| 24 | `data_groups` | `b` | Data groups |
| 25 | `conditional_access_organization_present` | `b` | Conditional access organization present |
| 26 | `user_applications` | `a(qsaybybbqbyay)` | [UserApplication](#userapplication) records |

`id` is SCIdS, passed as `component_id` to `SetDataService`. The separate
`component_id` field identifies the transport component. `data_service` selects
a programme or data-service ID; `data` describes the component transport.

## UserApplication

Signature: `(qsaybybbqbyay)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `type` | `q` | Application ID; see [Data reception](dbus-api.md#data-reception) |
| 1 | `name` | `s` | Name |
| 2 | `data` | `ay` | Raw signalled application bytes |
| 3 | `xpad` | `b` | Uses X-PAD |
| 4 | `xpad_type` | `y` | X-PAD type; 255 if unavailable |
| 5 | `conditional_access` | `b` | Conditional access |
| 6 | `conditional_access_organization_present` | `b` | Conditional access organization present |
| 7 | `conditional_access_organization` | `q` | Conditional access organization |
| 8 | `data_groups` | `b` | Data groups |
| 9 | `data_type` | `y` | Signalled data component type |
| 10 | `parameters` | `ay` | Application-specific parameter bytes |

## SignalStatus

Signature: `(bbbbyybubu)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `ofdm_lock` | `b` | OFDM lock |
| 1 | `fec_lock` | `b` | FEC lock |
| 2 | `fec_sync` | `b` | FEC sync |
| 3 | `signal_quality_valid` | `b` | Signal quality valid |
| 4 | `signal_quality` | `y` | Quality, 0–100 |
| 5 | `antenna_level` | `y` | Antenna level, 0–6 |
| 6 | `msc_cer_valid` | `b` | MSC CER available |
| 7 | `msc_cer` | `u` | MSC CER in 0.01% units |
| 8 | `fic_crc_error_rate_valid` | `b` | FIC CRC error rate valid |
| 9 | `fic_crc_error_rate` | `u` | FIC CRC error rate in 0.01% units |

Error rates are in 0.01% units: `207` means `2.07%`.

## DateTime

Signature: `(qyyyyyq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `year` | `q` | Year |
| 1 | `month` | `y` | Month |
| 2 | `day` | `y` | Day |
| 3 | `hour` | `y` | Hour |
| 4 | `minute` | `y` | Minute |
| 5 | `second` | `y` | Second |
| 6 | `milliseconds` | `q` | Milliseconds |

Broadcast time in UTC. All-zero fields mean no time has been received.

## DLP

Signature: `(sssb)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `title` | `s` | Title |
| 1 | `artist` | `s` | Artist |
| 2 | `album` | `s` | Album |
| 3 | `item_running` | `b` | Item running |

## AudioStatistics

Signature: `(uybttttttttbb)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `sampling_rate` | `u` | Sampling rate in Hz |
| 1 | `channels` | `y` | Channel configuration |
| 2 | `sbr` | `b` | SBR |
| 3 | `frames_received` | `t` | Frames received |
| 4 | `frames_output` | `t` | Frames passed to the audio callback |
| 5 | `frames_discarded` | `t` | Frames discarded |
| 6 | `rs_corrected` | `t` | Corrected RS symbols |
| 7 | `rs_uncorrectable` | `t` | Uncorrectable RS codewords |
| 8 | `crc_errors` | `t` | CRC errors |
| 9 | `sync_losses` | `t` | Sync losses |
| 10 | `dropouts` | `t` | Dropouts |
| 11 | `rs_available` | `b` | RS statistics available |
| 12 | `crc_available` | `b` | CRC statistics available |

Frames are AAC access units or MP2 frames. Counters describe decoder output,
including frames suppressed by muting. They reset with audio reception.

Consecutive failures or a gap of at least 500 ms count as one dropout until
output resumes. MP2 reports RS and CRC checks as unavailable.

## SlideShowItem

Signature: `(qssbbxbxyysssyb)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `transport_id` | `q` | Transport ID |
| 1 | `content_name` | `s` | Content name |
| 2 | `path` | `s` | Absolute local image path |
| 3 | `trigger_time_present` | `b` | Trigger time present |
| 4 | `trigger_now` | `b` | Trigger now |
| 5 | `trigger_time` | `x` | Trigger time, Unix seconds |
| 6 | `expire_time_present` | `b` | Expire time present |
| 7 | `expire_time` | `x` | Expiry time, Unix seconds |
| 8 | `category_id` | `y` | Category ID |
| 9 | `slide_id` | `y` | Slide ID |
| 10 | `category_title` | `s` | Category title |
| 11 | `click_through_url` | `s` | Click-through URL |
| 12 | `alternative_location_url` | `s` | Alternative location URL |
| 13 | `alert` | `y` | Alert |
| 14 | `active` | `b` | Currently displayed slide |

## Multimedia

Signature: `(ssssqq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `url` | `s` | URL |
| 1 | `path` | `s` | Path |
| 2 | `mime_type` | `s` | MIME type |
| 3 | `type` | `s` | Type |
| 4 | `width` | `q` | Width in pixels |
| 5 | `height` | `q` | Height in pixels |

`path` is set when the asset is available locally; otherwise use `url`.

## ServiceLogo

Signature: `(uyqa(ssssqq))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `service_id` | `u` | Service ID |
| 1 | `ecc` | `y` | ECC |
| 2 | `ensemble_id` | `q` | Ensemble ID |
| 3 | `multimedia` | `a(ssssqq)` | [Multimedia](#multimedia) records |

Match logos by service ID, ECC and ensemble ID.

## SPIAsset

Signature: `(qsss)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `transport_id` | `q` | Transport ID |
| 1 | `content_name` | `s` | Content name |
| 2 | `path` | `s` | Absolute local file path |
| 3 | `mime_type` | `s` | MIME type |

## SPIObject

Signature: `(quyqssiays)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `transport_id` | `q` | Transport ID |
| 1 | `body_size` | `u` | Body size in bytes |
| 2 | `content_type` | `y` | Content type |
| 3 | `content_subtype` | `q` | Content subtype |
| 4 | `content_name` | `s` | Content name |
| 5 | `mime_type` | `s` | MIME type |
| 6 | `compression_type` | `i` | Compression type; -1 if absent |
| 7 | `profile_subset` | `ay` | Profile subset |
| 8 | `decode_status` | `s` | Decode status |

## ServiceInformation

Signature: `(ubyyqssssssasa(ssssqq))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `service_id` | `u` | Service ID |
| 1 | `data_service` | `b` | Data service |
| 2 | `component_id` | `y` | SCIdS |
| 3 | `ecc` | `y` | ECC |
| 4 | `ensemble_id` | `q` | Ensemble ID |
| 5 | `short_name` | `s` | Short name |
| 6 | `medium_name` | `s` | Medium name |
| 7 | `long_name` | `s` | Long name |
| 8 | `short_description` | `s` | Short description |
| 9 | `long_description` | `s` | Long description |
| 10 | `language` | `s` | Language |
| 11 | `genres` | `as` | Genres |
| 12 | `multimedia` | `a(ssssqq)` | [Multimedia](#multimedia) records |

## Programme

Signature: `(ubysusssssssusuasa(ssssqq))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `service_id` | `u` | Service ID |
| 1 | `data_service` | `b` | Data service |
| 2 | `component_id` | `y` | SCIdS |
| 3 | `id` | `s` | ID |
| 4 | `short_id` | `u` | Short ID |
| 5 | `short_name` | `s` | Short name |
| 6 | `medium_name` | `s` | Medium name |
| 7 | `long_name` | `s` | Long name |
| 8 | `short_description` | `s` | Short description |
| 9 | `long_description` | `s` | Long description |
| 10 | `language` | `s` | Language |
| 11 | `start_time` | `s` | Broadcast date/time string |
| 12 | `duration` | `u` | Duration in seconds |
| 13 | `actual_time` | `s` | Actual date/time string; empty if unavailable |
| 14 | `actual_duration` | `u` | Actual duration in seconds |
| 15 | `genres` | `as` | Genres |
| 16 | `multimedia` | `a(ssssqq)` | [Multimedia](#multimedia) records |

## ProgrammeGroup

Signature: `(susssssasa(ssssqq))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `id` | `s` | ID |
| 1 | `short_id` | `u` | Short ID |
| 2 | `short_name` | `s` | Short name |
| 3 | `medium_name` | `s` | Medium name |
| 4 | `long_name` | `s` | Long name |
| 5 | `short_description` | `s` | Short description |
| 6 | `long_description` | `s` | Long description |
| 7 | `genres` | `as` | Genres |
| 8 | `multimedia` | `a(ssssqq)` | [Multimedia](#multimedia) records |

## JournalineStatus

Signature: `(byyqbyqq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `configured` | `b` | Configured |
| 1 | `version` | `y` | Version |
| 2 | `extended_header_length` | `y` | Extended header length |
| 3 | `point_of_entry` | `q` | Home page object ID |
| 4 | `toc_present` | `b` | TOC present |
| 5 | `toc_revision` | `y` | TOC revision |
| 6 | `object_count` | `q` | Signalled object count |
| 7 | `timeout_minutes` | `q` | Timeout in minutes |

`object_count` is the broadcast count. Use `GetJournalineIndex` for cached pages.

## JournalineIndexEntry

Signature: `(qybys)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `id` | `q` | Object ID for `GetJournalineObject` |
| 1 | `type` | `y` | Type |
| 2 | `static_content` | `b` | Static content |
| 3 | `revision` | `y` | Revision |
| 4 | `title` | `s` | Title |

## JournalineLink

Signature: `(qs)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `object_id` | `q` | Target Journaline object ID |
| 1 | `text` | `s` | Text |

## JournalineGeneralLink

Signature: `(yss)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `type` | `y` | Type |
| 1 | `address` | `s` | Address |
| 2 | `label` | `s` | Label |

## JournalineObject

Signature: `(qybyssa(qs)aasa(yss)sbx)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `id` | `q` | ID |
| 1 | `type` | `y` | Type |
| 2 | `static_content` | `b` | Static content |
| 3 | `revision` | `y` | Revision |
| 4 | `title` | `s` | Title |
| 5 | `body` | `s` | Body |
| 6 | `links` | `a(qs)` | [JournalineLink](#journalinelink) records |
| 7 | `items` | `aas` | Rows of text columns |
| 8 | `general_links` | `a(yss)` | [JournalineGeneralLink](#journalinegenerallink) records |
| 9 | `language` | `s` | Language |
| 10 | `expiry_present` | `b` | Expiry present |
| 11 | `expiry` | `x` | Expiry time, Unix seconds |

## TPEGStatus

Signature: `(ttttttttq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `frames_received` | `t` | Frames received |
| 1 | `groups_received` | `t` | Groups received |
| 2 | `group_crc_errors` | `t` | Group CRC errors |
| 3 | `transport_crc_errors` | `t` | Transport CRC errors |
| 4 | `directory_crc_errors` | `t` | Directory CRC errors |
| 5 | `component_crc_errors` | `t` | Component CRC errors |
| 6 | `continuity_errors` | `t` | Continuity errors |
| 7 | `malformed_frames` | `t` | Malformed frames |
| 8 | `service_count` | `q` | Service count |

`StopTPEG` keeps counters and cached frames. A new transport resets them.

## TPEGServiceIdentifier

Signature: `(yyy)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `a` | `y` | A |
| 1 | `b` | `y` | B |
| 2 | `c` | `y` | C |

## TPEGComponent

Signature: `(yqay)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `id` | `y` | Component ID |
| 1 | `header_crc` | `q` | Transmitted header CRC |
| 2 | `data` | `ay` | Raw component bytes |

## TPEGFrame

Signature: `(ty(yyy)ya(yqay)ay)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `sequence` | `t` | Reception sequence cursor |
| 1 | `type` | `y` | Transport frame type |
| 2 | `service` | `(yyy)` | [TPEGServiceIdentifier](#tpegserviceidentifier) |
| 3 | `encryption` | `y` | Signalled encryption indicator |
| 4 | `components` | `a(yqay)` | [TPEGComponent](#tpegcomponent) records |
| 5 | `data` | `ay` | Raw frame bytes |

Sequences restart when TPEG reception is reset. Clear the cursor on reset or tuner change.

## TPEGContentStatus

Signature: `(tttt)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `messages_received` | `t` | Successfully decoded messages, including repetitions |
| 1 | `data_crc_errors` | `t` | Failed SNI or application payload CRC checks |
| 2 | `malformed_components` | `t` | Invalid lengths, structures or field encodings |
| 3 | `unsupported_components` | `t` | Unmapped, encrypted or unsupported content |

## TPEGApplication

Signature: `(yyqyyuys)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `component_id` | `y` | SCID within the service |
| 1 | `content_id` | `y` | COID |
| 2 | `id` | `q` | AID; TEC is 5, TFP is 7 |
| 3 | `major_version` | `y` | Advertised application major version |
| 4 | `minor_version` | `y` | Advertised application minor version |
| 5 | `message_count` | `u` | Broadcast message count |
| 6 | `encryption` | `y` | Component encryption indicator; zero means clear |
| 7 | `description` | `s` | Component description |

Version and count fields stay zero until SNI data arrives.

## TPEGServiceInformation

Signature: `((yyy)ssyya(yyqyyuys))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `id` | `(yyy)` | TPEG service identifier |
| 1 | `name` | `s` | Service name |
| 2 | `description` | `s` | Service description |
| 3 | `table_version` | `y` | SNI table version |
| 4 | `character_encoding` | `y` | Advertised character-table identifier |
| 5 | `applications` | `a(yyqyyuys)` | [TPEGApplication](#tpegapplication) records |

## TPEGField

Signature: `(sss)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `component` | `s` | Component path; distinguishes sections and repeated causes |
| 1 | `name` | `s` | Field label, including units where applicable |
| 2 | `value` | `s` | Decoded value or hexadecimal unhandled attributes |

## TPEGMessage

Signature: `((yyy)yquyuubytsa(sss)ay)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `service` | `(yyy)` | TPEG service identifier |
| 1 | `component_id` | `y` | SCID |
| 2 | `application_id` | `q` | AID |
| 3 | `id` | `u` | Message ID |
| 4 | `version` | `y` | Message version |
| 5 | `expiry` | `u` | Expiry as Unix seconds in UTC |
| 6 | `generation_time` | `u` | Generation time as Unix seconds in UTC; zero if absent |
| 7 | `cancelled` | `b` | Explicit cancellation |
| 8 | `priority` | `y` | Message priority, or component group priority if absent |
| 9 | `sequence` | `t` | Transport frame sequence containing this version |
| 10 | `summary` | `s` | Traffic effect/cause or flow-method summary |
| 11 | `fields` | `a(sss)` | [TPEGField](#tpegfield) records |
| 12 | `data` | `ay` | Individual application message bytes, including its component header |

A message is active while it is not canceled and `expiry` is in the future.
Sequences restart when TPEG reception is reset.

## EmergencyWarningLocation

Signature: `(yybayq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `nff` | `y` | NFF |
| 1 | `zone` | `y` | Zone |
| 2 | `sub_codes` | `b` | Sub codes |
| 3 | `digits` | `ay` | Digits |
| 4 | `sub_code_mask` | `q` | Sub code mask |

## EmergencyWarning

Signature: `(bqysbybbsya(yybayq))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `other_ensemble` | `b` | Other ensemble |
| 1 | `ensemble_id` | `q` | Ensemble ID |
| 2 | `subchannel` | `y` | Subchannel |
| 3 | `phase` | `s` | Phase |
| 4 | `trigger_second_present` | `b` | Trigger second present |
| 5 | `trigger_second` | `y` | Trigger second |
| 6 | `status_present` | `b` | Status present |
| 7 | `last` | `b` | Last |
| 8 | `stage` | `s` | Stage |
| 9 | `incident_id` | `y` | Incident ID |
| 10 | `locations` | `a(yybayq)` | [EmergencyWarningLocation](#emergencywarninglocation) records |

## EmergencyWarningStatus

Signature: `(ba(bqysbybbsya(yybayq)))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `supported` | `b` | Warning support signaled; independent of active alerts |
| 1 | `alerts` | `a(bqysbybbsya(yybayq))` | [EmergencyWarning](#emergencywarning) records |

## FIDCEntry

Signature: `(bbyysay)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `d1` | `b` | D1 |
| 1 | `d2` | `b` | D2 |
| 2 | `component_id` | `y` | Transport component identifier (TCId) |
| 3 | `extension` | `y` | Extension |
| 4 | `type` | `s` | Type |
| 5 | `data` | `ay` | Received FIDC bytes |

## OEAnnouncementSupport

Signature: `(qasyq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `service_id` | `q` | Service ID |
| 1 | `announcements` | `as` | Announcements |
| 2 | `cluster_id` | `y` | Cluster ID |
| 3 | `ensemble_id` | `q` | Ensemble ID |

## OEAnnouncementSwitch

Signature: `(yasbyq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `cluster_id` | `y` | Cluster ID |
| 1 | `announcements` | `as` | Announcements |
| 2 | `newly_introduced` | `b` | Newly introduced |
| 3 | `component_id` | `y` | Component ID |
| 4 | `service_id` | `q` | Service ID |

## FMAnnouncementSupport

Signature: `(qaq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `service_id` | `q` | Service ID |
| 1 | `pi_codes` | `aq` | PI codes |

## FMAnnouncementSwitch

Signature: `(ybyq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `cluster_id` | `y` | Cluster ID |
| 1 | `newly_introduced` | `b` | Newly introduced |
| 2 | `region_id` | `y` | Region ID |
| 3 | `pi_code` | `q` | PI code |

## ServiceLink

Signature: `(qbbbbsuy)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `linkage_set` | `q` | Linkage set |
| 1 | `active` | `b` | Active |
| 2 | `hard` | `b` | Hard |
| 3 | `shared` | `b` | Shared |
| 4 | `international` | `b` | International |
| 5 | `type` | `s` | Type |
| 6 | `id` | `u` | ID |
| 7 | `ecc` | `y` | ECC |

Entries with the same linkage set and scope belong together. `type` selects
DAB, FM-RDS, DRM or DRM/AMSS. International DAB IDs also use `ecc`.
Service links do not trigger automatic tuning.

## OtherEnsembleService

Signature: `(ubyaq)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `service_id` | `u` | Service ID |
| 1 | `data_service` | `b` | Data service |
| 2 | `conditional_access_id` | `y` | Conditional access ID |
| 3 | `ensemble_ids` | `aq` | Ensemble IDs |

## FrequencyInformation

Signature: `(bqsbuy)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `other_ensemble` | `b` | Other ensemble |
| 1 | `id` | `q` | ID |
| 2 | `type` | `s` | Type |
| 3 | `continuous` | `b` | Continuous output signaled |
| 4 | `frequency` | `u` | Frequency in kHz |
| 5 | `control` | `y` | Control |

`id` identifies an ensemble or service according to `type`. For DAB, `control`
contains continuity/area flags; DRM/AMSS uses it for the high ID byte.

## ScanProgress

Signature: `(qqsu)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `current` | `q` | Channels checked; 1-based while receiving |
| 1 | `total` | `q` | Total channels |
| 2 | `channel` | `s` | Channel |
| 3 | `frequency` | `u` | Frequency in kHz |

## ScanResult

Signature: `(suqyssa(qssyybqsssasass))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `channel` | `s` | Channel |
| 1 | `frequency` | `u` | Frequency in kHz |
| 2 | `ensemble_id` | `q` | Ensemble ID |
| 3 | `ecc` | `y` | ECC |
| 4 | `label` | `s` | Label |
| 5 | `short_label` | `s` | Short label |
| 6 | `services` | `a(qssyybqsssasass)` | [Service](#service) records |

Fast scans may return an empty service list.

## FICFigure

Signature: `(txynyssaya(ss))`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `sequence` | `t` | Sequence |
| 1 | `received` | `x` | Reception time, Unix milliseconds |
| 2 | `type` | `y` | FIG type; 255 for a FIB |
| 3 | `extension` | `n` | FIG extension; -1 if not applicable |
| 4 | `flags` | `y` | FIG header flags |
| 5 | `name` | `s` | Content description |
| 6 | `status` | `s` | Decoded, Raw, Unsupported, Disabled, Malformed, CRC error or Incomplete |
| 7 | `data` | `ay` | FIG header and payload, or raw FIB bytes |
| 8 | `fields` | `a(ss)` | Ordered name/value pairs |

FIG 0 flags: C/N in bit 7, OE in bit 6, P/D in bit 5.

## HardwareStatus

Signature: `(isxttxttsb)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `last_usb_error` | `i` | libusb error code; 0 if none recorded |
| 1 | `last_usb_error_name` | `s` | Last USB error name |
| 2 | `last_usb_error_time` | `x` | Time of latest USB error (Unix ms) |
| 3 | `usb_errors` | `t` | USB errors |
| 4 | `usb_timeouts` | `t` | USB timeouts |
| 5 | `last_data_time` | `x` | Latest FIC/MSC reception time (Unix ms) |
| 6 | `recovery_attempts` | `t` | Recovery attempts |
| 7 | `recovery_successes` | `t` | Recovery successes |
| 8 | `recovery_reason` | `s` | Reason for the latest recovery |
| 9 | `recovering` | `b` | Recovering |

Timestamps are Unix milliseconds; zero means unavailable. Counters survive
retuning and reset when the tuner is replaced.

## FICStatistics

Signature: `(uuuuauauau)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `fibs_received` | `u` | FIBs received |
| 1 | `fibs_valid` | `u` | Valid FIBs |
| 2 | `fibs_crc_errors` | `u` | FIB CRC errors |
| 3 | `figs_received` | `u` | FIGs received |
| 4 | `fig_types` | `au` | Counts by FIG type; 8 entries |
| 5 | `fig0_extensions` | `au` | Counts by FIG 0 extension; 32 entries |
| 6 | `fig5_extensions` | `au` | Counts by FIG 5 extension; 8 entries |

No change notifications. Poll this property if needed.

## SlideShowStatistics

Signature: `(ttttttquubbtb)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `groups_received` | `t` | Groups received |
| 1 | `groups_accepted` | `t` | Groups accepted |
| 2 | `group_crc_errors` | `t` | Group CRC errors |
| 3 | `segments` | `t` | Segments |
| 4 | `objects` | `t` | Objects |
| 5 | `header_parses` | `t` | Header parses |
| 6 | `transport_id` | `q` | Transport ID |
| 7 | `received_segments` | `u` | Received segments of the tracked image |
| 8 | `total_segments` | `u` | Total segments of the tracked image |
| 9 | `total_segments_known` | `b` | Total segments known |
| 10 | `first_image_received` | `b` | First image received |
| 11 | `first_image_ms` | `t` | Time to first image in ms; valid when `first_image_received` |
| 12 | `cache_hit` | `b` | Reused a cached slide |

## DataPathStatistics

Signature: `(tttttttttttttttt)`

| Position | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | `msc_bytes` | `t` | MSC bytes received |
| 1 | `packets_valid` | `t` | Packets valid |
| 2 | `packet_crc_errors` | `t` | Packet CRC errors |
| 3 | `packets_addressed` | `t` | Packets addressed |
| 4 | `data_groups` | `t` | Data groups |
| 5 | `packet_continuity_errors` | `t` | Packet continuity errors |
| 6 | `fec_frames` | `t` | FEC frames |
| 7 | `fec_errors` | `t` | FEC errors |
| 8 | `mot_groups_received` | `t` | MOT groups received |
| 9 | `mot_groups_accepted` | `t` | MOT groups accepted |
| 10 | `mot_crc_errors` | `t` | MOT CRC errors |
| 11 | `mot_segments` | `t` | MOT segments |
| 12 | `mot_directories` | `t` | MOT directories |
| 13 | `mot_objects` | `t` | MOT objects |
| 14 | `spi_objects` | `t` | SPI objects |
| 15 | `spi_updates` | `t` | SPI updates |

Counters describe the current packet-data transport and reset when it changes.
