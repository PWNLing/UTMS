#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "media/LatestVideoFrameBuffer.h"
#include "media/VideoDetection.h"
#include "media/YoloModelConfig.h"
#include "media/YoloPostProcessor.h"

class VideoInferenceTest : public QObject {
    Q_OBJECT

private slots:
    void readsYoloModelConfiguration();
    void rejectsIncompleteModelConfiguration();
    void mapsVideoClasses_data();
    void mapsVideoClasses();
    void mapsYoloClassesAndSuppressesOverlaps();
    void keepsOnlyTheLatestPendingFrame();
};

void VideoInferenceTest::readsYoloModelConfiguration()
{
    QTemporaryDir model_directory;
    QVERIFY(model_directory.isValid());

    QFile model_file(model_directory.filePath(QStringLiteral("model.onnx")));
    QVERIFY(model_file.open(QIODevice::WriteOnly));
    model_file.close();

    QFile classes_file(model_directory.filePath(QStringLiteral("classes.txt")));
    QVERIFY(classes_file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(classes_file.write("person\nbicycle\ncar\ntruck\n"), qint64(25));
    classes_file.close();

    QFile config_file(model_directory.filePath(QStringLiteral("model.json")));
    QVERIFY(config_file.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray configuration = R"({
        "model_family": "yolo26",
        "task": "detect",
        "model_file": "model.onnx",
        "classes_file": "classes.txt",
        "input_width": 640,
        "input_height": 640,
        "confidence_threshold": 0.25,
        "nms_threshold": 0.45,
        "letterbox": true,
        "swap_rb": true,
        "normalize": true,
        "normalize_scale": 0.00392156862745098
    })";
    QCOMPARE(config_file.write(configuration), qint64(configuration.size()));
    config_file.close();

    QString error;
    const auto config = utms::YoloModelConfig::read(model_directory.path(), &error);

    QVERIFY2(config.has_value(), qPrintable(error));
    QCOMPARE(config->input_size, QSize(640, 640));
    QCOMPARE(config->class_names, QStringList({QStringLiteral("person"), QStringLiteral("bicycle"),
                                               QStringLiteral("car"), QStringLiteral("truck")}));
    QCOMPARE(config->model_path, model_file.fileName());
}

void VideoInferenceTest::rejectsIncompleteModelConfiguration()
{
    QTemporaryDir model_directory;
    QVERIFY(model_directory.isValid());

    QFile config_file(model_directory.filePath(QStringLiteral("model.json")));
    QVERIFY(config_file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(config_file.write("{}"), qint64(2));
    config_file.close();

    QString error;
    const auto config = utms::YoloModelConfig::read(model_directory.path(), &error);

    QVERIFY(!config.has_value());
    QVERIFY(!error.isEmpty());
}

void VideoInferenceTest::mapsVideoClasses_data()
{
    QTest::addColumn<QString>("class_name");
    QTest::addColumn<utms::VideoDetectionCategory>("expected_category");
    QTest::addColumn<QString>("expected_text");

    QTest::newRow("person") << QStringLiteral("person") << utms::VideoDetectionCategory::kPedestrian
                             << QStringLiteral("行人");
    QTest::newRow("bicycle") << QStringLiteral("bicycle") << utms::VideoDetectionCategory::kBicycle
                              << QStringLiteral("自行车");
    QTest::newRow("car") << QStringLiteral("car") << utms::VideoDetectionCategory::kCar << QStringLiteral("汽车");
    QTest::newRow("truck") << QStringLiteral("truck") << utms::VideoDetectionCategory::kTruck
                            << QStringLiteral("卡车");
    QTest::newRow("other") << QStringLiteral("bus") << utms::VideoDetectionCategory::kUnknown
                            << QStringLiteral("未知");
}

void VideoInferenceTest::mapsVideoClasses()
{
    QFETCH(QString, class_name);
    QFETCH(utms::VideoDetectionCategory, expected_category);
    QFETCH(QString, expected_text);

    const auto category = utms::videoDetectionCategoryFromClassName(class_name);
    QCOMPARE(category, expected_category);
    QCOMPARE(utms::videoDetectionCategoryText(category), expected_text);
}

void VideoInferenceTest::mapsYoloClassesAndSuppressesOverlaps()
{
    utms::YoloModelConfig config;
    config.input_size = QSize(640, 640);
    config.confidence_threshold = 0.25F;
    config.nms_threshold = 0.45F;
    config.class_names = {QStringLiteral("person"), QStringLiteral("bicycle"), QStringLiteral("car"),
                          QStringLiteral("truck"), QStringLiteral("bus")};

    // Shape is [batch, attributes, candidates]. The first two candidates overlap and
    // represent the same car; the third is an unmapped class and stays as unknown.
    const std::vector<float> output = {
        320.0F, 322.0F, 120.0F, // center x
        320.0F, 322.0F, 120.0F, // center y
        100.0F, 100.0F, 40.0F,  // width
        100.0F, 100.0F, 40.0F,  // height
        0.01F, 0.01F, 0.01F,    // person
        0.01F, 0.01F, 0.01F,    // bicycle
        0.90F, 0.80F, 0.01F,    // car
        0.01F, 0.01F, 0.01F,    // truck
        0.01F, 0.01F, 0.85F,    // bus -> unknown
    };

    const QVector<utms::VideoDetection> detections =
        utms::YoloPostProcessor::process(output, {1, 9, 3}, config, QSize(640, 640));

    QCOMPARE(detections.size(), 2);
    QCOMPARE(detections.at(0).category, utms::VideoDetectionCategory::kCar);
    QCOMPARE(detections.at(1).category, utms::VideoDetectionCategory::kUnknown);
    QCOMPARE(utms::videoDetectionCategoryText(detections.at(0).category), QStringLiteral("汽车"));
    QCOMPARE(utms::videoDetectionCategoryText(detections.at(1).category), QStringLiteral("未知"));
}

void VideoInferenceTest::keepsOnlyTheLatestPendingFrame()
{
    utms::LatestVideoFrameBuffer frame_buffer;
    const QImage first_frame(QSize(2, 2), QImage::Format_RGB32);
    QImage latest_frame(QSize(2, 2), QImage::Format_RGB32);
    latest_frame.fill(Qt::green);

    frame_buffer.replace(first_frame, 1);
    frame_buffer.replace(latest_frame, 2);
    QVERIFY(frame_buffer.tryBeginProcessing());

    const auto next_frame = frame_buffer.takeLatest();
    QVERIFY(next_frame.has_value());
    QCOMPARE(next_frame->generation, quint64(2));
    QCOMPARE(next_frame->frame.pixelColor(0, 0), QColor(Qt::green));
    QVERIFY(!frame_buffer.takeLatest().has_value());

    QImage replacement_frame(QSize(2, 2), QImage::Format_RGB32);
    replacement_frame.fill(Qt::blue);
    frame_buffer.replace(replacement_frame, 3);
    QVERIFY(frame_buffer.tryBeginProcessing());
    QCOMPARE(frame_buffer.takeLatest()->generation, quint64(3));
}

QTEST_MAIN(VideoInferenceTest)

#include "test_video_inference.moc"
