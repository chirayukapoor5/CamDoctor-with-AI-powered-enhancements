#pragma once

#include "camera_control.h"
#include "camera_performance.h"
#include "camera_usage.h"
#include "control_backend.h"
#include "camera_director_tools.h"
#include "groq_client.h"
#include "image_quality.h"
#include "secure_credential_store.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVector>

#include <memory>

class QCamera;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QFrame;
class QScrollArea;
class QSplitter;
class QToolButton;
class QTimer;
class QVBoxLayout;
class QMenu;
class QVideoFrame;
class QVideoSink;
class ControlRow;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void buildUi();
    void refreshDevices();
    void selectCamera(int index);
    void rebuildControls();
    void clearControls();
    bool applyValue(const CameraControlValue &value, bool reportFailure = true);
    void reapplyAll();
    void saveProfile();
    void loadProfile();
    void refreshCameraUsers();
    void terminateSelectedCameraUser();
    void setStatus(const QString &message, bool error = false);
    void updatePerformanceMetrics();
    void resetImageQuality(const QString &status, const QString &detail = {});
    void analyzeImageQuality(const QImage &image, qint64 timestampNanoseconds);
    void updateImageQuality(const ImageQualityResult &result);
    void drawLuminanceHistogram(const std::array<quint64, 256> &histogram);
    [[nodiscard]] QVector<CameraControlValue> currentValues() const;
    [[nodiscard]] QVector<CameraControl> cameraControls(QString *error = nullptr) const;
    void requestDirector();
    void sendDirectorMessagesWithCredential(const QJsonArray &messages);
    void openGroqSettings();
    void handleDirectorReply(const QJsonObject &message, const QString &error);
    void renderPreview(const QImage &image);
    void startEffectsPreview(const QImage &image);
    void applyPendingDirectorActions();
    bool restoreSnapshot(const QVector<CameraControlValue> &values, const ImageEffects &effects);
    void undoDirectorChanges();
    void resetDirectorChanges();
    void capturePhoto();
    void refreshDirectorButtons();
    void beginPostAdjustmentMeasurement(const ImageQualityResult &baseline);

    struct AdjustmentSnapshot {
        QVector<CameraControlValue> values;
        ImageEffects effects;
    };

    QMediaDevices mediaDevices_;
    QMediaCaptureSession captureSession_;
    QElapsedTimer performanceClock_;
    CameraPerformance cameraPerformance_;
    ImageQualityAnalyzer imageQualityAnalyzer_;
    ImageQualitySampler imageQualitySampler_;
    QFutureWatcher<ImageQualityResult> *imageQualityWatcher_{};
    QFutureWatcher<QImage> *effectsWatcher_{};
    GroqClient *groqClient_{};
    SecureCredentialStore *credentialStore_{};
    quint64 imageQualityGeneration_{};
    quint64 imageQualityRequestGeneration_{};
    qint64 imageQualityRequestTimestampNanoseconds_{};
    qint64 postAdjustmentThresholdNanoseconds_{};
    quint64 directorContextGeneration_{};
    quint64 directorRequestGeneration_{};
    quint64 effectsGeneration_{};
    quint64 effectsRequestGeneration_{};
    QCamera *camera_{};
    std::unique_ptr<CameraControlBackend> backend_;

    QComboBox *deviceCombo_{};
    QPushButton *refreshButton_{};
    QPushButton *saveButton_{};
    QPushButton *loadButton_{};
    QPushButton *reapplyButton_{};
    QComboBox *cameraUsersCombo_{};
    QPushButton *refreshUsersButton_{};
    QPushButton *terminateUserButton_{};
    QCheckBox *autoReapplyBox_{};
    QLabel *videoLabel_{};
    QVideoSink *videoSink_{};
    QScrollArea *controlsScroll_{};
    QSplitter *splitter_{};
    QWidget *controlsContainer_{};
    QVBoxLayout *controlsLayout_{};
    QLabel *statusLabel_{};
    QLabel *emptyLabel_{};
    QLabel *performanceLabel_{};
    QLabel *imageQualityStatusLabel_{};
    QLabel *imageQualitySharpnessLabel_{};
    QLabel *imageQualityBrightnessLabel_{};
    QLabel *imageQualityBlackClippingLabel_{};
    QLabel *imageQualityWhiteClippingLabel_{};
    QLabel *imageQualityContrastLabel_{};
    QLabel *imageQualityDurationLabel_{};
    QLabel *imageQualityHistogramLabel_{};
    QLabel *imageQualityObservationsLabel_{};
    QLabel *imageQualityDetailsLabel_{};
    QToolButton *imageQualityDetailsButton_{};
    QCheckBox *groqOptInBox_{};
    QCheckBox *autoApplyDirectorBox_{};
    QLabel *groqConnectionStatusLabel_{};
    QPushButton *groqSettingsButton_{};
    QPlainTextEdit *directorPrompt_{};
    QPlainTextEdit *directorResponse_{};
    QPushButton *askDirectorButton_{};
    QPushButton *cancelDirectorButton_{};
    QPushButton *applyDirectorButton_{};
    QPushButton *undoDirectorButton_{};
    QPushButton *resetDirectorButton_{};
    QPushButton *capturePhotoButton_{};
    QTimer *reapplyTimer_{};
    QTimer *performanceTimer_{};
    QVector<ControlRow *> controlRows_;
    QVector<CameraUser> cameraUsers_;
    QVector<CameraControl> latestCameraControls_;
    QVector<DirectorAction> pendingDirectorActions_;
    QVector<AdjustmentSnapshot> directorSnapshots_;
    QVector<CameraControlValue> originalCameraValues_;
    QJsonArray directorRequestMessages_;
    ImageEffects currentImageEffects_;
    ImageEffects originalImageEffects_;
    ImageQualityResult latestImageMetrics_;
    ImageQualityResult preAdjustmentMetrics_;
    QImage latestRawFrame_;
    QImage pendingEffectsFrame_;
    bool directorToolRoundTripUsed_{};
    bool awaitingPostAdjustmentMetrics_{};
    QByteArray selectedDeviceId_;
    QString selectedDeviceName_;
    QString groqModel_{QStringLiteral("openai/gpt-oss-20b")};
    bool groqConnected_{};
    bool awaitingDirectorCredential_{};
    bool refreshing_{false};

    void displayFrame(const QVideoFrame &frame);
};
