#include "image_effects.h"

#include <algorithm>
#include <cmath>

namespace {
int clampChannel(const double value)
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
}

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}
} // namespace

bool ImageEffectsProcessor::validate(const ImageEffects &effects, QString *error)
{
    if (effects.brightness < -100 || effects.brightness > 100
        || !std::isfinite(effects.contrast) || effects.contrast < 0.5 || effects.contrast > 1.5
        || !std::isfinite(effects.saturation) || effects.saturation < 0.0
        || effects.saturation > 2.0 || effects.warmth < -40 || effects.warmth > 40) {
        setError(error, QStringLiteral("Effect values are outside their supported ranges."));
        return false;
    }
    return true;
}

QImage ImageEffectsProcessor::apply(const QImage &image, const ImageEffects &effects)
{
    if (image.isNull() || !validate(effects)) {
        return {};
    }

    QImage output = image.convertToFormat(QImage::Format_ARGB32);
    if (output.isNull()) {
        return {};
    }

    for (int y = 0; y < output.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(output.scanLine(y));
        for (int x = 0; x < output.width(); ++x) {
            const QRgb pixel = line[x];
            double red = (qRed(pixel) - 127.5) * effects.contrast + 127.5
                + effects.brightness + effects.warmth;
            double green = (qGreen(pixel) - 127.5) * effects.contrast + 127.5
                + effects.brightness;
            double blue = (qBlue(pixel) - 127.5) * effects.contrast + 127.5
                + effects.brightness - effects.warmth;
            const double luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
            red = luminance + (red - luminance) * effects.saturation;
            green = luminance + (green - luminance) * effects.saturation;
            blue = luminance + (blue - luminance) * effects.saturation;
            if (effects.grayscale) {
                red = green = blue = luminance;
            }
            line[x] = qRgba(clampChannel(red), clampChannel(green), clampChannel(blue),
                            qAlpha(pixel));
        }
    }

    if (effects.mildSharpening && output.width() > 2 && output.height() > 2) {
        const QImage source = output;
        for (int y = 1; y < output.height() - 1; ++y) {
            auto *destination = reinterpret_cast<QRgb *>(output.scanLine(y));
            const auto *above = reinterpret_cast<const QRgb *>(source.constScanLine(y - 1));
            const auto *current = reinterpret_cast<const QRgb *>(source.constScanLine(y));
            const auto *below = reinterpret_cast<const QRgb *>(source.constScanLine(y + 1));
            for (int x = 1; x < output.width() - 1; ++x) {
                const QRgb center = current[x];
                const int redEdges = 4 * qRed(center) - qRed(current[x - 1])
                    - qRed(current[x + 1]) - qRed(above[x]) - qRed(below[x]);
                const int greenEdges = 4 * qGreen(center) - qGreen(current[x - 1])
                    - qGreen(current[x + 1]) - qGreen(above[x]) - qGreen(below[x]);
                const int blueEdges = 4 * qBlue(center) - qBlue(current[x - 1])
                    - qBlue(current[x + 1]) - qBlue(above[x]) - qBlue(below[x]);
                destination[x] = qRgba(
                    clampChannel(qRed(center) + 0.15 * redEdges),
                    clampChannel(qGreen(center) + 0.15 * greenEdges),
                    clampChannel(qBlue(center) + 0.15 * blueEdges), qAlpha(center));
            }
        }
    }
    return output;
}
