#include "camera_performance.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace {
constexpr qint64 kWindowNanoseconds = 2'000'000'000;
constexpr double kLateFrameIntervalMultiplier = 1.5;
constexpr double kNanosecondsPerSecond = 1'000'000'000.0;
constexpr double kNanosecondsPerMillisecond = 1'000'000.0;
} // namespace

CameraPerformance::CameraPerformance(const double expectedFrameRate)
{
    reset(expectedFrameRate);
}

void CameraPerformance::reset(const double expectedFrameRate)
{
    frameTimestamps_.clear();
    lateFrameCount_ = 0;
    expectedFrameIntervalNanoseconds_ =
        std::isfinite(expectedFrameRate) && expectedFrameRate > 0.0
        ? kNanosecondsPerSecond / expectedFrameRate
        : 0.0;
}

void CameraPerformance::recordFrame(const qint64 timestampNanoseconds)
{
    if (!frameTimestamps_.empty() && timestampNanoseconds <= frameTimestamps_.back()) {
        return;
    }

    if (!frameTimestamps_.empty()) {
        const qint64 intervalNanoseconds = timestampNanoseconds - frameTimestamps_.back();
        if (expectedFrameIntervalNanoseconds_ > 0.0
            && intervalNanoseconds
                > expectedFrameIntervalNanoseconds_ * kLateFrameIntervalMultiplier) {
            ++lateFrameCount_;
        }
    }

    frameTimestamps_.push_back(timestampNanoseconds);
    while (!frameTimestamps_.empty()
           && timestampNanoseconds - frameTimestamps_.front() > kWindowNanoseconds) {
        frameTimestamps_.pop_front();
    }
}

CameraPerformanceMetrics CameraPerformance::metrics(const qint64 nowNanoseconds) const
{
    CameraPerformanceMetrics result;
    result.lateFrameCount = lateFrameCount_;
    result.lateFrameCountAvailable = expectedFrameIntervalNanoseconds_ > 0.0;
    if (frameTimestamps_.empty()) {
        return result;
    }

    const qint64 windowStart = nowNanoseconds < std::numeric_limits<qint64>::min() + kWindowNanoseconds
        ? std::numeric_limits<qint64>::min()
        : nowNanoseconds - kWindowNanoseconds;
    const auto first = std::lower_bound(frameTimestamps_.cbegin(), frameTimestamps_.cend(), windowStart);
    const auto end = std::upper_bound(first, frameTimestamps_.cend(), nowNanoseconds);
    if (std::distance(first, end) < 2) {
        return result;
    }
    result.hasFrameIntervals = true;

    double meanIntervalNanoseconds = 0.0;
    double sumSquaredDifferences = 0.0;
    qint64 previousTimestamp = *first;
    qint64 intervalCount = 0;

    for (auto current = std::next(first); current != end; ++current) {
        const double intervalNanoseconds = static_cast<double>(*current - previousTimestamp);
        ++intervalCount;
        const double differenceFromMean = intervalNanoseconds - meanIntervalNanoseconds;
        meanIntervalNanoseconds += differenceFromMean / static_cast<double>(intervalCount);
        sumSquaredDifferences += differenceFromMean * (intervalNanoseconds - meanIntervalNanoseconds);
        previousTimestamp = *current;
    }

    result.rollingFramesPerSecond =
        static_cast<double>(intervalCount) * kNanosecondsPerSecond
        / static_cast<double>(*std::prev(end) - *first);
    result.averageFrameIntervalMilliseconds =
        meanIntervalNanoseconds / kNanosecondsPerMillisecond;
    result.frameIntervalJitterMilliseconds =
        std::sqrt(sumSquaredDifferences / static_cast<double>(intervalCount))
        / kNanosecondsPerMillisecond;
    return result;
}
