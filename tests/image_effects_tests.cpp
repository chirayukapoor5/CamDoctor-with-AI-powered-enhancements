#include "image_effects.h"

#include <QtTest>

class ImageEffectsTests final : public QObject {
    Q_OBJECT

private slots:
    void grayscaleUsesPixelLuminance()
    {
        QImage image(1, 1, QImage::Format_RGB32);
        image.setPixelColor(0, 0, QColor(50, 100, 150));
        ImageEffects effects;
        effects.grayscale = true;

        const QImage result = ImageEffectsProcessor::apply(image, effects);
        QVERIFY(!result.isNull());
        const QColor pixel = result.pixelColor(0, 0);
        QCOMPARE(pixel.red(), pixel.green());
        QCOMPARE(pixel.green(), pixel.blue());
        QCOMPARE(pixel.red(), 93);
    }

    void adjustmentsAreBoundedAndDeterministic()
    {
        QImage image(3, 2, QImage::Format_RGB32);
        image.fill(QColor(80, 120, 160));
        ImageEffects effects;
        effects.brightness = 20;
        effects.contrast = 1.2;
        effects.saturation = 1.4;
        effects.warmth = 15;
        effects.mildSharpening = true;

        const QImage preview = ImageEffectsProcessor::apply(image, effects);
        const QImage saved = ImageEffectsProcessor::apply(image, effects);
        ImageEffects withoutWarmth = effects;
        withoutWarmth.warmth = 0;
        const QImage neutralTone = ImageEffectsProcessor::apply(image, withoutWarmth);
        QVERIFY(!preview.isNull());
        QCOMPARE(preview, saved);
        QVERIFY(preview.pixelColor(1, 0).red() > neutralTone.pixelColor(1, 0).red());
        QVERIFY(preview.pixelColor(1, 0).blue() < neutralTone.pixelColor(1, 0).blue());
    }

    void brightnessAdjustmentHasExpectedPixelOutput()
    {
        QImage image(1, 1, QImage::Format_RGB32);
        image.fill(QColor(80, 80, 80));
        ImageEffects effects;
        effects.brightness = 20;

        const QImage result = ImageEffectsProcessor::apply(image, effects);
        QCOMPARE(result.pixelColor(0, 0), QColor(100, 100, 100));
    }

    void sharpeningAddsOnlyMildEdgeEmphasis()
    {
        QImage image(5, 5, QImage::Format_RGB32);
        image.fill(QColor(128, 128, 128));
        image.setPixelColor(2, 2, QColor(180, 180, 180));
        ImageEffects effects;
        effects.mildSharpening = true;

        const QImage result = ImageEffectsProcessor::apply(image, effects);
        QVERIFY(result.pixelColor(2, 2).red() > 180);
        QVERIFY(result.pixelColor(2, 2).red() <= 220);
    }

    void invalidImagesAndEffectsAreUnavailable()
    {
        QVERIFY(ImageEffectsProcessor::apply({}, {}).isNull());
        ImageEffects effects;
        effects.contrast = 3.0;
        QString error;
        QVERIFY(!ImageEffectsProcessor::validate(effects, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(ImageEffectsProcessor::apply(QImage(2, 2, QImage::Format_RGB32), effects).isNull());
    }
};

QTEST_GUILESS_MAIN(ImageEffectsTests)
#include "image_effects_tests.moc"
