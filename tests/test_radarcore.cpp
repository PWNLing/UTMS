#include <algorithm>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QtTest>

#include "core/Logger.h"
#include "core/RadarFrameStore.h"
#include "core/RadarJsonParser.h"
#include "map/AmapConfig.h"
#include "map/OnlineMapState.h"
#include "network/UdpReceiver.h"

namespace {

utms::TrackData makeTrack(qint64 track_id) {
    utms::TrackData track;
    track.track_id = track_id;
    track.type = utms::TargetType::kCar;
    track.position = utms::GeoPosition{25.31, 110.41};
    return track;
}

const utms::TrackData &findTrack(const utms::RadarFrame &frame, qint64 track_id) {
    const auto iterator = std::find_if(frame.tracks.cbegin(), frame.tracks.cend(),
                                       [track_id](const utms::TrackData &track) { return track.track_id == track_id; });
    Q_ASSERT(iterator != frame.tracks.cend());
    return *iterator;
}

QByteArray sequencePayload(std::optional<qint64> sequence, double timestamp) {
    const QString header =
        sequence.has_value() ? QStringLiteral(R"json("header":{"sequence":%1},)json").arg(sequence.value()) : QString();
    return QStringLiteral(R"json({%1"timestamp":%2,"tracks":[]})json").arg(header).arg(timestamp, 0, 'f', 3).toUtf8();
}

quint16 availableUdpPort() {
    QUdpSocket socket;
    const bool bound = socket.bind(QHostAddress(QHostAddress::LocalHost), static_cast<quint16>(0));
    if (!bound) {
        const QByteArray message =
            QStringLiteral("Failed to reserve a UDP test port: %1").arg(socket.errorString()).toUtf8();
        QTest::qFail(message.constData(), __FILE__, __LINE__);
        return 0;
    }
    return socket.localPort();
}

} // namespace

class RadarCoreTest : public QObject {
    Q_OBJECT

private slots:
    void parsesValidJson();
    void rejectsInvalidFrameStructures_data();
    void rejectsInvalidFrameStructures();
    void filtersInvalidTargetsAndReportsValidationWarnings();
    void validatesCoordinateBoundaries_data();
    void validatesCoordinateBoundaries();
    void preservesMissingOptionalMeasurementsWithoutWarnings();
    void overwritesDuplicateTracksAndReportsCountMismatch();
    void normalizesTargetTypes_data();
    void normalizesTargetTypes();
    void replacesCurrentFrameAndPreservesFirstSeen();
    void removesDisappearedTargets();
    void resetsFirstSeenAfterTargetDisappears();
    void rejectsDuplicateAndOutOfOrderSequences();
    void acceptsAndReportsSequenceJump();
    void resetsSequenceWhenListeningRestarts();
    void acceptsFrameAfterSenderRestart();
    void rejectsSmallOutOfOrderDropWithNewerTimestamp();
    void resetsSequenceBeforeAcceptingFrameWithoutSequence();
    void invalidFramesDoNotRefreshReceivingStatus();
    void rotatesRuntimeLogsWithinRetentionLimit();
    void loadsAmapConfiguration();
    void rejectsIncompleteAmapConfiguration();
    void computesIncrementalOnlineMapUpdates();
    void automaticallyCentersOnRadarOnlyOnce();
};

