# UTMS Radar Display Control

UTMS is a single-context radar display control application. Its language covers the first-stage radar-display baseline and the delivered second-stage login, video, recording, and system-monitoring capabilities, plus the delivered third-stage realtime-trajectory, history-replay, geofence, and target-alert capabilities.

## Language

### Product Scope

**First-stage MVP**:
The radar-display baseline covers UDP JSON intake, online/offline map display, current-frame tables, statistics, status, logging, simulation, tests, and source-based Windows delivery.
_Avoid_: Video phase, full system, installer release

**Second-stage capabilities**:
The delivered scope for one RTSP video stream, FFmpeg decoding, YOLO ONNX Runtime detection, video presentation, operator-controlled recording, startup login, and local system monitoring. Radar-video association is not part of this stage.
_Avoid_: First-stage video, radar-video association

**Third-stage capabilities**:
The delivered scope for short-lived realtime trajectories, persisted radar-history query and replay, and geofence-driven target alerts. These capabilities extend accepted radar frames without changing current-frame snapshot semantics.
_Avoid_: Video replay, radar-video association, multi-radar fusion

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

**Realtime trajectory**:
A bounded, short-lived polyline derived from positions sampled every five hundred milliseconds for one target's live map presentation. It is supplementary display state and does not alter the current-frame target set; it defaults to the selected target's latest 30 seconds and can be disabled, shortened, lengthened, or shown for all targets. It is a category-coloured, round-joined solid line whose display shares ten-point endpoints between fading segments; sample points are hidden by default. A target can resume its trajectory only after a brief absence; time gaps or implausible position jumps begin a new segment.
_Avoid_: Historical replay, persisted track, radar track ID

**History session**:
The persisted recording interval created by one successful UDP-listener start and closed when that listener stops. It owns recorded radar frames and their valid targets; live display receives every accepted frame while persistence samples the latest accepted full snapshot at an operator-selected frequency, defaulting to 2 FPS. A session still marked active after an application restart is retained and classified as abnormally ended.
_Avoid_: Video recording session, continuous archive

**History retention**:
The operator-configured period for which persisted history sessions and their data remain available. It defaults to seven days, supports one through thirty days, and removes expired data automatically; manual deletion is limited to explicitly confirmed sessions.
_Avoid_: Cloud backup, permanent archive, remote database

**History export**:
A CSV representation of the currently queried history result or one selected target's trajectory points. It contains only persisted radar-history fields and does not export raw complete frames as JSON.
_Avoid_: JSON frame export, video export, database backup

**History store**:
The local SQLite persistence owned by the history-recording path. It organizes history sessions, sampled accepted radar frames, and their valid targets for indexed replay and query. It also retains the selected history sampling frequency, retention period, geofences, and alert rules. A store failure degrades only persistence; it is retried in the background and never blocks live display or alert analysis.
_Avoid_: JSON archive, cloud database, remote database

**Replay mode**:
The explicit operator state in which map, table, and statistics show a selected historical radar frame while accepted live frames continue to be displayed only in the background data flow, recorded, and analysed for alerts. Historical frames are selected by a query scoped by time range, session, track ID, and target category. It supports frame stepping, time seeking, and scaled playback; a long data gap is explicitly reported and skipped rather than interpolated. Its map displays the selected target's trajectory from the query start through the current replay time. Returning to live mode immediately restores the latest live frame and clears replay-only trajectory state.
_Avoid_: Paused UDP, video playback, live mode

**Geofence**:
A persisted enabled or disabled GCJ-02 geographic boundary that defines an area to evaluate for target-alert rules. A geofence can be circular, rectangular, or polygonal; rectangles are non-rotated GCJ-02 boundaries defined by southwest and northeast corners, and polygons contain three to twenty non-self-intersecting vertices. Disabled geofences do not participate in new alert evaluation but retain their existing alert history.
_Avoid_: Map layer, target category

**Alert rule**:
A persisted enabled or disabled condition that evaluates accepted live targets against a scope, threshold, and geofence to create target alerts. The supported rule types are stable geofence entry, stable geofence exit, geofence dwell timeout, and geofence-contained speeding; its speeding threshold uses metres per second and its dwell threshold is between five seconds and twenty-four hours. Entry, exit, and speeding rules each have a configurable zero-to-sixty-second confirmation duration that defaults to one second; entry uses the boundary and confirmed exit requires a five-metre outward margin. Dwell rules do not add confirmation time beyond their dwell threshold.
_Avoid_: Per-frame UI notification, video detection

**Alert engine**:
The worker-side boundary that evaluates accepted live radar frames against persisted geofences and alert rules, then emits structured target alerts. It does not access the GUI and its failure cannot stop live display or history recording.
_Avoid_: Alert center widget, replay evaluator, UDP receiver

**Target alert**:
A durable record that one target satisfied one alert rule at a particular time, with an acknowledgement state. It is not re-evaluated or recreated during historical replay; replay only shows the persisted alert markers. A target's geofence state can survive a brief disappearance, but expiry of that state is not an exit alert. A continuous condition creates only one alert until it recovers, and repeated alerts for the same rule and target are constrained by a configurable cooldown that defaults to thirty seconds. Alerts are retained when acknowledged, and acknowledgements record the fixed demonstration operator `root` and an optional handling note. A severe alert has a one-time sound, brief map highlight, and non-modal operator notification. Selecting an alert locates its recorded position and selects its target only when that target remains visible.
_Avoid_: Alarm sound, toast notification, replay result

**Alert center**:
The operator-facing tab for reviewing and acknowledging persisted target alerts, and for entering geofence and alert-rule management.
_Avoid_: Blocking alert dialog, replay controller, map toolbar

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
The shared map surface boundary that presents either online or offline map mode while preserving the same radar frame, selection, center, zoom, highlight, realtime trajectory, replay trajectory, geofence, and alert-location state.
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
