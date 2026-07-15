#include <memory>

#include <QSignalSpy>
#include <QtTest>

#include "media/LatestVideoFrameBuffer.h"
#include "media/VideoDetection.h"
#include "media/YoloInferenceWorker.h"

class YoloRuntimeTest : public QObject {
    Q_OBJECT

private slots:
    void loadsAndRunsBundledModel();
};

void YoloRuntimeTest::loadsAndRunsBundledModel()
{
    qRegisterMetaType<QVector<utms::VideoDetection>>();
    auto frame_buffer = std::make_shared<utms::LatestVideoFrameBuffer>();
    utms::YoloInferenceWorker inference_worker(frame_buffer);
    QSignalSpy model_ready_spy(&inference_worker, &utms::YoloInferenceWorker::modelReady);
    QSignalSpy model_error_spy(&inference_worker, &utms::YoloInferenceWorker::modelError);
    QSignalSpy result_spy(&inference_worker, &utms::YoloInferenceWorker::inferenceCompleted);
    QSignalSpy inference_error_spy(&inference_worker, &utms::YoloInferenceWorker::inferenceError);

    inference_worker.requestProcessing(true);
    inference_worker.enableDetection(1, QString::fromUtf8(UTMS_TEST_MODEL_DIRECTORY));

    QCOMPARE(model_error_spy.count(), 0);
    QCOMPARE(model_ready_spy.count(), 1);

    QImage frame(QSize(640, 480), QImage::Format_RGB888);
    frame.fill(Qt::black);
    frame_buffer->replace(frame, 7);
    QVERIFY(frame_buffer->tryBeginProcessing());
    inference_worker.processLatestFrames();

    QCOMPARE(inference_error_spy.count(), 0);
    QCOMPARE(result_spy.count(), 1);
    QCOMPARE(result_spy.constFirst().at(1).toULongLong(), quint64(7));
    inference_worker.releaseModel();
}

QTEST_MAIN(YoloRuntimeTest)

#include "test_yolo_runtime.moc"