void RadarCoreTest::parsesValidJson() {
    const QByteArray payload = R"json({
        "timestamp": 1773824375.675,
        "header": {"sequence": 14760},
        "ego_position": {"latitude": 25.3100844, "longitude": 110.4111849},
        "target_count": 2,
        "tracks": [
            {
                "track_id": 2219,
                "type": " pedestrian ",
                "position": {"latitude": 25.3097296, "longitude": 110.4157190},
                "distance": 5.0,
                "velocity": 6.57
            },
            {
                "track_id": 2220,
                "type": "unrecognized",
                "position": {"latitude": 25.311, "longitude": 110.417}
            }
        ]
    })json";
    const QDateTime received_at = QDateTime::fromMSecsSinceEpoch(1'700'000'000'000);

    const utms::RadarParseResult result = utms::RadarJsonParser::parse(payload, received_at);

    QVERIFY2(result.frame.has_value(), qPrintable(result.error));
    QCOMPARE(result.frame->received_at, received_at);
    QCOMPARE(result.frame->sequence.value(), 14760);
    QVERIFY(result.frame->ego_position.has_value());
    QCOMPARE(result.frame->tracks.size(), 2);
    QCOMPARE(result.frame->tracks.at(0).track_id, 2219);
    QCOMPARE(result.frame->tracks.at(0).type, utms::TargetType::kPedestrian);
    QCOMPARE(result.frame->tracks.at(0).velocity_mps.value(), 6.57);
    QCOMPARE(result.frame->tracks.at(0).distance_m.value(), 5.0);
    QCOMPARE(result.frame->tracks.at(1).type, utms::TargetType::kUnknown);
    QVERIFY(!result.frame->tracks.at(1).velocity_mps.has_value());
    QVERIFY(!result.frame->tracks.at(1).distance_m.has_value());
}

void RadarCoreTest::rejectsInvalidFrameStructures_data() {
    QTest::addColumn<QByteArray>("payload");

    QTest::newRow("invalid-json") << QByteArray("{");
    QTest::newRow("array-root") << QByteArray("[]");
    QTest::newRow("missing-tracks") << QByteArray(R"json({"target_count":0})json");
    QTest::newRow("tracks-not-array") << QByteArray(R"json({"tracks":{}})json");
}

void RadarCoreTest::rejectsInvalidFrameStructures() {
    QFETCH(QByteArray, payload);

    const utms::RadarParseResult result = utms::RadarJsonParser::parse(payload);

    QVERIFY(!result.frame.has_value());
    QVERIFY(!result.error.isEmpty());
}

void RadarCoreTest::filtersInvalidTargetsAndReportsValidationWarnings() {
    const QByteArray payload = R"json({
        "ego_position":{"latitude":0,"longitude":0},
        "tracks":[
            {"type":"CAR","position":{"latitude":25.3,"longitude":110.4}},
            {"track_id":2,"position":{"latitude":91,"longitude":110.4}},
            {"track_id":3,"position":{"latitude":0,"longitude":0}},
            {"track_id":4,"position":{"latitude":25.3,"longitude":110.4},
             "velocity":"fast","distance":-1}
        ]
    })json";

    const utms::RadarParseResult result = utms::RadarJsonParser::parse(payload);

    QVERIFY2(result.frame.has_value(), qPrintable(result.error));
    QVERIFY(!result.frame->ego_position.has_value());
    QCOMPARE(result.frame->tracks.size(), 1);
    QCOMPARE(result.frame->tracks.constFirst().track_id, 4);
    QVERIFY(!result.frame->tracks.constFirst().velocity_mps.has_value());
    QVERIFY(!result.frame->tracks.constFirst().distance_m.has_value());
    QVERIFY(result.warnings.size() >= 6);
}

void RadarCoreTest::validatesCoordinateBoundaries_data() {
    QTest::addColumn<QString>("latitude");
    QTest::addColumn<QString>("longitude");
    QTest::addColumn<bool>("expected_valid");

    QTest::newRow("latitude-below-minimum") << QStringLiteral("-90.1") << QStringLiteral("110") << false;
    QTest::newRow("longitude-below-minimum") << QStringLiteral("25") << QStringLiteral("-180.1") << false;
    QTest::newRow("longitude-above-maximum") << QStringLiteral("25") << QStringLiteral("180.1") << false;
    QTest::newRow("non-finite-latitude") << QStringLiteral("\"1e309\"") << QStringLiteral("110") << false;
    QTest::newRow("lower-boundaries") << QStringLiteral("-90") << QStringLiteral("-180") << true;
    QTest::newRow("upper-boundaries") << QStringLiteral("90") << QStringLiteral("180") << true;
}

