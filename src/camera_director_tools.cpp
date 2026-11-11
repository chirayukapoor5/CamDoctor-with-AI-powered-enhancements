#include "camera_director_tools.h"

#include <QJsonValue>

#include <cmath>
#include <limits>

namespace {
QJsonObject functionTool(const QString &name, const QString &description,
                         const QJsonObject &properties, const QJsonArray &required)
{
    return {
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"), QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("description"), description},
            {QStringLiteral("parameters"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("properties"), properties},
                {QStringLiteral("required"), required},
                {QStringLiteral("additionalProperties"), false},
            }},
        }},
    };
}

QJsonObject integerProperty(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), description}};
}

QJsonObject simpleReadTool(const QString &name, const QString &description)
{
    return functionTool(name, description, {}, {});
}

bool hasOnlyKeys(const QJsonObject &object, const QStringList &allowed)
{
    for (const QString &key : object.keys()) {
        if (!allowed.contains(key)) {
            return false;
        }
    }
    return true;
}

const CameraControl *findControl(const QVector<CameraControl> &controls,
                                 const QStringList &ids)
{
    for (const auto &control : controls) {
        for (const auto &id : ids) {
            if (normalizedControlId(control.id) == id) {
                return &control;
            }
        }
    }
    return nullptr;
}

bool numberAsInteger(const QJsonValue &value, qint64 *number)
{
    if (!value.isDouble()) {
        return false;
    }
    const double parsed = value.toDouble();
    constexpr double maximumExactJsonInteger = 9007199254740991.0;
    if (!std::isfinite(parsed) || std::floor(parsed) != parsed
        || parsed < -maximumExactJsonInteger || parsed > maximumExactJsonInteger) {
        return false;
    }
    *number = static_cast<qint64>(parsed);
    return true;
}

const CameraControlOption *findModeOption(const CameraControl &control, const QString &mode)
{
    for (const auto &option : control.options) {
        if (option.label.trimmed().compare(mode, Qt::CaseInsensitive) == 0) {
            return &option;
        }
    }
    return nullptr;
}

DirectorToolValidation invalid(const QString &message)
{
    DirectorToolValidation result;
    result.error = message;
    return result;
}

DirectorToolValidation readOnly()
{
    DirectorToolValidation result;
    result.valid = true;
    result.readOnly = true;
    return result;
}

DirectorToolValidation settingAction(DirectorAction::Type type, const QString &summary,
                                     const CameraControl &control, const qint64 value,
                                     const bool automatic)
{
    if (control.readOnly || control.type == CameraControlType::Button
        || control.type == CameraControlType::Boolean
        || value < control.minimum || value > control.maximum
        || control.clamped(value) != value) {
        return invalid(QStringLiteral("%1 is not writable at the requested value.")
                           .arg(control.name));
    }
    DirectorToolValidation result;
    result.valid = true;
    result.action.type = type;
    result.action.summary = summary;
    result.action.controlId = control.id;
    result.action.controlValue = {control.id, value, automatic};
    return result;
}
} // namespace

