#pragma once

#include "camera_control.h"
#include "image_effects.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct DirectorAction {
    enum class Type {
        SetExposure,
        SetFocusMode,
        SetWhiteBalance,
        ApplyImageEffects,
        RestoreSettings,
    };

    Type type{Type::ApplyImageEffects};
    QString summary;
    QString controlId;
    CameraControlValue controlValue;
    ImageEffects effects;
};

struct DirectorToolValidation {
    bool valid{};
    bool readOnly{};
    QString error;
    DirectorAction action;
};

class CameraDirectorTools {
public:
    [[nodiscard]] static QJsonArray definitions();
    [[nodiscard]] static DirectorToolValidation validate(
        const QString &name, const QJsonObject &arguments,
        const QVector<CameraControl> &controls, const ImageEffects &currentEffects);
    [[nodiscard]] static QJsonObject capabilities(const QVector<CameraControl> &controls);
};
