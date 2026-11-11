#include "image_quality.h"

#include <QElapsedTimer>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
constexpr double kRedLuminanceWeight = 0.2126;
constexpr double kGreenLuminanceWeight = 0.7152;
constexpr double kBlueLuminanceWeight = 0.0722;
constexpr double kBlackClippingMaximum = 5.0;
constexpr double kWhiteClippingMinimum = 250.0;
constexpr double kPercentileLow = 0.05;
constexpr double kPercentileHigh = 0.95;
constexpr qint64 kNanosecondsPerSecond = 1'000'000'000;
constexpr double kDefaultSampleRate = 3.0;
constexpr QSize kMaximumAnalysisResolution(640, 360);

double luminance(const QRgb pixel)
{
    return kRedLuminanceWeight * qRed(pixel)
        + kGreenLuminanceWeight * qGreen(pixel)
        + kBlueLuminanceWeight * qBlue(pixel);
}

int percentileBin(const std::array<quint64, 256> &histogram,
                  const quint64 pixelCount,
                  const double percentile)
{
    const auto rank = std::max<quint64>(
        1, static_cast<quint64>(std::ceil(percentile * static_cast<double>(pixelCount))));
    quint64 cumulative = 0;
    for (int bin = 0; bin < static_cast<int>(histogram.size()); ++bin) {
        cumulative += histogram.at(static_cast<std::size_t>(bin));
        if (cumulative >= rank) {
            return bin;
        }
    }
    return static_cast<int>(histogram.size()) - 1;
}
} // namespace

ImageQualityAnalyzer::ImageQualityAnalyzer(const QSize maximumResolution,
                                           const ImageQualityThresholds thresholds)
    : maximumResolution_(maximumResolution.boundedTo(kMaximumAnalysisResolution)
                             .expandedTo(QSize(1, 1))),
      thresholds_(thresholds)
{
}

