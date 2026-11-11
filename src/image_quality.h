#pragma once

#include <QImage>
#include <QSize>
#include <QStringList>

#include <array>
#include <optional>

struct ImageQualityThresholds {
    double significantClippingPercent{5.0};
    double lowAverageLuminance{45.0};
    double lowSharpnessVariance{20.0};
};

struct ImageQualityResult {
    bool valid{};
    bool sharpnessAvailable{};
    double meanLuminance{};
    double sharpnessVariance{};
    double blackClippingPercent{};
    double whiteClippingPercent{};
    double contrast{};
    double processingMilliseconds{};
    std::array<quint64, 256> luminanceHistogram{};
    QStringList observations;
    QString error;
};

class ImageQualityAnalyzer final {
public:
    explicit ImageQualityAnalyzer(
        QSize maximumResolution = QSize(640, 360),
        ImageQualityThresholds thresholds = {});

    [[nodiscard]] ImageQualityResult analyze(const QImage &image) const;

private:
    QSize maximumResolution_;
    ImageQualityThresholds thresholds_;
};

class ImageQualitySampler final {
public:
    explicit ImageQualitySampler(double framesPerSecond = 3.0);

    // Resets sampling cadence without releasing an active analysis slot.
    void reset();
    [[nodiscard]] bool tryStartAnalysis(qint64 timestampNanoseconds);
    void finishAnalysis();

private:
    qint64 minimumIntervalNanoseconds_{};
    std::optional<qint64> lastAnalysisNanoseconds_;
    bool analysisInFlight_{};
};