void RadarCoreTest::validatesCoordinateBoundaries() {
    QFETCH(QString, latitude);
    QFETCH(QString, longitude);
    QFETCH(bool, expected_valid);
    const QByteArray payload =
        QStringLiteral(R"json({"tracks":[{"track_id":1,"position":{"latitude":%1,"longitude":%2}}]})json")
            .arg(latitude, longitude)
            .toUtf8();

    const utms::RadarParseResult result = utms::RadarJsonParser::parse(payload);

    QVERIFY2(result.frame.has_value(), qPrintable(result.error));
    QCOMPARE(!result.frame->tracks.isEmpty(), expected_valid);
}

void RadarCoreTest::preservesMissingOptionalMeasurementsWithoutWarnings() {
    const QByteArray payload = R"json({
        "tracks":[
            {"track_id":9,"position":{"latitude":25.3,"longitude":110.4}}
        ]
    })json";

    const utms::RadarParseResult result = utms::RadarJsonParser::parse(payload);

    QVERIFY2(result.frame.has_value(), qPrintable(result.error));
    QCOMPARE(result.frame->tracks.size(), 1);
    QVERIFY(!result.frame->tracks.constFirst().velocity_mps.has_value());
    QVERIFY(!result.frame->tracks.constFirst().distance_m.has_value());
    QVERIFY(result.warnings.isEmpty());
}

void RadarCoreTest::overwritesDuplicateTracksAndReportsCountMismatch() {
    const QByteArray payload = R"json({
        "target_count":1,
        "tracks":[
            {"track_id":7,"type":"CAR","position":{"latitude":25.3,"longitude":110.4}},
            {"track_id":7,"type":"TRUCK","position":{"latitude":25.4,"longitude":110.5}}
        ]
    })json";

    const utms::RadarParseResult result = utms::RadarJsonParser::parse(payload);

    QVERIFY2(result.frame.has_value(), qPrintable(result.error));
    QCOMPARE(result.frame->tracks.size(), 1);
    QCOMPARE(result.frame->tracks.constFirst().type, utms::TargetType::kTruck);
    QCOMPARE(result.frame->tracks.constFirst().position.latitude, 25.4);
    QCOMPARE(result.warnings.size(), 2);
}

void RadarCoreTest::normalizesTargetTypes_data() {
    QTest::addColumn<QString>("protocol_type");
    QTest::addColumn<utms::TargetType>("expected_type");

    QTest::newRow("car") << QStringLiteral(" car ") << utms::TargetType::kCar;
    QTest::newRow("truck") << QStringLiteral("Truck") << utms::TargetType::kTruck;
    QTest::newRow("pedestrian") << QStringLiteral("PEDESTRIAN") << utms::TargetType::kPedestrian;
    QTest::newRow("bicycle") << QStringLiteral(" bicycle ") << utms::TargetType::kBicycle;
    QTest::newRow("unknown") << QStringLiteral("BUS") << utms::TargetType::kUnknown;
}

void RadarCoreTest::normalizesTargetTypes() {
    QFETCH(QString, protocol_type);
    QFETCH(utms::TargetType, expected_type);
    const QByteArray payload =
        QStringLiteral(
            R"json({"tracks":[{"track_id":1,"type":"%1","position":{"latitude":25.3,"longitude":110.4}}]})json")
            .arg(protocol_type)
            .toUtf8();

    const utms::RadarParseResult result = utms::RadarJsonParser::parse(payload);

    QVERIFY2(result.frame.has_value(), qPrintable(result.error));
    QCOMPARE(result.frame->tracks.constFirst().type, expected_type);
}

