#include "camera_performance.h"

#include <QtTest>

class CameraPerformanceTests final : public QObject {
    Q_OBJECT

private slots:
    void calculatesRollingFramesPerSecondAndAverageInterval()
    {
        constexpr qint64 nanosecondsPerFrame = 1'000'000'000 / 30;
        CameraPerformance performance;
        for (qint64 frame = 0; frame <= 60; ++frame) {
            performance.recordFrame(frame * nanosecondsPerFrame);
        }

        const CameraPerformanceMetrics current = performance.metrics(2'000'000'000);
        QVERIFY(qAbs(current.rollingFramesPerSecond - 30.0) < 0.01);
        QVERIFY(qAbs(current.averageFrameIntervalMilliseconds - 33.333333) < 0.001);
        QVERIFY(current.frameIntervalJitterMilliseconds < 0.001);
    }

    void calculatesJitterForVariableFrameIntervals()
    {
        CameraPerformance performance;
        performance.recordFrame(0);
        performance.recordFrame(10'000'000);
        performance.recordFrame(30'000'000);
        performance.recordFrame(40'000'000);
        performance.recordFrame(60'000'000);

        const CameraPerformanceMetrics current = performance.metrics(60'000'000);
        QVERIFY(qAbs(current.averageFrameIntervalMilliseconds - 15.0) < 0.001);
        QVERIFY(qAbs(current.frameIntervalJitterMilliseconds - 5.0) < 0.001);
        QVERIFY(qAbs(current.rollingFramesPerSecond - (1'000.0 / 15.0)) < 0.001);
    }

    void excludesFramesOutsideRollingWindow()
    {
        CameraPerformance performance;
        performance.recordFrame(0);
        performance.recordFrame(100'000'000);
        performance.recordFrame(2'100'000'000);

        const CameraPerformanceMetrics current = performance.metrics(2'100'000'000);
        QVERIFY(qAbs(current.rollingFramesPerSecond - 0.5) < 0.001);
        QCOMPARE(current.averageFrameIntervalMilliseconds, 2'000.0);
    }

    void countsLateIntervalsAndResetsForNewCamera()
    {
        CameraPerformance performance(30.0);
        performance.recordFrame(0);
        performance.recordFrame(50'000'000);
        QCOMPARE(performance.metrics(50'000'000).lateFrameCount, qint64(0));
        performance.recordFrame(101'000'000);
        QCOMPARE(performance.metrics(101'000'000).lateFrameCount, qint64(1));

        performance.reset(60.0);
        performance.recordFrame(5'000'000'000);
        QCOMPARE(performance.metrics(5'000'000'000).lateFrameCount, qint64(0));
    }

    void reportsUnavailableMetricsWhenNoNominalFrameRateExists()
    {
        CameraPerformance performance;
        performance.recordFrame(0);
        performance.recordFrame(20'000'000);

        const CameraPerformanceMetrics current = performance.metrics(20'000'000);
        QVERIFY(current.hasFrameIntervals);
        QVERIFY(!current.lateFrameCountAvailable);
    }
};

QTEST_GUILESS_MAIN(CameraPerformanceTests)
#include "camera_performance_tests.moc"
