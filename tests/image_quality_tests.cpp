#include "image_quality.h"

#include <QImage>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>

#include <algorithm>
#include <cmath>

namespace {
QImage grayscaleImage(const int width, const int height, const auto &valueAt)
{
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int value = valueAt(x, y);
            pixels[x] = qRgb(value, value, value);
        }
    }
    return image;
}

quint64 histogramPopulation(const ImageQualityResult &result)
{
    quint64 count = 0;
    for (const quint64 bin : result.luminanceHistogram) {
        count += bin;
    }
    return count;
}

QImage boxBlur(const QImage &image)
{
    QImage blurred(image.size(), QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *output = reinterpret_cast<QRgb *>(blurred.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            int sum = 0;
            int count = 0;
            for (int offsetY = -1; offsetY <= 1; ++offsetY) {
                for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                    const int sampleX = std::clamp(x + offsetX, 0, image.width() - 1);
                    const int sampleY = std::clamp(y + offsetY, 0, image.height() - 1);
                    sum += qRed(image.pixel(sampleX, sampleY));
                    ++count;
                }
            }
            const int value = qRound(static_cast<double>(sum) / count);
            output[x] = qRgb(value, value, value);
        }
    }
    return blurred;
}
} // namespace

class ImageQualityTests final : public QObject {
    Q_OBJECT

private slots:
    void analyzesSolidBlackImage()
    {
        QImage image(32, 24, QImage::Format_RGB32);
        image.fill(Qt::black);
        const ImageQualityResult result = ImageQualityAnalyzer().analyze(image);

        QVERIFY(result.valid);
        QVERIFY(result.sharpnessAvailable);
        QCOMPARE(result.meanLuminance, 0.0);
        QCOMPARE(result.blackClippingPercent, 100.0);
        QCOMPARE(result.whiteClippingPercent, 0.0);
        QCOMPARE(result.sharpnessVariance, 0.0);
        QVERIFY(result.processingMilliseconds >= 0.0);
        QCOMPARE(histogramPopulation(result), quint64(32 * 24));
        QVERIFY(result.observations.contains(
            QStringLiteral("Significant shadow clipping detected.")));
    }

    void analyzesSolidWhiteImage()
    {
        QImage image(32, 24, QImage::Format_RGB32);
        image.fill(Qt::white);
        const ImageQualityResult result = ImageQualityAnalyzer().analyze(image);

        QVERIFY(result.valid);
        QCOMPARE(result.meanLuminance, 255.0);
        QCOMPARE(result.whiteClippingPercent, 100.0);
        QCOMPARE(result.blackClippingPercent, 0.0);
        QCOMPARE(result.sharpnessVariance, 0.0);
        QVERIFY(result.observations.contains(
            QStringLiteral("Significant highlight clipping detected.")));
    }

    void analyzesMidGrayImage()
    {
        const QImage image = grayscaleImage(20, 20, [](int, int) { return 128; });
        const ImageQualityResult result = ImageQualityAnalyzer().analyze(image);

        QVERIFY(result.valid);
        QCOMPARE(result.meanLuminance, 128.0);
        QCOMPARE(result.blackClippingPercent, 0.0);
        QCOMPARE(result.whiteClippingPercent, 0.0);
        QCOMPARE(result.sharpnessVariance, 0.0);
        QCOMPARE(result.contrast, 0.0);
    }

    void appliesInclusiveClippingThresholds()
    {
        QImage image(4, 1, QImage::Format_RGB32);
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(0));
        pixels[0] = qRgb(5, 5, 5);
        pixels[1] = qRgb(6, 6, 6);
        pixels[2] = qRgb(249, 249, 249);
        pixels[3] = qRgb(250, 250, 250);
        const ImageQualityResult result = ImageQualityAnalyzer().analyze(image);

