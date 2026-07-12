#include <algorithm>

#include <QtTest>
#include <QUdpSocket>

#include "core/RadarFrameStore.h"
#include "core/RadarJsonParser.h"
#include "network/UdpReceiver.h"

namespace {

utms::TrackData makeTrack(qint64 track_id)
{
    utms::TrackData track;
    track.track_id = track_id;
    track.type = utms::TargetType::kCar;
    track.position = utms::GeoPosition{25.31, 110.41};
    return track;
}

const utms::TrackData &findTrack(const utms::RadarFrame &frame, qint64 track_id)
{
    const auto iterator = std::find_if(frame.tracks.cbegin(), frame.tracks.cend(),
                                       [track_id](const utms::TrackData &track) {
                                           return track.track_id == track_id;
                                       });
    Q_ASSERT(iterator != frame.tracks.cend());
    return *iterator;
}

QByteArray sequencePayload(std::optional<qint64> sequence, double timestamp)
{
    const QString header = sequence.has_value()
                               ? QStringLiteral(R"json("header":{"sequence":%1},)json").arg(sequence.value())
                               : QString();
    return QStringLiteral(R"json({%1"timestamp":%2,"tracks":[]})json")
        .arg(header)
        .arg(timestamp, 0, 'f', 3)
        .toUtf8();
}

quint16 availableUdpPort()
{
    QUdpSocket socket;
    const bool bound = socket.bind(QHostAddress::LocalHost, 0);
    Q_ASSERT(bound);
    return socket.localPort();
}

}  // namespace

class RadarCoreTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesValidJson();
    void replacesCurrentFrameAndPreservesFirstSeen();
    void removesDisappearedTargets();
    void resetsFirstSeenAfterTargetDisappears();
    void acceptsFrameAfterSenderRestart();
    void rejectsSmallOutOfOrderDropWithNewerTimestamp();
    void resetsSequenceBeforeAcceptingFrameWithoutSequence();
};

void RadarCoreTest::parsesValidJson()
{
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

void RadarCoreTest::replacesCurrentFrameAndPreservesFirstSeen()
{
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

void RadarCoreTest::removesDisappearedTargets()
{
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

void RadarCoreTest::resetsFirstSeenAfterTargetDisappears()
{
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

void RadarCoreTest::acceptsFrameAfterSenderRestart()
{
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

void RadarCoreTest::rejectsSmallOutOfOrderDropWithNewerTimestamp()
{
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

void RadarCoreTest::resetsSequenceBeforeAcceptingFrameWithoutSequence()
{
    utms::UdpReceiver receiver;
    QSignalSpy frame_spy(&receiver, &utms::UdpReceiver::frameReceived);
    QUdpSocket sender;
    const quint16 port = availableUdpPort();
    receiver.startListening(port);

    QCOMPARE(sender.writeDatagram(sequencePayload(500, 1'000.0), QHostAddress::LocalHost, port) > 0, true);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 1, 1'000);
    QTest::qWait(3'100);
    QCOMPARE(sender.writeDatagram(sequencePayload(std::nullopt, 1'004.0), QHostAddress::LocalHost, port) > 0,
             true);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 2, 1'000);
    QCOMPARE(sender.writeDatagram(sequencePayload(1, 1'004.1), QHostAddress::LocalHost, port) > 0, true);
    QTRY_COMPARE_WITH_TIMEOUT(frame_spy.count(), 3, 1'000);

    receiver.stopListening();
}

QTEST_MAIN(RadarCoreTest)

#include "test_radarcore.moc"