QJsonArray CameraDirectorTools::definitions()
{
    const QJsonArray exposureRequired{QStringLiteral("value")};
    const QJsonArray focusRequired{QStringLiteral("mode")};
    const QJsonArray whiteRequired{QStringLiteral("value")};
    const QJsonArray effectsRequired{
        QStringLiteral("grayscale"), QStringLiteral("brightness"),
        QStringLiteral("contrast"), QStringLiteral("saturation"),
        QStringLiteral("warmth"), QStringLiteral("mild_sharpening")};
    const QJsonObject effectsProperties{
        {QStringLiteral("grayscale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
        {QStringLiteral("brightness"), integerProperty(QStringLiteral("Integer from -100 to 100."))},
        {QStringLiteral("contrast"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
        {QStringLiteral("saturation"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
        {QStringLiteral("warmth"), integerProperty(QStringLiteral("Integer from -40 to 40."))},
        {QStringLiteral("mild_sharpening"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
    };
    const QJsonObject whiteValue{
        {QStringLiteral("anyOf"), QJsonArray{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}},
            QJsonObject{
                {QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("manual")}},
            },
        }},
        {QStringLiteral("description"), QStringLiteral("Use auto/manual or an integer temperature supported by the camera.")},
    };

    return {
        simpleReadTool(QStringLiteral("get_camera_capabilities"),
                       QStringLiteral("Read supported controls and their ranges.")),
        simpleReadTool(QStringLiteral("get_current_settings"),
                       QStringLiteral("Read current camera settings.")),
        simpleReadTool(QStringLiteral("get_image_metrics"),
                       QStringLiteral("Read the latest locally measured image metrics; unavailable metrics must remain unavailable.")),
        functionTool(QStringLiteral("set_exposure"),
                     QStringLiteral("Propose a manual exposure value within the camera's advertised range."),
                     {{QStringLiteral("value"), integerProperty(QStringLiteral("Camera-native exposure value."))}},
                     exposureRequired),
        functionTool(QStringLiteral("set_focus_mode"),
                     QStringLiteral("Propose auto or manual focus only when supported by the selected camera."),
                     {{QStringLiteral("mode"), QJsonObject{
                         {QStringLiteral("type"), QStringLiteral("string")},
                         {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"), QStringLiteral("manual")}},
                     }}},
                     focusRequired),
        functionTool(QStringLiteral("set_white_balance"),
                     QStringLiteral("Propose automatic/manual white balance or a supported temperature."),
                     {{QStringLiteral("value"), whiteValue}}, whiteRequired),
        functionTool(QStringLiteral("apply_image_effects"),
                     QStringLiteral("Propose bounded deterministic software effects for preview and explicit photo saves."),
                     effectsProperties, effectsRequired),
        functionTool(QStringLiteral("restore_settings"),
                     QStringLiteral("Propose restoring the latest in-memory adjustment snapshot. Only snapshot_id 'latest' is accepted."),
                     {{QStringLiteral("snapshot_id"), QJsonObject{
                         {QStringLiteral("type"), QStringLiteral("string")},
                         {QStringLiteral("enum"), QJsonArray{QStringLiteral("latest")}},
                     }}},
                     {QStringLiteral("snapshot_id")}),
    };
}

DirectorToolValidation CameraDirectorTools::validate(
    const QString &name, const QJsonObject &arguments,
    const QVector<CameraControl> &controls, const ImageEffects &currentEffects)
{
    if (name == QLatin1String("get_camera_capabilities")
        || name == QLatin1String("get_current_settings")
        || name == QLatin1String("get_image_metrics")) {
        return arguments.isEmpty() ? readOnly()
                                   : invalid(QStringLiteral("Read tools do not accept arguments."));
    }

    if (name == QLatin1String("set_exposure")) {
        qint64 value = 0;
        if (!hasOnlyKeys(arguments, {QStringLiteral("value")})
            || arguments.size() != 1 || !numberAsInteger(arguments.value(QStringLiteral("value")), &value)) {
            return invalid(QStringLiteral("Exposure requires one integer value."));
        }
        const auto *control = findControl(controls, {QStringLiteral("exposure")});
        if (!control || (control->type != CameraControlType::Integer
                         && control->type != CameraControlType::Menu)) {
            return invalid(QStringLiteral("The selected camera does not expose a compatible manual exposure control."));
        }
        return settingAction(DirectorAction::Type::SetExposure,
                             QStringLiteral("Set exposure to %1").arg(value),
                             *control, value, false);
    }

    if (name == QLatin1String("set_focus_mode")) {
        if (!hasOnlyKeys(arguments, {QStringLiteral("mode")}) || arguments.size() != 1
            || !arguments.value(QStringLiteral("mode")).isString()) {
            return invalid(QStringLiteral("Focus mode requires 'auto' or 'manual'."));
        }
        const QString mode = arguments.value(QStringLiteral("mode")).toString();
        if (mode != QLatin1String("auto") && mode != QLatin1String("manual")) {
            return invalid(QStringLiteral("Focus mode must be 'auto' or 'manual'."));
        }
        const auto *autoControl = findControl(controls, {QStringLiteral("focus-auto")});
        if (autoControl && autoControl->type == CameraControlType::Boolean && !autoControl->readOnly) {
            DirectorToolValidation result;
            result.valid = true;
            result.action.type = DirectorAction::Type::SetFocusMode;
            result.action.controlId = autoControl->id;
            result.action.controlValue = {autoControl->id, mode == QLatin1String("auto") ? 1 : 0, false};
            result.action.summary = QStringLiteral("Set focus to %1").arg(mode);
            return result;
        }
        const auto *focus = findControl(controls, {QStringLiteral("focus")});
        if (!focus || focus->readOnly) {
            return invalid(QStringLiteral("Auto/manual focus mode is not exposed by this camera."));
        }
        if (focus->type == CameraControlType::Menu) {
            const auto *option = findModeOption(*focus, mode);
            if (!option) {
                return invalid(QStringLiteral("This focus control has no matching mode option."));
            }
            DirectorToolValidation result;
            result.valid = true;
            result.action.type = DirectorAction::Type::SetFocusMode;
            result.action.controlId = focus->id;
            result.action.controlValue = {focus->id, option->value, false};
            result.action.summary = QStringLiteral("Set focus to %1").arg(mode);
            return result;
        }
        if (!focus->supportsAuto) {
            return invalid(QStringLiteral("Auto/manual focus mode is not exposed by this camera."));
        }
        DirectorToolValidation result;
        result.valid = true;
        result.action.type = DirectorAction::Type::SetFocusMode;
        result.action.controlId = focus->id;
        result.action.controlValue = {focus->id, focus->value, mode == QLatin1String("auto")};
        result.action.summary = QStringLiteral("Set focus to %1").arg(mode);
        return result;
    }

    if (name == QLatin1String("set_white_balance")) {
        if (!hasOnlyKeys(arguments, {QStringLiteral("value")}) || arguments.size() != 1) {
            return invalid(QStringLiteral("White balance requires one mode or integer temperature."));
        }
        const QJsonValue value = arguments.value(QStringLiteral("value"));
        const auto *temperature = findControl(controls, {QStringLiteral("white-balance")});
        if (value.isString()) {
            const QString mode = value.toString();
            if (mode != QLatin1String("auto") && mode != QLatin1String("manual")) {
                return invalid(QStringLiteral("White-balance mode must be 'auto' or 'manual'."));
            }
            const auto *autoControl = findControl(controls, {QStringLiteral("white-balance-auto")});
            if (autoControl && autoControl->type == CameraControlType::Boolean && !autoControl->readOnly) {
                DirectorToolValidation result;
                result.valid = true;
                result.action.type = DirectorAction::Type::SetWhiteBalance;
                result.action.controlId = autoControl->id;
                result.action.controlValue = {autoControl->id, mode == QLatin1String("auto") ? 1 : 0, false};
                result.action.summary = QStringLiteral("Set white balance to %1").arg(mode);
                return result;
            }
            if (temperature && !temperature->readOnly
                && temperature->type == CameraControlType::Menu) {
                const auto *option = findModeOption(*temperature, mode);
                if (option) {
                    DirectorToolValidation result;
                    result.valid = true;
                    result.action.type = DirectorAction::Type::SetWhiteBalance;
                    result.action.controlId = temperature->id;
                    result.action.controlValue = {temperature->id, option->value, false};
                    result.action.summary = QStringLiteral("Set white balance to %1").arg(mode);
                    return result;
                }
            }
            if (!temperature || temperature->readOnly || !temperature->supportsAuto) {
                return invalid(QStringLiteral("Automatic/manual white balance is not exposed by this camera."));
            }
            DirectorToolValidation result;
            result.valid = true;
            result.action.type = DirectorAction::Type::SetWhiteBalance;
            result.action.controlId = temperature->id;
            result.action.controlValue = {
                temperature->id, temperature->value, mode == QLatin1String("auto")};
            result.action.summary = QStringLiteral("Set white balance to %1").arg(mode);
            return result;
        }

        qint64 kelvin = 0;
        if (!numberAsInteger(value, &kelvin) || !temperature
            || temperature->type != CameraControlType::Integer) {
            return invalid(QStringLiteral("White-balance temperature is unavailable or invalid."));
        }
        return settingAction(DirectorAction::Type::SetWhiteBalance,
                             QStringLiteral("Set white-balance temperature to %1").arg(kelvin),
                             *temperature, kelvin, false);
    }

    if (name == QLatin1String("apply_image_effects")) {
        static const QStringList keys{
            QStringLiteral("grayscale"), QStringLiteral("brightness"),
            QStringLiteral("contrast"), QStringLiteral("saturation"),
            QStringLiteral("warmth"), QStringLiteral("mild_sharpening")};
        if (!hasOnlyKeys(arguments, keys) || arguments.size() != keys.size()
            || !arguments.value(QStringLiteral("grayscale")).isBool()
            || !arguments.value(QStringLiteral("mild_sharpening")).isBool()
            || !arguments.value(QStringLiteral("contrast")).isDouble()
            || !arguments.value(QStringLiteral("saturation")).isDouble()) {
            return invalid(QStringLiteral("Image-effects arguments do not match the typed tool schema."));
        }
        qint64 brightness = 0;
        qint64 warmth = 0;
        if (!numberAsInteger(arguments.value(QStringLiteral("brightness")), &brightness)
            || !numberAsInteger(arguments.value(QStringLiteral("warmth")), &warmth)) {
            return invalid(QStringLiteral("Brightness and warmth must be integers."));
        }
        if (brightness < -100 || brightness > 100 || warmth < -40 || warmth > 40) {
            return invalid(QStringLiteral("Brightness or warmth is outside the supported range."));
        }
        DirectorAction action;
        action.type = DirectorAction::Type::ApplyImageEffects;
        action.effects = {
            arguments.value(QStringLiteral("grayscale")).toBool(),
            static_cast<int>(brightness),
            arguments.value(QStringLiteral("contrast")).toDouble(),
            arguments.value(QStringLiteral("saturation")).toDouble(),
            static_cast<int>(warmth),
            arguments.value(QStringLiteral("mild_sharpening")).toBool(),
        };
        QString error;
        if (!ImageEffectsProcessor::validate(action.effects, &error)) {
            return invalid(error);
        }
        if (action.effects == currentEffects) {
            action.summary = QStringLiteral("Keep the current software effects");
        } else {
            action.summary = QStringLiteral("Update deterministic image effects");
        }
        DirectorToolValidation result;
        result.valid = true;
        result.action = action;
        return result;
    }

    if (name == QLatin1String("restore_settings")) {
        if (!hasOnlyKeys(arguments, {QStringLiteral("snapshot_id")}) || arguments.size() != 1
            || arguments.value(QStringLiteral("snapshot_id")).toString() != QLatin1String("latest")) {
            return invalid(QStringLiteral("Only the latest in-memory adjustment snapshot can be restored."));
        }
        DirectorToolValidation result;
        result.valid = true;
        result.action.type = DirectorAction::Type::RestoreSettings;
        result.action.summary = QStringLiteral("Restore the latest adjustment snapshot");
        return result;
    }

    return invalid(QStringLiteral("Tool is not in the local allowlist."));
}

QJsonObject CameraDirectorTools::capabilities(const QVector<CameraControl> &controls)
{
    QJsonArray entries;
    for (const auto &control : controls) {
        QJsonArray options;
        for (const auto &option : control.options) {
            options.append(QJsonObject{
                {QStringLiteral("value"), static_cast<double>(option.value)},
                {QStringLiteral("label"), option.label},
            });
        }
        entries.append(QJsonObject{
            {QStringLiteral("id"), control.id},
            {QStringLiteral("name"), control.name},
            {QStringLiteral("type"), cameraControlTypeKey(control.type)},
            {QStringLiteral("minimum"), static_cast<double>(control.minimum)},
            {QStringLiteral("maximum"), static_cast<double>(control.maximum)},
            {QStringLiteral("step"), static_cast<double>(control.normalizedStep())},
            {QStringLiteral("supportsAuto"), control.supportsAuto},
            {QStringLiteral("readOnly"), control.readOnly},
            {QStringLiteral("options"), options},
        });
    }
    return {{QStringLiteral("controls"), entries}};
}
