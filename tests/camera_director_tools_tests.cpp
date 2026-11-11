#include "camera_director_tools.h"

#include <QtTest>

namespace {
CameraControl integerControl(const QString &id, qint64 value, qint64 minimum,
                             qint64 maximum, qint64 step = 1)
{
    CameraControl control;
    control.id = id;
    control.name = id;
    control.type = CameraControlType::Integer;
    control.value = value;
    control.minimum = minimum;
    control.maximum = maximum;
    control.step = step;
    return control;
}
} // namespace

class CameraDirectorToolsTests final : public QObject {
    Q_OBJECT

private slots:
    void registryIsBoundedAndDoesNotExposeCapture()
    {
        const QJsonArray definitions = CameraDirectorTools::definitions();
        QCOMPARE(definitions.size(), 8);
        for (const auto &definition : definitions) {
            const auto function = definition.toObject().value(QStringLiteral("function")).toObject();
            QVERIFY(!function.value(QStringLiteral("name")).toString().isEmpty());
            const auto parameters = function.value(QStringLiteral("parameters")).toObject();
            QCOMPARE(parameters.value(QStringLiteral("additionalProperties")).toBool(), false);
            QVERIFY(function.value(QStringLiteral("name")).toString()
                    != QLatin1String("capture_photo"));
        }
    }

    void exposureRequiresAnAdvertisedInRangeValue()
    {
        const QVector<CameraControl> controls{integerControl(QStringLiteral("exposure"), 0, -10, 10, 2)};
        QVERIFY(!CameraDirectorTools::validate(
            QStringLiteral("set_exposure"), {{QStringLiteral("value"), 1}}, {}, {}).valid);
        const auto valid = CameraDirectorTools::validate(
            QStringLiteral("set_exposure"), {{QStringLiteral("value"), -4}}, controls, {});
        QVERIFY(valid.valid);
        QCOMPARE(valid.action.controlValue.value, -4);
        QVERIFY(!valid.action.controlValue.automatic);

        QVERIFY(!CameraDirectorTools::validate(
            QStringLiteral("set_exposure"), {{QStringLiteral("value"), 3}}, controls, {}).valid);
        QVERIFY(!CameraDirectorTools::validate(
            QStringLiteral("set_exposure"), {{QStringLiteral("value"), 12}}, controls, {}).valid);
        QVERIFY(!CameraDirectorTools::validate(
            QStringLiteral("set_exposure"),
            {{QStringLiteral("value"), 4}, {QStringLiteral("shell"), QStringLiteral("no")}},
            controls, {}).valid);
    }

    void focusUnavailableAndCameraDisconnectedAreRejected()
    {
        QVERIFY(!CameraDirectorTools::validate(
            QStringLiteral("set_focus_mode"), {{QStringLiteral("mode"), QStringLiteral("auto")}},
            {}, {}).valid);

        CameraControl focus;
        focus.id = QStringLiteral("focus-auto");
        focus.type = CameraControlType::Boolean;
        focus.value = 0;
        const auto result = CameraDirectorTools::validate(
            QStringLiteral("set_focus_mode"), {{QStringLiteral("mode"), QStringLiteral("auto")}},
            {focus}, {});
        QVERIFY(result.valid);
        QCOMPARE(result.action.controlValue.value, 1);

        CameraControl menuFocus;
        menuFocus.id = QStringLiteral("focus");
        menuFocus.type = CameraControlType::Menu;
        menuFocus.options = {{1, QStringLiteral("Auto")}, {2, QStringLiteral("Manual")}};
        const auto manual = CameraDirectorTools::validate(
            QStringLiteral("set_focus_mode"), {{QStringLiteral("mode"), QStringLiteral("manual")}},
            {menuFocus}, {});
        QVERIFY(manual.valid);
        QCOMPARE(manual.action.controlValue.value, 2);
    }

    void whiteBalanceRequiresSupportedControlAndValidMode()
    {
        CameraControl automatic;
        automatic.id = QStringLiteral("white-balance-auto");
        automatic.type = CameraControlType::Boolean;
        const auto autoResult = CameraDirectorTools::validate(
            QStringLiteral("set_white_balance"), {{QStringLiteral("value"), QStringLiteral("auto")}},
            {automatic}, {});
        QVERIFY(autoResult.valid);
        QCOMPARE(autoResult.action.controlValue.value, 1);

        const auto unavailable = CameraDirectorTools::validate(
            QStringLiteral("set_white_balance"), {{QStringLiteral("value"), 4500}}, {}, {});
        QVERIFY(!unavailable.valid);
    }

    void imageEffectsRejectMalformedAndOutOfRangeArguments()
    {
        const QJsonObject validArguments{
            {QStringLiteral("grayscale"), true},
            {QStringLiteral("brightness"), 10},
            {QStringLiteral("contrast"), 1.1},
            {QStringLiteral("saturation"), 1.0},
            {QStringLiteral("warmth"), -5},
            {QStringLiteral("mild_sharpening"), false},
        };
        const auto result = CameraDirectorTools::validate(
            QStringLiteral("apply_image_effects"), validArguments, {}, {});
        QVERIFY(result.valid);
        QVERIFY(result.action.effects.grayscale);
        QCOMPARE(result.action.effects.brightness, 10);

        QJsonObject invalidArguments = validArguments;
        invalidArguments.insert(QStringLiteral("brightness"), 101);
        QVERIFY(!CameraDirectorTools::validate(
            QStringLiteral("apply_image_effects"), invalidArguments, {}, {}).valid);
        QVERIFY(!CameraDirectorTools::validate(
            QStringLiteral("apply_image_effects"), {{QStringLiteral("grayscale"), true}}, {}, {}).valid);
    }

    void restoreIsOnlyTheLatestSnapshotAndToolsAreWhitelisted()
    {
        const auto restore = CameraDirectorTools::validate(
            QStringLiteral("restore_settings"), {{QStringLiteral("snapshot_id"), QStringLiteral("latest")}},
            {}, {});
        QVERIFY(restore.valid);
        QVERIFY(!CameraDirectorTools::validate(
            QStringLiteral("restore_settings"),
            {{QStringLiteral("snapshot_id"), QStringLiteral("arbitrary-file")}}, {}, {}).valid);
        QVERIFY(!CameraDirectorTools::validate(
            QStringLiteral("run_shell"), {}, {}, {}).valid);
        QVERIFY(CameraDirectorTools::validate(
            QStringLiteral("get_image_metrics"), {}, {}, {}).readOnly);
    }
};

QTEST_GUILESS_MAIN(CameraDirectorToolsTests)
#include "camera_director_tools_tests.moc"