ImageQualityResult ImageQualityAnalyzer::analyze(const QImage &image) const
{
    QElapsedTimer timer;
    timer.start();

    ImageQualityResult result;
    const auto unavailable = [&result, &timer](const QString &reason) {
        result.error = reason;
        result.processingMilliseconds = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
        return result;
    };

    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return unavailable(QStringLiteral("Image is empty."));
    }

    QImage boundedImage = image;
    if (image.width() > maximumResolution_.width()
        || image.height() > maximumResolution_.height()) {
        boundedImage = image.scaled(
            maximumResolution_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (boundedImage.isNull()) {
            return unavailable(QStringLiteral("Could not resize the image for analysis."));
        }
    }

    const QImage rgbImage = boundedImage.convertToFormat(QImage::Format_RGB32);
    if (rgbImage.isNull()) {
        return unavailable(QStringLiteral("Could not convert the image to RGB."));
    }

    const auto pixelCount = static_cast<quint64>(rgbImage.width())
        * static_cast<quint64>(rgbImage.height());
    std::vector<double> luminanceValues(static_cast<std::size_t>(pixelCount));
    quint64 blackPixelCount = 0;
    quint64 whitePixelCount = 0;
    double luminanceSum = 0.0;

    for (int y = 0; y < rgbImage.height(); ++y) {
        const auto *pixels = reinterpret_cast<const QRgb *>(rgbImage.constScanLine(y));
        const auto rowOffset = static_cast<std::size_t>(y)
            * static_cast<std::size_t>(rgbImage.width());
        for (int x = 0; x < rgbImage.width(); ++x) {
            const double value = luminance(pixels[x]);
            luminanceValues[rowOffset + static_cast<std::size_t>(x)] = value;
            luminanceSum += value;
            if (value <= kBlackClippingMaximum) {
                ++blackPixelCount;
            }
            if (value >= kWhiteClippingMinimum) {
                ++whitePixelCount;
            }
            const int bin = std::clamp(qRound(value), 0, 255);
            ++result.luminanceHistogram.at(static_cast<std::size_t>(bin));
        }
    }

    result.valid = true;
    result.meanLuminance = luminanceSum / static_cast<double>(pixelCount);
    result.blackClippingPercent =
        100.0 * static_cast<double>(blackPixelCount) / static_cast<double>(pixelCount);
    result.whiteClippingPercent =
        100.0 * static_cast<double>(whitePixelCount) / static_cast<double>(pixelCount);
    const int lowPercentile =
        percentileBin(result.luminanceHistogram, pixelCount, kPercentileLow);
    const int highPercentile =
        percentileBin(result.luminanceHistogram, pixelCount, kPercentileHigh);
    result.contrast = static_cast<double>(highPercentile - lowPercentile);

    if (rgbImage.width() >= 3 && rgbImage.height() >= 3) {
        double meanLaplacian = 0.0;
        double sumSquaredDifferences = 0.0;
        quint64 sampleCount = 0;
        const auto width = static_cast<std::size_t>(rgbImage.width());

        for (int y = 1; y < rgbImage.height() - 1; ++y) {
            const auto rowOffset = static_cast<std::size_t>(y) * width;
            for (int x = 1; x < rgbImage.width() - 1; ++x) {
                const auto index = rowOffset + static_cast<std::size_t>(x);
                const double response = 4.0 * luminanceValues[index]
                    - luminanceValues[index - 1]
                    - luminanceValues[index + 1]
                    - luminanceValues[index - width]
                    - luminanceValues[index + width];
                ++sampleCount;
                const double difference = response - meanLaplacian;
                meanLaplacian += difference / static_cast<double>(sampleCount);
                sumSquaredDifferences += difference * (response - meanLaplacian);
            }
        }

        result.sharpnessAvailable = sampleCount > 0;
        if (result.sharpnessAvailable) {
            result.sharpnessVariance =
                sumSquaredDifferences / static_cast<double>(sampleCount);
        }
    }

    if (result.blackClippingPercent >= thresholds_.significantClippingPercent) {
        result.observations.append(QStringLiteral("Significant shadow clipping detected."));
    }
    if (result.whiteClippingPercent >= thresholds_.significantClippingPercent) {
        result.observations.append(QStringLiteral("Significant highlight clipping detected."));
    }
    if (result.meanLuminance < thresholds_.lowAverageLuminance) {
        result.observations.append(QStringLiteral("The current scene has low average luminance."));
    }
    if (result.sharpnessAvailable
        && result.sharpnessVariance < thresholds_.lowSharpnessVariance) {
        result.observations.append(
            QStringLiteral("Low edge detail measured. Check focus, lighting, or scene texture."));
    }

    result.processingMilliseconds = static_cast<double>(timer.nsecsElapsed()) / 1'000'000.0;
    return result;
}

ImageQualitySampler::ImageQualitySampler(const double framesPerSecond)
{
    const double safeFrameRate = std::isfinite(framesPerSecond) && framesPerSecond > 0.0
        ? framesPerSecond
        : kDefaultSampleRate;
    const double interval = std::ceil(
        static_cast<double>(kNanosecondsPerSecond) / safeFrameRate);
    if (!std::isfinite(interval)
        || interval >= static_cast<double>(std::numeric_limits<qint64>::max())) {
        minimumIntervalNanoseconds_ = std::numeric_limits<qint64>::max();
    } else {
        minimumIntervalNanoseconds_ = std::max<qint64>(1, static_cast<qint64>(interval));
    }
}

void ImageQualitySampler::reset()
{
    lastAnalysisNanoseconds_.reset();
}

bool ImageQualitySampler::tryStartAnalysis(const qint64 timestampNanoseconds)
{
    if (analysisInFlight_ || timestampNanoseconds < 0
        || (lastAnalysisNanoseconds_
            && timestampNanoseconds <= *lastAnalysisNanoseconds_)) {
        return false;
    }
    if (lastAnalysisNanoseconds_
        && timestampNanoseconds - *lastAnalysisNanoseconds_ < minimumIntervalNanoseconds_) {
        return false;
    }
    lastAnalysisNanoseconds_ = timestampNanoseconds;
    analysisInFlight_ = true;
    return true;
}

void ImageQualitySampler::finishAnalysis()
{
    analysisInFlight_ = false;
}
