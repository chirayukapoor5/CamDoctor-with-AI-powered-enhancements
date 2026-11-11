#pragma once

#include <QtGlobal>

#include <deque>

struct CameraPerformanceMetrics {
    double rollingFramesPerSecond{};
    double averageFrameIntervalMilliseconds{};
    double frameIntervalJitterMilliseconds{};
    qint64 lateFrameCount{};
    bool hasFrameIntervals{};
    bool lateFrameCountAvailable{};
};

class CameraPerformance final {
public:
    explicit CameraPerformance(double expectedFrameRate = 0.0);

    void reset(double expectedFrameRate = 0.0);
    void recordFrame(qint64 timestampNanoseconds);

    [[nodiscard]] CameraPerformanceMetrics metrics(qint64 nowNanoseconds) const;

private:
    std::deque<qint64> frameTimestamps_;
    double expectedFrameIntervalNanoseconds_{};
    qint64 lateFrameCount_{};
};
