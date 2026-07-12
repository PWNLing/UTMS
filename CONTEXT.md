# UTMS Radar Display Control

UTMS is a single-context radar display control application. Its language describes the first-stage MVP: receiving radar snapshot data over UDP and presenting the latest radar and target state on maps, tables, charts, and status surfaces.

## Language

### Product Scope

**First-stage MVP**:
The current product phase focused on radar display control, UDP JSON intake, online/offline map display, current-frame tables, statistics, status, logging, simulation, tests, and source-based Windows delivery.
_Avoid_: Video phase, full system, installer release

**Second-stage video capabilities**:
Future capabilities around RTSP video, FFmpeg, YOLO, ONNX Runtime, video decoding, and radar-video association. They are outside the first-stage MVP.
_Avoid_: First-stage video, placeholder implementation

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
The logging boundary for lifecycle, receive, validation, map, tile, and severe application events. Normal high-frequency frames are not log records.
_Avoid_: Debug console, frame recorder