        QVERIFY(result.valid);
        QCOMPARE(result.blackClippingPercent, 25.0);
        QCOMPARE(result.whiteClippingPercent, 25.0);
    }

    void usesRequiredRgbLuminanceWeights()
    {
        QImage red(1, 1, QImage::Format_RGB32);
        red.fill(qRgb(255, 0, 0));
        QImage green(1, 1, QImage::Format_RGB32);
        green.fill(qRgb(0, 255, 0));
        QImage blue(1, 1, QImage::Format_RGB32);
        blue.fill(qRgb(0, 0, 255));

        const ImageQualityAnalyzer analyzer;
        QVERIFY(qAbs(analyzer.analyze(red).meanLuminance - 54.213) < 0.001);
        QVERIFY(qAbs(analyzer.analyze(green).meanLuminance - 182.376) < 0.001);
        QVERIFY(qAbs(analyzer.analyze(blue).meanLuminance - 18.411) < 0.001);
    }

    void calculatesGradientHistogramAndPercentileContrast()
    {
        const QImage image = grayscaleImage(256, 20, [](int x, int) { return x; });
        const ImageQualityResult result = ImageQualityAnalyzer().analyze(image);

        QVERIFY(result.valid);
        QCOMPARE(histogramPopulation(result), quint64(256 * 20));
        for (int value = 0; value < 256; ++value) {
            QCOMPARE(result.luminanceHistogram.at(static_cast<std::size_t>(value)), quint64(20));
        }
        QVERIFY(qAbs(result.meanLuminance - 127.5) < 0.001);
        QCOMPARE(result.contrast, 231.0);
    }

    void capsAnalysisResolution()
    {
        QImage image(800, 400, QImage::Format_RGB32);
        image.fill(qRgb(128, 128, 128));
        const ImageQualityAnalyzer analyzer(QSize(1280, 720));
        const ImageQualityResult result = analyzer.analyze(image);

        QVERIFY(result.valid);
        QCOMPARE(histogramPopulation(result), quint64(640 * 320));
        QCOMPARE(result.meanLuminance, 128.0);
    }

    void blurReducesLaplacianVariance()
    {
        const QImage textured = grayscaleImage(64, 64, [](const int x, const int y) {
            return ((x + y) % 2 == 0) ? 0 : 255;
        });
        const ImageQualityAnalyzer analyzer;
        const ImageQualityResult sharp = analyzer.analyze(textured);
        const ImageQualityResult blurred = analyzer.analyze(boxBlur(textured));

        QVERIFY(sharp.valid);
        QVERIFY(blurred.valid);
        QVERIFY(sharp.sharpnessVariance > blurred.sharpnessVariance);
    }

    void reportsEmptyAndTinyImagesExplicitly()
    {
        const ImageQualityResult empty = ImageQualityAnalyzer().analyze(QImage());
        QVERIFY(!empty.valid);
        QVERIFY(!empty.error.isEmpty());
        QVERIFY(empty.processingMilliseconds >= 0.0);

        QImage tiny(2, 2, QImage::Format_Grayscale16);
        tiny.fill(0x8080);
        const ImageQualityResult small = ImageQualityAnalyzer().analyze(tiny);
        QVERIFY(small.valid);
        QVERIFY(!small.sharpnessAvailable);
        QCOMPARE(histogramPopulation(small), quint64(4));
    }

    void supportsConfigurableObservationThresholds()
    {
        const ImageQualityThresholds thresholds{
            .significantClippingPercent = 101.0,
            .lowAverageLuminance = 0.0,
            .lowSharpnessVariance = 0.0,
        };
        QImage black(8, 8, QImage::Format_RGB32);
        black.fill(Qt::black);
        const ImageQualityResult result = ImageQualityAnalyzer(QSize(640, 360), thresholds)
                                              .analyze(black);

        QVERIFY(result.valid);
        QVERIFY(result.observations.isEmpty());
    }

    void samplesAtConfiguredRateAndResets()
    {
        ImageQualitySampler sampler;
        QVERIFY(sampler.tryStartAnalysis(0));
        QVERIFY(!sampler.tryStartAnalysis(333'333'333));
        sampler.finishAnalysis();
        QVERIFY(!sampler.tryStartAnalysis(333'333'332));
        QVERIFY(sampler.tryStartAnalysis(333'333'334));
        sampler.finishAnalysis();
        QVERIFY(!sampler.tryStartAnalysis(300'000'000));

        sampler.reset();
        QVERIFY(sampler.tryStartAnalysis(1));
        sampler.finishAnalysis();
    }

    void keepsAnalysisBoundedAndRunsOffTheEventThread()
    {
        ImageQualitySampler sampler;
        QVERIFY(sampler.tryStartAnalysis(0));
        QVERIFY(!sampler.tryStartAnalysis(1'000'000'000));
        sampler.finishAnalysis();
        QVERIFY(sampler.tryStartAnalysis(1'000'000'000));
        sampler.finishAnalysis();

        const QImage image = grayscaleImage(640, 360, [](int x, int y) {
            return (x * 17 + y * 31) % 256;
        });
        QFutureWatcher<ImageQualityResult> watcher;
        QEventLoop eventLoop;
        QTimer uiHeartbeat;
        uiHeartbeat.setSingleShot(true);
        bool heartbeatDelivered = false;
        connect(&uiHeartbeat, &QTimer::timeout, &eventLoop, [&] {
            heartbeatDelivered = true;
            eventLoop.quit();
        });
        connect(&watcher, &QFutureWatcher<ImageQualityResult>::finished,
                &eventLoop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([image] {
            return ImageQualityAnalyzer().analyze(image);
        }));
        uiHeartbeat.start(0);
        eventLoop.exec();
        watcher.waitForFinished();
        QVERIFY(heartbeatDelivered);
        QVERIFY(watcher.result().valid);
        QVERIFY(watcher.result().processingMilliseconds >= 0.0);
    }
};

QTEST_GUILESS_MAIN(ImageQualityTests)
#include "image_quality_tests.moc"
