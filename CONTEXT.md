# UTMS Radar Display Control

UTMS is a single-context radar display control application. Its language covers the first-stage radar-display baseline and the delivered second-stage login, video, recording, and system-monitoring capabilities.

## Language

### Product Scope

**First-stage MVP**:
The radar-display baseline covers UDP JSON intake, online/offline map display, current-frame tables, statistics, status, logging, simulation, tests, and source-based Windows delivery.
_Avoid_: Video phase, full system, installer release

**Second-stage capabilities**:
The delivered scope for one RTSP video stream, FFmpeg decoding, YOLO ONNX Runtime detection, video presentation, operator-controlled recording, startup login, and local system monitoring. Radar-video association is not part of this stage.
_Avoid_: First-stage video, radar-video association

**Login gate**:
The mandatory startup credential check that must succeed before an operator can enter the main application. It is a fixed demonstration login and does not represent persistent identity, registration, roles, or authorization.
_Avoid_: User management, account system, permission system

**Host system metrics**:
The local computer's aggregate CPU utilization and physical-memory usage, presented independently of UTMS itself.
_Avoid_: Per-core metrics, disk monitoring, network monitoring, GPU monitoring, hardware telemetry

**UTMS process metrics**:
The CPU utilization and memory usage attributable to the running UTMS process, presented alongside host system metrics for stability observation.
_Avoid_: Host system metrics, process manager

**System monitoring session**:
An operator-controlled period in which UTMS samples and displays host and UTMS process metrics. It is disabled by default, can be started and stopped from the system-monitoring tab, and shows no stale metrics after it stops.
_Avoid_: Always-on telemetry, background service

### Video Data

**Video detection**:
A YOLO result from a decoded video frame, shown as a bounding box with a category and confidence. It is not a radar target, has no track ID, and is not included in radar tables or statistics.
_Avoid_: Target, radar target, track

**Video detection category**:
The displayed category of a video detection. `person`, `bicycle`, `car`, and `truck` are shown as 行人、自行车、汽车、卡车; every other YOLO class is shown as 未知.
_Avoid_: Radar target type, full COCO category

**Video decoder**:
The worker-side boundary that maintains the RTSP connection and turns the stream into video frames. It is separate from UI rendering and detection inference.
_Avoid_: Video widget, inference worker

**Video inference worker**:
The worker-side boundary that turns the latest decoded frame into video detections using the configured YOLO model. It is separate from RTSP decoding and UI rendering.
_Avoid_: Video decoder, UI detector

**RTSP video recording**:
An operator-controlled capture of the current RTSP source's original encoded video track into one local MP4 file. It excludes audio, video detections, the UTMS interface, screen content, and local playback management.
_Avoid_: Screen recording, annotated recording, transcoded recording, recording library

**Recording session**:
One requested RTSP video recording that becomes active only when the next video keyframe can be captured. Its duration begins when recording becomes active; it fails rather than remaining in preparation for more than 10 seconds.
_Avoid_: Continuous archive, scheduled recording

### Radar Data

**Radar frame**:
A complete accepted snapshot of radar data represented in the application as the current display state. It may contain receive time, sender timestamp, sequence number, radar position, valid targets, and current-frame statistics.
_Avoid_: Accumulated frame, historical frame

**Current-frame snapshot**:
The rule that each accepted UDP JSON message replaces the previous target set with its own `tracks` content. Missing targets disappear immediately and no historical track points or trajectory lines are retained.
_Avoid_: Incremental update, target timeout, track history

**Target**:
A displayed object from a valid entry in `tracks`. A target must have a track ID and a valid latitude/longitude pair.
_Avoid_: Detection, object, point

**Track ID**:
The target identity key carried as `track_id` within a frame. If the same ID appears more than once in one frame, the later target record is the one that remains.
_Avoid_: Object ID, target number

**Radar position**:
The radar's own GCJ-02 position from `ego_position`. It is displayed as the radar point and can drive first automatic map centering, but it is not a target and is not counted in target statistics.
_Avoid_: Ego target, ownship target

**First appearance time**:
The local receive time when a target first appears in the current continuous presence run. It resets when the target disappears from a frame and later appears again.
_Avoid_: Sender timestamp, track timestamp

**Sequence baseline**:
The last accepted sequence reference used to reject duplicate and out-of-order frames. It resets after listener restart, accepted-data silence, or a detectable sender restart.
_Avoid_: Global sequence, permanent sequence

### Coordinates And Maps

**GCJ-02 coordinate**:
The only coordinate system used by radar position, target position, online Amap, and offline Amap tiles in the first-stage MVP. The application does not convert from WGS-84, BD-09, or other systems.
_Avoid_: WGS-84, BD-09, converted coordinate

**Map panel**:
The shared map surface boundary that presents either online or offline map mode while preserving the same radar frame, selection, center, zoom, and highlight state.
_Avoid_: Online map state, offline map state

**Online map**:
The Amap JavaScript map rendered through Qt WebEngine from a local HTML page. C++ owns business state; JavaScript owns map rendering and target click callbacks.
_Avoid_: Web business state, external browser map

**Offline map**:
The local Amap tile map rendered with Qt graphics classes from `data/map/amap/{z}/{x}/{y}.png`. It supports street tiles only in the first-stage MVP.
_Avoid_: Offline satellite map, custom tile source

**Selected target**:
The single current target selected through either the map or table. Selection is shared between map modes and table views and is cleared when the target disappears.
_Avoid_: Multi-selection, followed target

### Display And Status

**Track table**:
The tabular view of all valid targets in the latest accepted frame, with filtering, sorting, and map linkage. It is current-frame only and does not store historical rows.
_Avoid_: Track history table, target log

**Target statistics**:
Counts and chart data derived only from the latest accepted frame. The bicycle category belongs to the vehicle group in the pie chart.
_Avoid_: Historical statistics, cumulative totals

**UDP status**:
The single bottom-status indicator for listener and recent accepted-data state: red when stopped or failed, yellow when started without recent accepted data, and green when recent accepted data exists.
_Avoid_: Radar online status, network health status

**Simulated frame rate**:
A display-only random FPS value in the required demo range. It is not calculated from actual UDP receive throughput.
_Avoid_: Measured frame rate, UDP receive rate

### Module Boundaries

**Main window**:
The top-level UI composition boundary. It organizes layout and connects modules, but it does not own UDP parsing, map rendering internals, statistics rules, or logging implementation.
_Avoid_: Application controller, all-in-one window

**UDP receiver**:
The worker-side boundary for UDP listening, socket lifecycle, receive flow, and sequence acceptance policy.
_Avoid_: Parser, UI updater

**Radar JSON parser**:
The boundary that converts UDP JSON payloads into domain data and applies field validation, target filtering, type normalization, and frame-level rejection rules.
_Avoid_: UDP receiver, table adapter

**Logger**:
The logging boundary for lifecycle, receive, validation, map, tile, video connection, recording, monitoring, and severe application events. Passwords, normal video frames, normal recording packets, normal detections, and successful per-second metric samples are not log records.
_Avoid_: Debug console, frame recorder, credential store