void RadarCoreTest::replacesCurrentFrameAndPreservesFirstSeen() {
    utms::RadarFrameStore store;
    const QDateTime first_time = QDateTime::fromMSecsSinceEpoch(1'000);
    const QDateTime second_time = QDateTime::fromMSecsSinceEpoch(2'000);

    utms::RadarFrame first_frame;
    first_frame.received_at = first_time;
    first_frame.tracks = {makeTrack(1), makeTrack(2)};
    store.replace(first_frame);

    utms::RadarFrame second_frame;
    second_frame.received_at = second_time;
    second_frame.tracks = {makeTrack(2), makeTrack(3)};
    const utms::RadarFrame current_frame = store.replace(second_frame);

    QCOMPARE(current_frame.tracks.size(), 2);
    QCOMPARE(findTrack(current_frame, 2).first_seen_at, first_time);
    QCOMPARE(findTrack(current_frame, 3).first_seen_at, second_time);
}

void RadarCoreTest::removesDisappearedTargets() {
    utms::RadarFrameStore store;
    utms::RadarFrame first_frame;
    first_frame.received_at = QDateTime::fromMSecsSinceEpoch(1'000);
    first_frame.tracks = {makeTrack(1), makeTrack(2)};
    store.replace(first_frame);

    utms::RadarFrame second_frame;
    second_frame.received_at = QDateTime::fromMSecsSinceEpoch(2'000);
    second_frame.tracks = {makeTrack(2)};

    const utms::RadarFrame current_frame = store.replace(second_frame);
    QCOMPARE(current_frame.tracks.size(), 1);
    QCOMPARE(current_frame.tracks.constFirst().track_id, 2);
}

void RadarCoreTest::resetsFirstSeenAfterTargetDisappears() {
    utms::RadarFrameStore store;
    utms::RadarFrame frame;
    frame.received_at = QDateTime::fromMSecsSinceEpoch(1'000);
    frame.tracks = {makeTrack(7)};
    store.replace(frame);

    frame.received_at = QDateTime::fromMSecsSinceEpoch(2'000);
    frame.tracks.clear();
    store.replace(frame);

    frame.received_at = QDateTime::fromMSecsSinceEpoch(3'000);
    frame.tracks = {makeTrack(7)};
    const utms::RadarFrame current_frame = store.replace(frame);

    QCOMPARE(current_frame.tracks.constFirst().first_seen_at, frame.received_at);
}

void RadarCoreTest::rejectsDuplicateAndOutOfOrderSequences() {
    utms::UdpReceiver receiver;
    QSignalSpy frame_spy(&receiver, &utms::UdpReceiver::frameReceived);
    QUdpSocket sender;
    const quint16 port = availableUdpPort();
    receiver.startListening(port);

    QVERIFY(sender.writeDatagram(sequencePayload(10, 1'000.0), QHostAddress::LocalHost, port) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 1, 1'000);
    QVERIFY(sender.writeDatagram(sequencePayload(10, 1'000.1), QHostAddress::LocalHost, port) > 0);
    QVERIFY(sender.writeDatagram(sequencePayload(9, 1'000.2), QHostAddress::LocalHost, port) > 0);
    QTest::qWait(100);
    QCOMPARE(frame_spy.count(), 1);

    receiver.stopListening();
}

void RadarCoreTest::acceptsAndReportsSequenceJump() {
    utms::UdpReceiver receiver;
    QSignalSpy frame_spy(&receiver, &utms::UdpReceiver::frameReceived);
    QUdpSocket sender;
    const quint16 port = availableUdpPort();
    receiver.startListening(port);

    QVERIFY(sender.writeDatagram(sequencePayload(10, 1'000.0), QHostAddress::LocalHost, port) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 1, 1'000);
    QTest::ignoreMessage(QtWarningMsg, "UdpReceiver: sequence jump from 10 to 13");
    QVERIFY(sender.writeDatagram(sequencePayload(13, 1'000.1), QHostAddress::LocalHost, port) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 2, 1'000);

    receiver.stopListening();
}

void RadarCoreTest::resetsSequenceWhenListeningRestarts() {
    utms::UdpReceiver receiver;
    QSignalSpy frame_spy(&receiver, &utms::UdpReceiver::frameReceived);
    QUdpSocket sender;
    const quint16 port = availableUdpPort();
    receiver.startListening(port);

    QVERIFY(sender.writeDatagram(sequencePayload(500, 1'000.0), QHostAddress::LocalHost, port) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 1, 1'000);
    receiver.stopListening();
    receiver.startListening(port);
    QVERIFY(sender.writeDatagram(sequencePayload(1, 1'000.1), QHostAddress::LocalHost, port) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 2, 1'000);

    receiver.stopListening();
}

void RadarCoreTest::acceptsFrameAfterSenderRestart() {
    utms::UdpReceiver receiver;
    QSignalSpy frame_spy(&receiver, &utms::UdpReceiver::frameReceived);
    QUdpSocket sender;
    const quint16 port = availableUdpPort();
    receiver.startListening(port);

    QCOMPARE(sender.writeDatagram(sequencePayload(500, 1'000.0), QHostAddress::LocalHost, port) > 0, true);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 1, 1'000);
    QCOMPARE(sender.writeDatagram(sequencePayload(1, 1'001.0), QHostAddress::LocalHost, port) > 0, true);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 2, 1'000);

    receiver.stopListening();
}

void RadarCoreTest::rejectsSmallOutOfOrderDropWithNewerTimestamp() {
    utms::UdpReceiver receiver;
    QSignalSpy frame_spy(&receiver, &utms::UdpReceiver::frameReceived);
    QUdpSocket sender;
    const quint16 port = availableUdpPort();
    receiver.startListening(port);

    QCOMPARE(sender.writeDatagram(sequencePayload(150, 1'000.0), QHostAddress::LocalHost, port) > 0, true);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 1, 1'000);
    QCOMPARE(sender.writeDatagram(sequencePayload(99, 1'001.0), QHostAddress::LocalHost, port) > 0, true);
    QTest::qWait(100);
    QCOMPARE(frame_spy.count(), 1);

    receiver.stopListening();
}

void RadarCoreTest::resetsSequenceBeforeAcceptingFrameWithoutSequence() {
    utms::UdpReceiver receiver;
    QSignalSpy frame_spy(&receiver, &utms::UdpReceiver::frameReceived);
    QUdpSocket sender;
    const quint16 port = availableUdpPort();
    receiver.startListening(port);

    QCOMPARE(sender.writeDatagram(sequencePayload(500, 1'000.0), QHostAddress::LocalHost, port) > 0, true);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 1, 1'000);
    QTest::qWait(3'100);
    QCOMPARE(sender.writeDatagram(sequencePayload(std::nullopt, 1'004.0), QHostAddress::LocalHost, port) > 0, true);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 2, 1'000);
    QCOMPARE(sender.writeDatagram(sequencePayload(1, 1'004.1), QHostAddress::LocalHost, port) > 0, true);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 3, 1'000);

    receiver.stopListening();
}

void RadarCoreTest::invalidFramesDoNotRefreshReceivingStatus() {
    utms::UdpReceiver receiver;
    QSignalSpy status_spy(&receiver, &utms::UdpReceiver::statusChanged);
    QUdpSocket sender;
    const quint16 port = availableUdpPort();
    receiver.startListening(port);

    QVERIFY(sender.writeDatagram(sequencePayload(1, 1'000.0), QHostAddress::LocalHost, port) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(status_spy.constLast().at(0).value<utms::UdpStatus>(), utms::UdpStatus::kReceiving,
                              1'000);
    QTest::qWait(2'500);
    QVERIFY(sender.writeDatagram(QByteArray("{"), QHostAddress::LocalHost, port) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(status_spy.count() >= 3, 1'000);
    QTRY_COMPARE_WITH_TIMEOUT(status_spy.constLast().at(0).value<utms::UdpStatus>(), utms::UdpStatus::kListeningNoData,
                              1'000);

    receiver.stopListening();
}

void RadarCoreTest::rotatesRuntimeLogsWithinRetentionLimit() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    QVERIFY(utms::Logger::install(temporary_directory.path(), 300, 3));

    qWarning().noquote() << QStringLiteral("oversized-message-%1").arg(QString(1'000, QChar('y')));
    for (int index = 0; index < 20; ++index) {
        qWarning().noquote() << QStringLiteral("rotation-test-%1-%2").arg(index).arg(QString(80, QChar('x')));
    }
    const QString current_log_path = utms::Logger::logFilePath();
    utms::Logger::shutdown();

    const QStringList log_files =
        QDir(temporary_directory.path()).entryList({QStringLiteral("utms*.log")}, QDir::Files, QDir::Name);
    QVERIFY(!current_log_path.isEmpty());
    QVERIFY(QFileInfo::exists(current_log_path));
    QCOMPARE(log_files.size(), 3);

    bool found_last_message = false;
    for (const QString &file_name : log_files) {
        QFile file(QDir(temporary_directory.path()).filePath(file_name));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.size() <= 300);
        found_last_message = found_last_message || file.readAll().contains("rotation-test-19-");
    }
    QVERIFY(found_last_message);
}

void RadarCoreTest::loadsAmapConfiguration() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString config_path = temporary_directory.filePath(QStringLiteral("amap.json"));
    QFile config_file(config_path);
    QVERIFY(config_file.open(QIODevice::WriteOnly));
    QCOMPARE(config_file.write(R"json({"key":"web-key","securityCode":"security-code"})json"), 48);
    config_file.close();

    const utms::AmapConfigResult result = utms::loadAmapConfig(config_path);

    QVERIFY2(result.config.has_value(), qPrintable(result.error));
    QCOMPARE(result.config->key, QStringLiteral("web-key"));
    QCOMPARE(result.config->security_code, QStringLiteral("security-code"));
}

void RadarCoreTest::rejectsIncompleteAmapConfiguration() {
    QTemporaryDir temporary_directory;
    QVERIFY(temporary_directory.isValid());
    const QString config_path = temporary_directory.filePath(QStringLiteral("amap.json"));
    QFile config_file(config_path);
    QVERIFY(config_file.open(QIODevice::WriteOnly));
    QVERIFY(config_file.write(R"json({"key":""})json") > 0);
    config_file.close();

    const utms::AmapConfigResult result = utms::loadAmapConfig(config_path);

    QVERIFY(!result.config.has_value());
    QVERIFY(result.error.contains(QStringLiteral("Key")));
}

void RadarCoreTest::computesIncrementalOnlineMapUpdates() {
    utms::OnlineMapState map_state;
    utms::RadarFrame first_frame;
    first_frame.tracks = {makeTrack(1), makeTrack(2)};

    const utms::OnlineMapUpdate first_update = map_state.replaceFrame(first_frame);

    QCOMPARE(first_update.upserted_targets.size(), 2);
    QVERIFY(first_update.removed_track_ids.isEmpty());

    utms::RadarFrame second_frame;
    utms::TrackData moved_track = makeTrack(2);
    moved_track.position.longitude = 110.42;
    moved_track.type = utms::TargetType::kTruck;
    second_frame.tracks = {moved_track, makeTrack(3)};

    const utms::OnlineMapUpdate second_update = map_state.replaceFrame(second_frame);

    QCOMPARE(second_update.upserted_targets.size(), 2);
    QCOMPARE(second_update.removed_track_ids, QVector<qint64>{1});
    QCOMPARE(second_update.upserted_targets.constFirst().track_id, 2);
    QCOMPARE(second_update.upserted_targets.constFirst().color, QStringLiteral("#e67e22"));
    QCOMPARE(map_state.currentFrame().tracks.size(), 2);
}

void RadarCoreTest::automaticallyCentersOnRadarOnlyOnce() {
    utms::OnlineMapState map_state;
    QCOMPARE(map_state.center().longitude, 110.416819);
    QCOMPARE(map_state.center().latitude, 25.311724);
    QCOMPARE(map_state.zoom(), 17);

    utms::RadarFrame first_frame;
    first_frame.ego_position = utms::GeoPosition{25.31, 110.41};
    const utms::OnlineMapUpdate first_update = map_state.replaceFrame(first_frame);
    QVERIFY(first_update.automatic_center.has_value());
    QCOMPARE(first_update.automatic_center.value().longitude, 110.41);

    utms::RadarFrame second_frame;
    second_frame.ego_position = utms::GeoPosition{25.32, 110.42};
    const utms::OnlineMapUpdate second_update = map_state.replaceFrame(second_frame);
    QVERIFY(!second_update.automatic_center.has_value());
    QCOMPARE(map_state.radarPosition().value().longitude, 110.42);

    QVERIFY(map_state.locateRadar());
    QCOMPARE(map_state.center().longitude, 110.42);
    QCOMPARE(map_state.center().latitude, 25.32);
}

QTEST_MAIN(RadarCoreTest)

#include "test_radarcore.moc"
