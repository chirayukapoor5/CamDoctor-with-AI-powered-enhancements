#pragma once

#include <QImage>
#include <QString>

struct ImageEffects {
    bool grayscale{};
    int brightness{};
    double contrast{1.0};
    double saturation{1.0};
    int warmth{};
    bool mildSharpening{};

    friend bool operator==(const ImageEffects &, const ImageEffects &) = default;
};

class ImageEffectsProcessor {
public:
    [[nodiscard]] static bool validate(const ImageEffects &effects, QString *error = nullptr);
    [[nodiscard]] static QImage apply(const QImage &image, const ImageEffects &effects);
};
