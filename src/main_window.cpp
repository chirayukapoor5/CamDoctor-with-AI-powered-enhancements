#include "main_window.h"

#include "control_row.h"
#include "image_effects.h"
#include "profile_store.h"
#include "groq_settings_dialog.h"
#ifdef Q_OS_WIN
#include "windows_camera_rotation.h"
#endif

#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QCheckBox>
#include <QComboBox>
#include <QAction>
#include <QFileDialog>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoFrameFormat>
#include <QVideoFrame>
#include <QVideoSink>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <new>
#include <utility>

namespace {
constexpr auto kSettingsOrganization = "Tadas";
constexpr auto kSettingsApplication = "WebcamSettings";

QString suggestedProfilePath(const QString &deviceName)
{
    QString safeName = deviceName;
    safeName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._ -]")), QStringLiteral("_"));
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents + QLatin1Char('/') + safeName.trimmed() + QStringLiteral(".webcam.json");
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), backend_(createPlatformBackend())
{
    performanceClock_.start();
    groqClient_ = new GroqClient(this);
    credentialStore_ = new SecureCredentialStore(this);
    QSettings settings(QString::fromLatin1(kSettingsOrganization),
                       QString::fromLatin1(kSettingsApplication));
    groqModel_ = settings.value(QStringLiteral("groq/model"),
                                QStringLiteral("openai/gpt-oss-20b")).toString();
    buildUi();

    performanceTimer_ = new QTimer(this);
    performanceTimer_->setInterval(500);
    connect(performanceTimer_, &QTimer::timeout, this, [this] { updatePerformanceMetrics(); });
    performanceTimer_->start();

    imageQualityWatcher_ = new QFutureWatcher<ImageQualityResult>(this);
    connect(imageQualityWatcher_, &QFutureWatcher<ImageQualityResult>::finished, this, [this] {
        imageQualitySampler_.finishAnalysis();
        const ImageQualityResult result = imageQualityWatcher_->result();
        if (imageQualityRequestGeneration_ == imageQualityGeneration_ && camera_) {
            updateImageQuality(result);
        }
    });
    connect(autoApplyDirectorBox_, &QCheckBox::toggled, this, [](const bool enabled) {
        QSettings settings(QString::fromLatin1(kSettingsOrganization),
                           QString::fromLatin1(kSettingsApplication));
        settings.setValue(QStringLiteral("groq/autoApply"), enabled);
    });

    effectsWatcher_ = new QFutureWatcher<QImage>(this);
    connect(effectsWatcher_, &QFutureWatcher<QImage>::finished, this, [this] {
        const QImage rendered = effectsWatcher_->result();
        if (effectsRequestGeneration_ == effectsGeneration_ && camera_ && !rendered.isNull()) {
            const QSize target = videoLabel_->contentsRect().size();
            videoLabel_->setPixmap(QPixmap::fromImage(rendered).scaled(
                target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        if (!pendingEffectsFrame_.isNull()) {
            const QImage newest = std::exchange(pendingEffectsFrame_, {});
            startEffectsPreview(newest);
        }
    });

    connect(groqClient_, &GroqClient::finished, this,
            [this](const QJsonObject &message, const QString &error) {
                cancelDirectorButton_->setEnabled(false);
                handleDirectorReply(message, error);
                refreshDirectorButtons();
            });
    connect(credentialStore_, &SecureCredentialStore::readFinished, this,
            [this](const bool success, const QString &secret, const QString &error) {
                if (!awaitingDirectorCredential_) {
                    return;
                }
                awaitingDirectorCredential_ = false;
                if (directorRequestGeneration_ != directorContextGeneration_) {
                    refreshDirectorButtons();
                    return;
                }
                if (!success) {
                    groqConnected_ = false;
                    groqConnectionStatusLabel_->setText(
                        tr("Connect your Groq account to use AI Camera Director."));
                    directorRequestMessages_ = {};
                    directorResponse_->setPlainText(error);
                    setStatus(error, true);
                    refreshDirectorButtons();
                    return;
                }
                QString requestError;
                if (!groqClient_->request(directorRequestMessages_,
                                          CameraDirectorTools::definitions(),
                                          secret, groqModel_, &requestError)) {
                    directorRequestMessages_ = {};
                    directorResponse_->setPlainText(requestError);
                    setStatus(requestError, true);
                    refreshDirectorButtons();
                    return;
                }
                directorResponse_->setPlainText(tr("Waiting for Groq…"));
                refreshDirectorButtons();
            });

    reapplyTimer_ = new QTimer(this);
    reapplyTimer_->setInterval(5000);
    connect(reapplyTimer_, &QTimer::timeout, this, [this] { reapplyAll(); });

    connect(&mediaDevices_, &QMediaDevices::videoInputsChanged, this, [this] { refreshDevices(); });
    refreshDevices();
}

MainWindow::~MainWindow()
{
    if (effectsWatcher_) {
        effectsWatcher_->waitForFinished();
    }
    if (camera_) {
        camera_->stop();
    }
    if (backend_) {
        backend_->close();
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Webcam Settings"));
    resize(1120, 720);
    setMinimumSize(760, 500);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);

    auto *toolbar = new QHBoxLayout;
    deviceCombo_ = new QComboBox(central);
    deviceCombo_->setMinimumWidth(280);
    refreshButton_ = new QPushButton(tr("Refresh"), central);
    saveButton_ = new QPushButton(tr("Save profile"), central);
    loadButton_ = new QPushButton(tr("Load profile"), central);
    reapplyButton_ = new QPushButton(tr("Reapply now"), central);
    autoReapplyBox_ = new QCheckBox(tr("Reapply every 5 seconds"), central);

    toolbar->addWidget(new QLabel(tr("Camera:"), central));
    toolbar->addWidget(deviceCombo_, 1);
    toolbar->addWidget(refreshButton_);
    toolbar->addSpacing(8);
    toolbar->addWidget(saveButton_);
    toolbar->addWidget(loadButton_);
    toolbar->addWidget(reapplyButton_);
    toolbar->addWidget(autoReapplyBox_);
    root->addLayout(toolbar);

    auto *usageRow = new QHBoxLayout;
    cameraUsersCombo_ = new QComboBox(central);
    cameraUsersCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cameraUsersCombo_->setMinimumContentsLength(42);
    refreshUsersButton_ = new QPushButton(tr("Refresh use"), central);
    terminateUserButton_ = new QPushButton(tr("End process…"), central);
    usageRow->addWidget(new QLabel(tr("Camera use:"), central));
    usageRow->addWidget(cameraUsersCombo_, 1);
    usageRow->addWidget(refreshUsersButton_);
    usageRow->addWidget(terminateUserButton_);
    root->addLayout(usageRow);

    splitter_ = new QSplitter(Qt::Horizontal, central);
    auto *previewPanel = new QWidget(splitter_);
    auto *previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(8);

    videoLabel_ = new QLabel(tr("Starting camera preview…"), previewPanel);
    videoLabel_->setMinimumSize(420, 300);
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLabel_->setStyleSheet(QStringLiteral(
        "background: #111318; color: #aeb4bf; border-radius: 8px; padding: 8px;"));
    videoSink_ = new QVideoSink(this);
    connect(videoSink_, &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame &frame) { displayFrame(frame); });

    auto *performanceDashboard = new QFrame(previewPanel);
    performanceDashboard->setObjectName(QStringLiteral("performanceDashboard"));
    performanceDashboard->setFrameShape(QFrame::StyledPanel);
    performanceDashboard->setStyleSheet(QStringLiteral(
        "QFrame#performanceDashboard { background: palette(alternate-base); "
        "border: 1px solid palette(midlight); border-radius: 6px; }"));
    auto *performanceLayout = new QHBoxLayout(performanceDashboard);
    performanceLayout->setContentsMargins(12, 8, 12, 8);
    performanceLayout->setSpacing(12);
    auto *performanceHeading = new QLabel(tr("Preview performance"), performanceDashboard);
    performanceHeading->setStyleSheet(QStringLiteral("font-weight: 600;"));
    performanceLabel_ = new QLabel(
        tr("FPS  —   |   INTERVAL  —   |   JITTER  —   |   LATE  —"), performanceDashboard);
    performanceLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    performanceLabel_->setToolTip(tr(
        "Metrics describe frames delivered to this application's preview. A late interval "
        "is an observed frame arrival interval more than 1.5 times the camera format's "
        "nominal interval. The late count is unavailable if the camera does not report a "
        "frame rate, and it does not confirm a camera or hardware frame drop."));
    performanceLayout->addWidget(performanceHeading);
    performanceLayout->addWidget(performanceLabel_, 1);
    previewLayout->addWidget(videoLabel_, 1);
    previewLayout->addWidget(performanceDashboard);

    controlsContainer_ = new QWidget(splitter_);
    controlsLayout_ = new QVBoxLayout(controlsContainer_);
    controlsLayout_->setContentsMargins(12, 8, 12, 8);
    controlsLayout_->setSpacing(2);
    emptyLabel_ = new QLabel(tr("Select a camera to discover its controls."), controlsContainer_);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    controlsLayout_->addWidget(emptyLabel_);

    auto *imageQualityPanel = new QFrame(controlsContainer_);
    imageQualityPanel->setObjectName(QStringLiteral("imageQualityPanel"));
    imageQualityPanel->setFrameShape(QFrame::StyledPanel);
    imageQualityPanel->setStyleSheet(QStringLiteral(
        "QFrame#imageQualityPanel { background: palette(alternate-base); "
        "border: 1px solid palette(midlight); border-radius: 6px; }"));
    auto *imageQualityLayout = new QVBoxLayout(imageQualityPanel);
    imageQualityLayout->setContentsMargins(10, 8, 10, 8);
    imageQualityLayout->setSpacing(5);

    auto *imageQualityHeader = new QHBoxLayout;
    auto *imageQualityTitle = new QLabel(tr("Image Quality"), imageQualityPanel);
    imageQualityTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
    imageQualityStatusLabel_ = new QLabel(tr("Waiting for frames"), imageQualityPanel);
    imageQualityStatusLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    imageQualityHeader->addWidget(imageQualityTitle);
    imageQualityHeader->addWidget(imageQualityStatusLabel_, 1);
    imageQualityLayout->addLayout(imageQualityHeader);

    auto *imageQualityMetrics = new QGridLayout;
    imageQualityMetrics->setHorizontalSpacing(8);
    imageQualityMetrics->setVerticalSpacing(3);
    const auto addMetric = [imageQualityMetrics, imageQualityPanel](
                               const int row, const int column, const QString &title,
                               QLabel **value) {
        auto *label = new QLabel(title, imageQualityPanel);
        *value = new QLabel(QStringLiteral("—"), imageQualityPanel);
        (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse);
        imageQualityMetrics->addWidget(label, row, column * 2);
        imageQualityMetrics->addWidget(*value, row, column * 2 + 1);
    };
    addMetric(0, 0, tr("Sharpness (Laplacian variance)"), &imageQualitySharpnessLabel_);
    addMetric(1, 0, tr("Mean luminance"), &imageQualityBrightnessLabel_);
    addMetric(2, 0, tr("Black clipping"), &imageQualityBlackClippingLabel_);
    addMetric(3, 0, tr("White clipping"), &imageQualityWhiteClippingLabel_);
    addMetric(4, 0, tr("Contrast (P95 − P5)"), &imageQualityContrastLabel_);
    addMetric(5, 0, tr("Analysis duration"), &imageQualityDurationLabel_);
    imageQualityMetrics->setColumnStretch(0, 1);
    imageQualityLayout->addLayout(imageQualityMetrics);

    auto *histogramHeading = new QLabel(tr("Luminance histogram (0–255)"), imageQualityPanel);
    imageQualityHistogramLabel_ = new QLabel(tr("Waiting for frames"), imageQualityPanel);
    imageQualityHistogramLabel_->setFixedHeight(92);
    imageQualityHistogramLabel_->setMinimumWidth(256);
    imageQualityHistogramLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    imageQualityLayout->addWidget(histogramHeading);
    imageQualityLayout->addWidget(imageQualityHistogramLabel_);

    imageQualityObservationsLabel_ = new QLabel(tr("Waiting for frames"), imageQualityPanel);
    imageQualityObservationsLabel_->setWordWrap(true);
    imageQualityLayout->addWidget(imageQualityObservationsLabel_);
    imageQualityDetailsButton_ = new QToolButton(imageQualityPanel);
    imageQualityDetailsButton_->setText(tr("How measurements work"));
    imageQualityDetailsButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    imageQualityDetailsButton_->setCheckable(true);
    imageQualityDetailsButton_->setArrowType(Qt::RightArrow);
    imageQualityLayout->addWidget(imageQualityDetailsButton_, 0, Qt::AlignLeft);
    imageQualityDetailsLabel_ = new QLabel(imageQualityPanel);
    imageQualityDetailsLabel_->setWordWrap(true);
    imageQualityDetailsLabel_->setStyleSheet(QStringLiteral("color: palette(mid);"));
    imageQualityDetailsLabel_->setText(
        tr("Frames are sampled at up to 3 per second and analyzed at no more than 640×360. "
           "Luminance uses 0.2126R + 0.7152G + 0.0722B. Sharpness is the variance of the "
           "four-neighbor discrete Laplacian; contrast is the nearest-rank P95–P5 luminance "
           "range. Clipping is luminance ≤5 or ≥250. Observation thresholds are ≥5% clipping, "
           "mean luminance <45, and Laplacian variance <20. These are configurable heuristics, "
           "not diagnoses; scene texture, lighting, scaling, and camera processing affect them. "
           "No raw frames are recorded."));
    imageQualityDetailsLabel_->setVisible(false);
    imageQualityLayout->addWidget(imageQualityDetailsLabel_);
    controlsLayout_->insertWidget(0, imageQualityPanel);

    auto *directorPanel = new QFrame(controlsContainer_);
    directorPanel->setObjectName(QStringLiteral("aiCameraDirectorPanel"));
    directorPanel->setFrameShape(QFrame::StyledPanel);
    directorPanel->setStyleSheet(QStringLiteral(
        "QFrame#aiCameraDirectorPanel { background: palette(alternate-base); "
        "border: 1px solid palette(midlight); border-radius: 6px; }"));
    auto *directorLayout = new QVBoxLayout(directorPanel);
    directorLayout->setContentsMargins(10, 8, 10, 8);
    directorLayout->setSpacing(6);
    auto *directorHeading = new QLabel(tr("AI Camera Director"), directorPanel);
    directorHeading->setStyleSheet(QStringLiteral("font-weight: 600;"));
    directorLayout->addWidget(directorHeading);
    auto *privacyNotice = new QLabel(tr(
        "Optional Groq requests send your prompt, selected camera name, advertised control "
        "capabilities/current values, and numeric image metrics. Camera frames and saved "
        "photos are never uploaded. Changes need your approval unless Auto-Apply is enabled."),
        directorPanel);
    privacyNotice->setWordWrap(true);
    directorLayout->addWidget(privacyNotice);
    auto *connectionRow = new QHBoxLayout;
    groqConnectionStatusLabel_ = new QLabel(
        tr("Connect your Groq account to use AI Camera Director."), directorPanel);
    groqConnectionStatusLabel_->setWordWrap(true);
    groqSettingsButton_ = new QPushButton(tr("Groq Settings…"), directorPanel);
    connectionRow->addWidget(groqConnectionStatusLabel_, 1);
    connectionRow->addWidget(groqSettingsButton_);
    directorLayout->addLayout(connectionRow);
    groqOptInBox_ = new QCheckBox(
        tr("I agree to send prompts and this camera context to Groq"), directorPanel);
    groqOptInBox_->setChecked(false);
    directorLayout->addWidget(groqOptInBox_);
    directorPrompt_ = new QPlainTextEdit(directorPanel);
    directorPrompt_->setPlaceholderText(tr("Describe the photographic style or adjustment…"));
    directorPrompt_->setMaximumBlockCount(8);
    directorPrompt_->setFixedHeight(72);
    directorLayout->addWidget(directorPrompt_);
    auto *requestButtons = new QHBoxLayout;
    askDirectorButton_ = new QPushButton(tr("Ask Groq"), directorPanel);
    cancelDirectorButton_ = new QPushButton(tr("Cancel"), directorPanel);
    cancelDirectorButton_->setEnabled(false);
    requestButtons->addWidget(askDirectorButton_);
    requestButtons->addWidget(cancelDirectorButton_);
    directorLayout->addLayout(requestButtons);
    directorResponse_ = new QPlainTextEdit(directorPanel);
    directorResponse_->setReadOnly(true);
    directorResponse_->setPlaceholderText(tr("Assistant explanation and validated proposals appear here."));
    directorResponse_->setMaximumBlockCount(40);
    directorResponse_->setFixedHeight(110);
    directorLayout->addWidget(directorResponse_);
    auto *approvalRow = new QHBoxLayout;
    applyDirectorButton_ = new QPushButton(tr("Apply Settings"), directorPanel);
    undoDirectorButton_ = new QPushButton(tr("Undo"), directorPanel);
    resetDirectorButton_ = new QPushButton(tr("Reset to Original"), directorPanel);
    approvalRow->addWidget(applyDirectorButton_);
    approvalRow->addWidget(undoDirectorButton_);
    approvalRow->addWidget(resetDirectorButton_);
    directorLayout->addLayout(approvalRow);
    autoApplyDirectorBox_ = new QCheckBox(tr("Auto-Apply validated proposals"), directorPanel);
    autoApplyDirectorBox_->setChecked(
        QSettings(QString::fromLatin1(kSettingsOrganization),
                  QString::fromLatin1(kSettingsApplication))
            .value(QStringLiteral("groq/autoApply"), false).toBool());
    directorLayout->addWidget(autoApplyDirectorBox_);
    capturePhotoButton_ = new QPushButton(tr("Save preview photo…"), directorPanel);
    capturePhotoButton_->setToolTip(tr(
        "Saves the latest local preview frame using the same software effects as the preview. "
        "This is an explicit user action; the assistant cannot capture photos."));
    capturePhotoButton_->setEnabled(false);
    directorLayout->addWidget(capturePhotoButton_);
    controlsLayout_->insertWidget(1, directorPanel);
    controlsLayout_->addStretch(1);

    controlsScroll_ = new QScrollArea(splitter_);
    controlsScroll_->setMinimumWidth(440);
    controlsScroll_->setWidgetResizable(true);
    controlsScroll_->setFrameShape(QFrame::NoFrame);
    controlsScroll_->setWidget(controlsContainer_);
    splitter_->addWidget(previewPanel);
    splitter_->addWidget(controlsScroll_);
    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 2);
    splitter_->setSizes({660, 460});
    root->addWidget(splitter_, 1);

    statusLabel_ = new QLabel(central);
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);
    setCentralWidget(central);

    QMenu *settingsMenu = menuBar()->addMenu(tr("Settings"));
    QMenu *aiProviderMenu = settingsMenu->addMenu(tr("AI Provider"));
    QAction *groqSettingsAction = aiProviderMenu->addAction(tr("Groq…"));
    connect(groqSettingsAction, &QAction::triggered, this, [this] { openGroqSettings(); });

    captureSession_.setVideoSink(videoSink_);

    connect(deviceCombo_, &QComboBox::currentIndexChanged, this, [this](const int index) {
        if (!refreshing_) {
            selectCamera(index);
        }
    });
    connect(refreshButton_, &QPushButton::clicked, this, [this] { refreshDevices(); });
    connect(saveButton_, &QPushButton::clicked, this, [this] { saveProfile(); });
    connect(loadButton_, &QPushButton::clicked, this, [this] { loadProfile(); });
    connect(reapplyButton_, &QPushButton::clicked, this, [this] { reapplyAll(); });
    connect(refreshUsersButton_, &QPushButton::clicked, this, [this] { refreshCameraUsers(); });
    connect(terminateUserButton_, &QPushButton::clicked, this,
            [this] { terminateSelectedCameraUser(); });
    connect(cameraUsersCombo_, &QComboBox::currentIndexChanged, this, [this](const int index) {
        terminateUserButton_->setEnabled(index >= 0 && index < cameraUsers_.size()
                                         && cameraUsers_.at(index).pid > 0);
    });
    connect(imageQualityDetailsButton_, &QToolButton::toggled, this, [this](const bool expanded) {
        imageQualityDetailsButton_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        imageQualityDetailsLabel_->setVisible(expanded);
    });
    connect(groqOptInBox_, &QCheckBox::toggled, this, [this](const bool enabled) {
        if (!enabled) {
            if (groqClient_->isBusy()) {
                ++directorContextGeneration_;
                directorRequestMessages_ = {};
                groqClient_->cancel();
            } else if (awaitingDirectorCredential_) {
                awaitingDirectorCredential_ = false;
                directorRequestMessages_ = {};
            }
        }
        refreshDirectorButtons();
    });
    connect(askDirectorButton_, &QPushButton::clicked, this, [this] { requestDirector(); });
    connect(groqSettingsButton_, &QPushButton::clicked, this, [this] { openGroqSettings(); });
    connect(cancelDirectorButton_, &QPushButton::clicked, this, [this] {
        if (groqClient_->isBusy()) {
            groqClient_->cancel();
        } else if (awaitingDirectorCredential_) {
            awaitingDirectorCredential_ = false;
            directorRequestMessages_ = {};
            directorResponse_->setPlainText(tr("Groq request cancelled."));
            refreshDirectorButtons();
        }
    });
    connect(applyDirectorButton_, &QPushButton::clicked, this,
            [this] { applyPendingDirectorActions(); });
    connect(undoDirectorButton_, &QPushButton::clicked, this,
            [this] { undoDirectorChanges(); });
    connect(resetDirectorButton_, &QPushButton::clicked, this,
            [this] { resetDirectorChanges(); });
    connect(capturePhotoButton_, &QPushButton::clicked, this,
            [this] { capturePhoto(); });
    connect(autoReapplyBox_, &QCheckBox::toggled, this, [this](const bool enabled) {
        if (enabled) {
            reapplyTimer_->start();
        } else {
            reapplyTimer_->stop();
        }
    });

    saveButton_->setEnabled(false);
    loadButton_->setEnabled(false);
    reapplyButton_->setEnabled(false);
    autoReapplyBox_->setEnabled(false);
    terminateUserButton_->setEnabled(false);
    applyDirectorButton_->setEnabled(false);
    undoDirectorButton_->setEnabled(false);
    resetDirectorButton_->setEnabled(false);
    askDirectorButton_->setEnabled(false);
    refreshDirectorButtons();
}

void MainWindow::refreshDevices()
{
    QByteArray selectedId = deviceCombo_->currentData().toByteArray();
    if (selectedId.isEmpty()) {
        QSettings settings(QString::fromLatin1(kSettingsOrganization), QString::fromLatin1(kSettingsApplication));
        selectedId = settings.value(QStringLiteral("lastDeviceId")).toByteArray();
    }
    refreshing_ = true;
    deviceCombo_->clear();

    const auto devices = QMediaDevices::videoInputs();
    for (const auto &device : devices) {
        deviceCombo_->addItem(device.description(), device.id());
    }

    int selectedIndex = -1;
    for (int i = 0; i < deviceCombo_->count(); ++i) {
        if (deviceCombo_->itemData(i).toByteArray() == selectedId) {
            selectedIndex = i;
            break;
        }
    }
    if (selectedIndex < 0 && !devices.isEmpty()) {
        selectedIndex = 0;
    }
    deviceCombo_->setCurrentIndex(selectedIndex);
    refreshing_ = false;

    if (selectedIndex >= 0) {
        selectCamera(selectedIndex);
    } else {
        selectCamera(-1);
        setStatus(tr("No video cameras were found."), true);
    }
}

void MainWindow::selectCamera(const int index)
{
    ++directorContextGeneration_;
    directorRequestMessages_ = {};
    if (groqClient_->isBusy()) {
        groqClient_->cancel();
    }
    resetImageQuality(tr("Waiting for frames"));
    latestRawFrame_ = {};
    pendingEffectsFrame_ = {};
    ++effectsGeneration_;
    currentImageEffects_ = {};
    originalImageEffects_ = {};
    pendingDirectorActions_.clear();
    directorSnapshots_.clear();
    originalCameraValues_.clear();
    latestCameraControls_.clear();
    capturePhotoButton_->setEnabled(false);
    directorResponse_->clear();
    refreshDirectorButtons();
    clearControls();
    if (camera_) {
        camera_->stop();
        captureSession_.setCamera(nullptr);
        delete camera_;
        camera_ = nullptr;
    }
    cameraPerformance_.reset();
    updatePerformanceMetrics();
    backend_->close();
    selectedDeviceId_.clear();
    selectedDeviceName_.clear();
    cameraUsers_.clear();
    cameraUsersCombo_->clear();
    terminateUserButton_->setEnabled(false);

    const auto devices = QMediaDevices::videoInputs();
    if (index < 0 || index >= devices.size()) {
        resetImageQuality(tr("Unavailable"), tr("No camera is selected."));
        saveButton_->setEnabled(false);
        loadButton_->setEnabled(false);
        reapplyButton_->setEnabled(false);
        autoReapplyBox_->setEnabled(false);
        refreshUsersButton_->setEnabled(false);
        refreshDirectorButtons();
        return;
    }

    const auto device = devices.at(index);
    selectedDeviceId_ = device.id();
    selectedDeviceName_ = device.description();
    refreshUsersButton_->setEnabled(true);
    refreshCameraUsers();
    camera_ = new QCamera(device, this);
    const auto formats = device.videoFormats();
    if (!formats.isEmpty()) {
        // A settings preview values reliability over capture resolution. 720p30
        // avoids expensive hardware transforms selected by some Windows drivers.
        const QSize target(1280, 720);
        const auto best = std::min_element(formats.cbegin(), formats.cend(), [&target](const auto &left, const auto &right) {
            const auto score = [&target](const QCameraFormat &format) {
                const QSize resolution = format.resolution();
                const qint64 areaDifference = std::abs(
                    static_cast<qint64>(resolution.width()) * resolution.height()
                    - static_cast<qint64>(target.width()) * target.height());
                const qint64 oversizedPenalty =
                    (resolution.width() > target.width() || resolution.height() > target.height()) ? 10'000'000 : 0;
                const qint64 lowFrameRatePenalty = format.maxFrameRate() < 29.0 ? 5'000'000 : 0;
                const qint64 frameRateDifference = static_cast<qint64>(
                    std::abs(format.maxFrameRate() - 30.0) * 100'000.0);
                const qint64 uncompressedPenalty =
                    format.pixelFormat() == QVideoFrameFormat::Format_Jpeg ? 0 : 20'000'000;
                return areaDifference + oversizedPenalty + lowFrameRatePenalty
                    + frameRateDifference + uncompressedPenalty;
            };
            return score(left) < score(right);
        });
        camera_->setCameraFormat(*best);
    }
    cameraPerformance_.reset(camera_->cameraFormat().maxFrameRate());
    updatePerformanceMetrics();
    captureSession_.setCamera(camera_);
    connect(camera_, &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString &description) {
                latestRawFrame_ = {};
                pendingEffectsFrame_ = {};
                capturePhotoButton_->setEnabled(false);
                resetImageQuality(tr("Unavailable"),
                                  tr("Capture failed: %1").arg(description));
                if (description.contains(QStringLiteral("lack of hardware resources"),
                                         Qt::CaseInsensitive)) {
                    videoLabel_->setText(tr(
                        "Preview unavailable because another application is using the camera.\n"
                        "You can still change and save controls here."));
                    setStatus(tr("Camera preview is busy in another application; controls remain available."),
                              true);
                    refreshCameraUsers();
                } else {
                    videoLabel_->setText(tr("Camera preview unavailable."));
                    setStatus(description, true);
                }
            });
    QString error;
    if (!backend_->open(device.id(), device.description(), &error)) {
        camera_->start();
        setStatus(tr("Preview started, but camera controls could not be opened: %1").arg(error), true);
        return;
    }

    QSettings settings(QString::fromLatin1(kSettingsOrganization), QString::fromLatin1(kSettingsApplication));
    settings.setValue(QStringLiteral("lastDeviceId"), device.id());
    rebuildControls();
    camera_->start();
}

void MainWindow::refreshCameraUsers()
{
    cameraUsers_.clear();
    cameraUsersCombo_->clear();
    terminateUserButton_->setEnabled(false);
    if (selectedDeviceId_.isEmpty()) {
        cameraUsersCombo_->addItem(tr("Select a camera first"));
        return;
    }

    QString error;
    cameraUsers_ = CameraUsageInspector::users(selectedDeviceId_, &error);
    for (const CameraUser &user : std::as_const(cameraUsers_)) {
        const QString label = user.executable.isEmpty()
            ? tr("%1 (PID %2)").arg(user.name).arg(user.pid)
            : tr("%1 (PID %2) — %3").arg(user.name).arg(user.pid).arg(user.executable);
        cameraUsersCombo_->addItem(label);
        cameraUsersCombo_->setItemData(cameraUsersCombo_->count() - 1, user.detail, Qt::ToolTipRole);
    }
    if (cameraUsers_.isEmpty()) {
        cameraUsersCombo_->addItem(error.isEmpty()
                                       ? tr("No other active camera process detected")
                                       : error);
        cameraUsersCombo_->setToolTip(error);
    } else {
        cameraUsersCombo_->setToolTip(cameraUsers_.first().detail);
        terminateUserButton_->setEnabled(cameraUsers_.first().pid > 0);
    }
}

void MainWindow::terminateSelectedCameraUser()
{
    const int index = cameraUsersCombo_->currentIndex();
    if (index < 0 || index >= cameraUsers_.size()) {
        return;
    }
    const CameraUser user = cameraUsers_.at(index);
    const auto answer = QMessageBox::warning(
        this, tr("End camera process?"),
        tr("End %1 (PID %2)?\n\nUnsaved work in that application may be lost. The process will be terminated immediately.")
            .arg(user.name).arg(user.pid),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!CameraUsageInspector::terminate(user.pid, &error)) {
        QMessageBox::warning(this, tr("Could not end process"), error);
        refreshCameraUsers();
        return;
    }
    setStatus(tr("Ended %1 (PID %2). Retrying camera preview…").arg(user.name).arg(user.pid));
    QTimer::singleShot(750, this, [this] {
        refreshCameraUsers();
        if (camera_) {
            camera_->stop();
            camera_->start();
        }
    });
}

void MainWindow::rebuildControls()
{
    clearControls();
    QString error;
    const auto controls = backend_->controls(&error);
    latestCameraControls_ = controls;
    if (!error.isEmpty()) {
        setStatus(error, true);
    }

    emptyLabel_->setVisible(controls.isEmpty());
    if (controls.isEmpty()) {
        emptyLabel_->setText(tr("This camera exposes no compatible controls."));
    }

    for (const auto &control : controls) {
        auto *row = new ControlRow(control, controlsContainer_);
#ifdef Q_OS_WIN
        if (control.id == QLatin1String(WindowsCameraRotation::controlId)) {
            row->setToolTip(tr("Rotates video for apps using the Windows camera pipeline. "
                               "Windows may ask for administrator access. Reopen the camera in other apps "
                               "after changing this setting; reconnect the webcam if needed."));
        }
#endif
        row->setChangeHandler([this](const CameraControlValue &value) { applyValue(value); });
        controlsLayout_->insertWidget(controlsLayout_->count() - 1, row);
        controlRows_.push_back(row);
    }
    splitter_->setSizes({660, 460});

    const bool enabled = !controls.isEmpty();
    saveButton_->setEnabled(enabled);
    loadButton_->setEnabled(enabled);
    reapplyButton_->setEnabled(enabled);
    autoReapplyBox_->setEnabled(enabled);
    if (enabled) {
        setStatus(tr("%1 controls available. Changes are applied directly to the camera.")
                      .arg(controls.size()));
    }
    originalCameraValues_ = currentValues();
    originalImageEffects_ = currentImageEffects_;
    refreshDirectorButtons();
}

void MainWindow::clearControls()
{
    for (auto *row : std::as_const(controlRows_)) {
        controlsLayout_->removeWidget(row);
        delete row;
    }
    controlRows_.clear();
    emptyLabel_->setVisible(true);
}

bool MainWindow::applyValue(const CameraControlValue &value, const bool reportFailure)
{
    QString error;
    if (!backend_->isOpen() && !backend_->open(selectedDeviceId_, selectedDeviceName_, &error)) {
        if (reportFailure) {
            setStatus(error, true);
        }
        return false;
    }
    CameraControlValue accepted;
    bool applied = false;
#ifdef Q_OS_WIN
    const bool rotation = value.id == QLatin1String(WindowsCameraRotation::controlId);
    if (rotation && !value.automatic) {
        applied = WindowsCameraRotation::writeWithElevation(selectedDeviceId_, value.value, &error);
        accepted = {value.id, value.value, false};
    } else
#endif
    {
        applied = backend_->setControl(value, &accepted, &error);
    }
    if (!applied) {
#ifdef Q_OS_WIN
        if (rotation) {
            qint64 actual = 0;
            if (WindowsCameraRotation::read(selectedDeviceId_, &actual)) {
                for (auto *row : std::as_const(controlRows_)) {
                    if (row->currentValue().id == value.id) row->setCurrentValue(actual, false, false);
                }
            }
        }
#endif
        if (reportFailure) {
            setStatus(error, true);
        }
        return false;
    }
    const auto row = std::find_if(controlRows_.cbegin(), controlRows_.cend(), [&accepted](const auto *item) {
        return item->currentValue().id == accepted.id;
    });
    QString name = accepted.id;
    if (row != controlRows_.cend()) {
        name = (*row)->controlName();
        (*row)->setCurrentValue(accepted.value, accepted.automatic, false);
    }
#ifdef Q_OS_WIN
    if (rotation) {
        if (camera_) {
            camera_->stop();
            camera_->start();
        }
        setStatus(tr("Video rotation saved. Turn the camera off and on in Google Meet or other apps. "
                     "Reconnect the webcam if the change is not visible yet."));
        return true;
    }
#endif
    setStatus(tr("Applied %1: %2%3")
                  .arg(name)
                  .arg(accepted.value)
                  .arg(accepted.automatic ? tr(" (auto)") : QString()));
    return true;
}

void MainWindow::reapplyAll()
{
    QString error;
    if (!backend_->isOpen() && !backend_->open(selectedDeviceId_, selectedDeviceName_, &error)) {
        setStatus(error, true);
        return;
    }
    int failures = 0;
    for (const auto &value : currentValues()) {
        CameraControlValue accepted;
        if (!backend_->setControl(value, &accepted, &error)) {
            ++failures;
        }
    }
    setStatus(failures == 0
                  ? tr("All camera settings reapplied.")
                  : tr("Reapply completed with %1 failed controls.").arg(failures),
              failures != 0);
}

void MainWindow::saveProfile()
{
    if (deviceCombo_->currentIndex() < 0) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save webcam profile"), suggestedProfilePath(deviceCombo_->currentText()),
        tr("Webcam profiles (*.webcam.json);;JSON files (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!ProfileStore::save(path, deviceCombo_->currentData().toByteArray(),
                            deviceCombo_->currentText(), currentValues(), &error)) {
        QMessageBox::warning(this, tr("Could not save profile"), error);
        return;
    }
    setStatus(tr("Profile saved to %1").arg(path));
}

void MainWindow::loadProfile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load webcam profile"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Webcam profiles (*.webcam.json);;JSON files (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QVector<CameraControlValue> values;
    QString profileDevice;
    QString error;
    if (!ProfileStore::load(path, &values, &profileDevice, &error)) {
        QMessageBox::warning(this, tr("Could not load profile"), error);
        return;
    }

    if (!profileDevice.isEmpty() && profileDevice != deviceCombo_->currentText()) {
        const auto answer = QMessageBox::question(
            this, tr("Profile is for another camera"),
            tr("This profile was saved for “%1”, but “%2” is selected. Apply it anyway?")
                .arg(profileDevice, deviceCombo_->currentText()));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    int applied = 0;
    for (const auto &value : values) {
        const auto it = std::find_if(controlRows_.begin(), controlRows_.end(), [&value](ControlRow *row) {
            return row->currentValue().id == value.id;
        });
        if (it != controlRows_.end()) {
            (*it)->setCurrentValue(value.value, value.automatic, true);
            ++applied;
        }
    }
    setStatus(tr("Loaded %1 settings from %2").arg(applied).arg(path));
}

void MainWindow::setStatus(const QString &message, const bool error)
{
    statusLabel_->setText(message);
    statusLabel_->setStyleSheet(error ? QStringLiteral("color: #d95555;")
                                      : QStringLiteral("color: palette(mid);"));
}

void MainWindow::resetImageQuality(const QString &status, const QString &detail)
{
    ++imageQualityGeneration_;
    imageQualitySampler_.reset();
    latestImageMetrics_ = {};
    preAdjustmentMetrics_ = {};
    awaitingPostAdjustmentMetrics_ = false;
    imageQualityStatusLabel_->setText(status);
    imageQualitySharpnessLabel_->setText(QStringLiteral("—"));
    imageQualityBrightnessLabel_->setText(QStringLiteral("—"));
    imageQualityBlackClippingLabel_->setText(QStringLiteral("—"));
    imageQualityWhiteClippingLabel_->setText(QStringLiteral("—"));
    imageQualityContrastLabel_->setText(QStringLiteral("—"));
    imageQualityDurationLabel_->setText(QStringLiteral("—"));
    imageQualityHistogramLabel_->setPixmap(QPixmap());
    imageQualityHistogramLabel_->setText(status);
    imageQualityObservationsLabel_->setText(
        detail.isEmpty() ? status : detail);
}

void MainWindow::analyzeImageQuality(const QImage &image, const qint64 timestampNanoseconds)
{
    if (!imageQualitySampler_.tryStartAnalysis(timestampNanoseconds)) {
        return;
    }

    imageQualityRequestGeneration_ = imageQualityGeneration_;
    imageQualityRequestTimestampNanoseconds_ = timestampNanoseconds;
    const ImageQualityAnalyzer analyzer = imageQualityAnalyzer_;
    imageQualityWatcher_->setFuture(QtConcurrent::run([analyzer, image] {
        try {
            return analyzer.analyze(image);
        } catch (const std::bad_alloc &) {
            ImageQualityResult result;
            result.error = QStringLiteral("Insufficient memory to analyze this frame.");
            return result;
        }
    }));
}

void MainWindow::updateImageQuality(const ImageQualityResult &result)
{
    if (!result.valid) {
        latestImageMetrics_ = {};
        resetImageQuality(tr("Unavailable"),
                          result.error.isEmpty() ? tr("Image analysis was unavailable.")
                                                 : result.error);
        return;
    }

    latestImageMetrics_ = result;
    if (awaitingPostAdjustmentMetrics_
        && imageQualityRequestTimestampNanoseconds_ >= postAdjustmentThresholdNanoseconds_) {
        const auto before = preAdjustmentMetrics_;
        awaitingPostAdjustmentMetrics_ = false;
        const QString beforeSharpness = before.sharpnessAvailable
            ? QString::number(before.sharpnessVariance, 'f', 1) : tr("unavailable");
        const QString afterSharpness = result.sharpnessAvailable
            ? QString::number(result.sharpnessVariance, 'f', 1) : tr("unavailable");
        directorResponse_->appendPlainText(
            tr("\n\nFresh local camera-frame measurements (not an objective quality score): "
               "mean luminance %1 → %2 / 255; black clipping %3% → %4%; "
               "white clipping %5% → %6%; contrast %7 → %8 / 255; "
               "Laplacian variance %9 → %10.%11")
                .arg(QString::number(before.meanLuminance, 'f', 1),
                     QString::number(result.meanLuminance, 'f', 1),
                     QString::number(before.blackClippingPercent, 'f', 2),
                     QString::number(result.blackClippingPercent, 'f', 2),
                     QString::number(before.whiteClippingPercent, 'f', 2),
                     QString::number(result.whiteClippingPercent, 'f', 2),
                     QString::number(before.contrast, 'f', 1),
                     QString::number(result.contrast, 'f', 1))
                .arg(beforeSharpness, afterSharpness)
                .arg(tr(" These measurements use the unmodified camera feed; software effects are not included.")));
        refreshDirectorButtons();
    }
    imageQualityStatusLabel_->setText(tr("Live measurements"));
    imageQualitySharpnessLabel_->setText(
        result.sharpnessAvailable
            ? QString::number(result.sharpnessVariance, 'f', 1)
            : tr("Unavailable"));
    imageQualityBrightnessLabel_->setText(
        tr("%1 / 255").arg(result.meanLuminance, 0, 'f', 1));
    imageQualityBlackClippingLabel_->setText(
        tr("%1%").arg(result.blackClippingPercent, 0, 'f', 2));
    imageQualityWhiteClippingLabel_->setText(
        tr("%1%").arg(result.whiteClippingPercent, 0, 'f', 2));
    imageQualityContrastLabel_->setText(tr("%1 / 255").arg(result.contrast, 0, 'f', 1));
    imageQualityDurationLabel_->setText(tr("%1 ms").arg(result.processingMilliseconds, 0, 'f', 2));
    drawLuminanceHistogram(result.luminanceHistogram);
    imageQualityObservationsLabel_->setText(
        result.observations.isEmpty()
            ? tr("No configured threshold observations.")
            : result.observations.join(QLatin1Char('\n')));
}

void MainWindow::drawLuminanceHistogram(const std::array<quint64, 256> &histogram)
{
    QImage chart(256, 92, QImage::Format_ARGB32_Premultiplied);
    chart.fill(imageQualityHistogramLabel_->palette().base().color());

    const quint64 maximum = *std::max_element(histogram.cbegin(), histogram.cend());
    if (maximum == 0) {
        imageQualityHistogramLabel_->setPixmap(QPixmap());
        imageQualityHistogramLabel_->setText(tr("Unavailable"));
        return;
    }

    QPainter painter(&chart);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(imageQualityHistogramLabel_->palette().highlight().color());
    constexpr int baseline = 87;
    constexpr int maximumBarHeight = 78;
    for (int bin = 0; bin < static_cast<int>(histogram.size()); ++bin) {
        const int height = static_cast<int>(
            static_cast<double>(histogram.at(static_cast<std::size_t>(bin)))
            / static_cast<double>(maximum) * maximumBarHeight);
        painter.drawLine(bin, baseline, bin, baseline - height);
    }
    painter.setPen(imageQualityHistogramLabel_->palette().mid().color());
    painter.drawLine(0, baseline, 255, baseline);
    painter.end();

    imageQualityHistogramLabel_->setText(QString());
    imageQualityHistogramLabel_->setPixmap(QPixmap::fromImage(chart));
}

void MainWindow::updatePerformanceMetrics()
{
    if (!camera_) {
        performanceLabel_->setText(
            tr("FPS  —   |   INTERVAL  —   |   JITTER  —   |   LATE  —"));
        return;
    }

    const CameraPerformanceMetrics current =
        cameraPerformance_.metrics(performanceClock_.nsecsElapsed());
    const QString interval = current.hasFrameIntervals
        ? QString::number(current.averageFrameIntervalMilliseconds, 'f', 1)
        : tr("—");
    const QString jitter = current.hasFrameIntervals
        ? QString::number(current.frameIntervalJitterMilliseconds, 'f', 1)
        : tr("—");
    const QString lateCount = current.lateFrameCountAvailable
        ? QString::number(current.lateFrameCount)
        : tr("—");
    performanceLabel_->setText(
        tr("FPS  %1   |   INTERVAL  %2 ms   |   JITTER  %3 ms   |   LATE  %4")
            .arg(QString::number(current.rollingFramesPerSecond, 'f', 1))
            .arg(interval)
            .arg(jitter)
            .arg(lateCount));
}

QVector<CameraControlValue> MainWindow::currentValues() const
{
    QVector<CameraControlValue> values;
    values.reserve(controlRows_.size());
    for (const auto *row : controlRows_) {
        if (row->canPersist()) values.push_back(row->currentValue());
    }
    return values;
}

QVector<CameraControl> MainWindow::cameraControls(QString *error) const
{
    if (!backend_ || !backend_->isOpen()) {
        if (error) {
            *error = QStringLiteral("Camera controls are unavailable while the native backend is disconnected.");
        }
        return {};
    }
    return backend_->controls(error);
}

void MainWindow::requestDirector()
{
    if (!groqConnected_) {
        directorResponse_->setPlainText(
            tr("Connect your Groq account to use AI Camera Director."));
        refreshDirectorButtons();
        return;
    }
    if (!groqOptInBox_->isChecked()) {
        directorResponse_->setPlainText(
            tr("Enable the explicit Groq sharing consent before sending a request."));
        return;
    }
    const QString prompt = directorPrompt_->toPlainText().trimmed();
    if (prompt.isEmpty() || prompt.size() > 1500) {
        directorResponse_->setPlainText(
            tr("Enter a request of 1–1500 characters."));
        return;
    }
    QString controlError;
    latestCameraControls_ = cameraControls(&controlError);
    pendingDirectorActions_.clear();
    refreshDirectorButtons();

    QJsonArray settings;
    for (const auto &value : currentValues()) {
        settings.append(QJsonObject{
            {QStringLiteral("id"), value.id},
            {QStringLiteral("value"), static_cast<double>(value.value)},
            {QStringLiteral("automatic"), value.automatic},
        });
    }
    QJsonObject metrics{
        {QStringLiteral("available"), latestImageMetrics_.valid},
    };
    if (latestImageMetrics_.valid) {
        metrics.insert(QStringLiteral("mean_luminance"), latestImageMetrics_.meanLuminance);
        metrics.insert(QStringLiteral("black_clipping_percent"), latestImageMetrics_.blackClippingPercent);
        metrics.insert(QStringLiteral("white_clipping_percent"), latestImageMetrics_.whiteClippingPercent);
        metrics.insert(QStringLiteral("contrast_p95_minus_p5"), latestImageMetrics_.contrast);
        metrics.insert(QStringLiteral("sharpness_available"), latestImageMetrics_.sharpnessAvailable);
        if (latestImageMetrics_.sharpnessAvailable) {
            metrics.insert(QStringLiteral("laplacian_variance"),
                           latestImageMetrics_.sharpnessVariance);
        }
    } else {
        metrics.insert(QStringLiteral("reason"),
                       camera_ ? QStringLiteral("No valid local frame metrics yet.")
                               : QStringLiteral("No camera is selected."));
    }
    const QJsonObject context{
        {QStringLiteral("camera_connected"), camera_ != nullptr},
        {QStringLiteral("camera_name"), selectedDeviceName_},
        {QStringLiteral("capabilities"), CameraDirectorTools::capabilities(latestCameraControls_)},
        {QStringLiteral("current_settings"), settings},
        {QStringLiteral("image_metrics"), metrics},
        {QStringLiteral("current_software_effects"), QJsonObject{
            {QStringLiteral("grayscale"), currentImageEffects_.grayscale},
            {QStringLiteral("brightness"), currentImageEffects_.brightness},
            {QStringLiteral("contrast"), currentImageEffects_.contrast},
            {QStringLiteral("saturation"), currentImageEffects_.saturation},
            {QStringLiteral("warmth"), currentImageEffects_.warmth},
            {QStringLiteral("mild_sharpening"), currentImageEffects_.mildSharpening},
        }},
        {QStringLiteral("control_read_error"), controlError},
    };
    const QString systemPrompt = tr(
        "You are a cautious photography assistant for a webcam settings application. "
        "Use only the supplied numeric metrics and advertised camera capabilities. "
        "Never claim objective improvement, sensor exposure time, hardware frame drops, "
        "lens damage, or definitive focus failure. Explain limitations. Use the typed "
        "write tools only to propose changes; the application locally validates every "
        "proposal and will not apply it without approval (unless its explicit Auto-Apply "
        "toggle is enabled). Do not request or imply photo capture. No camera image is "
        "available to you. Read-only context is already supplied; call those read tools "
        "only if required. If a capability is missing, explain that it is unsupported.");
    const QString userContent = tr("User request:\n%1\n\nLocally measured context "
                                   "(numeric data only; no image pixels):\n%2")
        .arg(prompt, QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Indented)));
    const QJsonArray messages{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                    {QStringLiteral("content"), systemPrompt}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), userContent}},
    };
    directorRequestMessages_ = messages;
    directorToolRoundTripUsed_ = false;
    directorRequestGeneration_ = directorContextGeneration_;
    sendDirectorMessagesWithCredential(messages);
}

void MainWindow::sendDirectorMessagesWithCredential(const QJsonArray &messages)
{
    directorRequestMessages_ = messages;
    awaitingDirectorCredential_ = true;
    directorResponse_->setPlainText(tr("Retrieving the Groq credential securely…"));
    if (!credentialStore_->read()) {
        awaitingDirectorCredential_ = false;
        directorResponse_->setPlainText(tr("Credential storage is busy; try again."));
        setStatus(tr("Credential storage is busy; try again."), true);
        refreshDirectorButtons();
        return;
    }
    refreshDirectorButtons();
}

void MainWindow::openGroqSettings()
{
    GroqSettingsDialog dialog(groqClient_, groqModel_, this);
    connect(&dialog, &GroqSettingsDialog::modelChanged, this, [this](const QString &model) {
        if (model.trimmed().isEmpty()) {
            return;
        }
        groqModel_ = model;
        QSettings settings(QString::fromLatin1(kSettingsOrganization),
                           QString::fromLatin1(kSettingsApplication));
        settings.setValue(QStringLiteral("groq/model"), groqModel_);
    });
    connect(&dialog, &GroqSettingsDialog::connectionChanged, this, [this](const bool connected) {
        groqConnected_ = connected;
        groqConnectionStatusLabel_->setText(
            connected ? tr("Connected to Groq. AI requests send text and numeric context only.")
                      : tr("Connect your Groq account to use AI Camera Director."));
        refreshDirectorButtons();
    });
    dialog.exec();
}

void MainWindow::handleDirectorReply(const QJsonObject &message, const QString &error)
{
    if (directorRequestGeneration_ != directorContextGeneration_) {
        directorRequestMessages_ = {};
        refreshDirectorButtons();
        return;
    }
    if (!error.isEmpty()) {
        directorRequestMessages_ = {};
        if (error.contains(QStringLiteral("rejected the API key"), Qt::CaseInsensitive)) {
            groqConnected_ = false;
            groqConnectionStatusLabel_->setText(
                tr("Connect your Groq account to use AI Camera Director."));
        }
        directorResponse_->setPlainText(error);
        setStatus(error, true);
        refreshDirectorButtons();
        return;
    }

    const auto calls = message.value(QStringLiteral("tool_calls")).toArray();
    if (!directorToolRoundTripUsed_ && !calls.isEmpty()) {
        bool allReadOnly = true;
        for (const auto &callValue : calls) {
            const auto function = callValue.toObject().value(QStringLiteral("function")).toObject();
            const QString name = function.value(QStringLiteral("name")).toString();
            const auto arguments = QJsonDocument::fromJson(
                function.value(QStringLiteral("arguments")).toString().toUtf8()).object();
            auto result = CameraDirectorTools::validate(
                name, arguments, latestCameraControls_, currentImageEffects_);
            if (!result.valid || !result.readOnly) {
                allReadOnly = false;
                break;
            }
        }
        if (allReadOnly) {
            QJsonArray followup = directorRequestMessages_;
            followup.append(message);
            for (qsizetype index = 0; index < calls.size(); ++index) {
                const auto call = calls.at(index).toObject();
                const QString name = call.value(QStringLiteral("function")).toObject()
                                         .value(QStringLiteral("name")).toString();
                QJsonObject result;
                if (name == QLatin1String("get_camera_capabilities")) {
                    result = CameraDirectorTools::capabilities(latestCameraControls_);
                } else if (name == QLatin1String("get_current_settings")) {
                    QJsonArray settings;
                    for (const auto &value : currentValues()) {
                        settings.append(QJsonObject{
                            {QStringLiteral("id"), value.id},
                            {QStringLiteral("value"), static_cast<double>(value.value)},
                            {QStringLiteral("automatic"), value.automatic},
                        });
                    }
                    result = {{QStringLiteral("settings"), settings}};
                } else {
                    result = {{QStringLiteral("available"), latestImageMetrics_.valid}};
                    if (latestImageMetrics_.valid) {
                        result.insert(QStringLiteral("mean_luminance"), latestImageMetrics_.meanLuminance);
                        result.insert(QStringLiteral("black_clipping_percent"),
                                      latestImageMetrics_.blackClippingPercent);
                        result.insert(QStringLiteral("white_clipping_percent"),
                                      latestImageMetrics_.whiteClippingPercent);
                        result.insert(QStringLiteral("contrast_p95_minus_p5"),
                                      latestImageMetrics_.contrast);
                        if (latestImageMetrics_.sharpnessAvailable) {
                            result.insert(QStringLiteral("laplacian_variance"),
                                          latestImageMetrics_.sharpnessVariance);
                        }
                    } else {
                        result.insert(QStringLiteral("reason"),
                                      QStringLiteral("No valid local image measurement is available."));
                    }
                }
                followup.append(QJsonObject{
                    {QStringLiteral("role"), QStringLiteral("tool")},
                    {QStringLiteral("tool_call_id"), call.value(QStringLiteral("id"))},
                    {QStringLiteral("content"),
                     QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))},
                });
            }
            directorToolRoundTripUsed_ = true;
            sendDirectorMessagesWithCredential(followup);
            return;
        }
    }

    QStringList output;
    const QString explanation = message.value(QStringLiteral("content")).toString().trimmed();
    if (!explanation.isEmpty()) {
        output.push_back(explanation);
    }
    pendingDirectorActions_.clear();
    QStringList validationNotes;
    for (const auto &callValue : calls) {
        const auto function = callValue.toObject().value(QStringLiteral("function")).toObject();
        const QString name = function.value(QStringLiteral("name")).toString();
        const auto arguments = QJsonDocument::fromJson(
            function.value(QStringLiteral("arguments")).toString().toUtf8()).object();
        const DirectorToolValidation validation =
            CameraDirectorTools::validate(name, arguments, latestCameraControls_, currentImageEffects_);
        if (!validation.valid) {
            validationNotes.push_back(tr("Unsupported proposal (%1): %2").arg(name, validation.error));
            continue;
        }
        if (validation.readOnly) {
            continue;
        }
        if (validation.action.type == DirectorAction::Type::RestoreSettings
            && directorSnapshots_.isEmpty()) {
            validationNotes.push_back(tr(
                "Unsupported proposal (restore_settings): no in-memory adjustment snapshot exists."));
            continue;
        }
        pendingDirectorActions_.push_back(validation.action);
    }

    QSet<QString> proposalTargets;
    bool duplicateTarget = false;
    for (const auto &action : std::as_const(pendingDirectorActions_)) {
        QString target;
        switch (action.type) {
        case DirectorAction::Type::SetExposure:
            target = QStringLiteral("exposure");
            break;
        case DirectorAction::Type::SetFocusMode:
            target = QStringLiteral("focus");
            break;
        case DirectorAction::Type::SetWhiteBalance:
            target = QStringLiteral("white-balance");
            break;
        case DirectorAction::Type::ApplyImageEffects:
            target = QStringLiteral("image-effects");
            break;
        case DirectorAction::Type::RestoreSettings:
            target = QStringLiteral("restore");
            break;
        }
        duplicateTarget = duplicateTarget || proposalTargets.contains(target);
        proposalTargets.insert(target);
    }
    if (duplicateTarget) {
        pendingDirectorActions_.clear();
        validationNotes.push_back(tr(
            "The response proposed multiple changes for the same setting. No proposals are available to apply."));
    }

    const bool hasRestore = std::any_of(
        pendingDirectorActions_.cbegin(), pendingDirectorActions_.cend(), [](const DirectorAction &action) {
            return action.type == DirectorAction::Type::RestoreSettings;
        });
    if (hasRestore && pendingDirectorActions_.size() != 1) {
        pendingDirectorActions_.clear();
        validationNotes.push_back(tr(
            "The restore proposal cannot be combined with other changes; ask to undo separately."));
    }
    if (!pendingDirectorActions_.isEmpty()) {
        output.push_back(tr("Validated proposals:"));
        for (const auto &action : pendingDirectorActions_) {
            output.push_back(QStringLiteral("• %1").arg(action.summary));
        }
    }
    if (!validationNotes.isEmpty()) {
        output.push_back(validationNotes.join(QLatin1Char('\n')));
    }
    if (calls.isEmpty()) {
        output.push_back(tr("No camera or effect changes were proposed."));
    }
    if (output.isEmpty()) {
        output.push_back(tr("Groq returned no explanation or applicable proposals."));
    }
    directorRequestMessages_ = {};
    directorResponse_->setPlainText(output.join(QStringLiteral("\n\n")));
    refreshDirectorButtons();
    if (autoApplyDirectorBox_->isChecked() && !pendingDirectorActions_.isEmpty()) {
        applyPendingDirectorActions();
    } else if (!pendingDirectorActions_.isEmpty()) {
        setStatus(tr("Review the validated proposals, then choose Apply Settings."));
    }
}

void MainWindow::renderPreview(const QImage &image)
{
    if (image.isNull()) {
        return;
    }
    if (currentImageEffects_ == ImageEffects{}) {
        const QSize target = videoLabel_->contentsRect().size();
        videoLabel_->setPixmap(QPixmap::fromImage(image).scaled(
            target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        return;
    }
    startEffectsPreview(image);
}

void MainWindow::startEffectsPreview(const QImage &image)
{
    if (image.isNull()) {
        return;
    }
    if (effectsWatcher_->isRunning()) {
        pendingEffectsFrame_ = image;
        return;
    }
    pendingEffectsFrame_ = {};
    const ImageEffects effects = currentImageEffects_;
    effectsRequestGeneration_ = effectsGeneration_;
    effectsWatcher_->setFuture(QtConcurrent::run([image, effects] {
        return ImageEffectsProcessor::apply(image, effects);
    }));
}

void MainWindow::applyPendingDirectorActions()
{
    if (pendingDirectorActions_.isEmpty()) {
        return;
    }
    if (pendingDirectorActions_.first().type == DirectorAction::Type::RestoreSettings) {
        undoDirectorChanges();
        pendingDirectorActions_.clear();
        refreshDirectorButtons();
        return;
    }
    if (!camera_) {
        setStatus(tr("No camera is selected; proposals were not applied."), true);
        return;
    }

    const ImageQualityResult beforeAdjustment = latestImageMetrics_;
    directorSnapshots_.push_back({currentValues(), currentImageEffects_});
    int successes = 0;
    int failures = 0;
    bool effectsChanged = false;
    for (const auto &action : std::as_const(pendingDirectorActions_)) {
        if (action.type == DirectorAction::Type::ApplyImageEffects) {
            if (currentImageEffects_ != action.effects) {
                currentImageEffects_ = action.effects;
                effectsChanged = true;
                ++successes;
            }
        } else if (applyValue(action.controlValue, false)) {
            ++successes;
        } else {
            ++failures;
        }
    }
    if (effectsChanged) {
        ++effectsGeneration_;
        pendingEffectsFrame_ = {};
        renderPreview(latestRawFrame_);
    }
    if (successes > 0) {
        beginPostAdjustmentMeasurement(beforeAdjustment);
    }
    pendingDirectorActions_.clear();
    directorResponse_->appendPlainText(
        tr("\n\nApplied %1 change(s); %2 could not be applied. Camera-reported values "
           "are read back where the backend supports it.")
            .arg(successes).arg(failures));
    setStatus(failures == 0
                  ? tr("Applied %1 assistant-proposed change(s).").arg(successes)
                  : tr("Applied %1 change(s); %2 failed. Use Undo to restore the prior snapshot.")
                        .arg(successes).arg(failures),
              failures != 0);
    refreshDirectorButtons();
}

bool MainWindow::restoreSnapshot(const QVector<CameraControlValue> &values,
                                 const ImageEffects &effects)
{
    const ImageQualityResult beforeAdjustment = latestImageMetrics_;
    bool success = true;
    for (const auto &value : values) {
        if (!applyValue(value, false)) {
            success = false;
        }
    }
    const bool effectsChanged = currentImageEffects_ != effects;
    currentImageEffects_ = effects;
    if (effectsChanged) {
        ++effectsGeneration_;
        pendingEffectsFrame_ = {};
        renderPreview(latestRawFrame_);
    }
    beginPostAdjustmentMeasurement(beforeAdjustment);
    return success;
}

void MainWindow::beginPostAdjustmentMeasurement(const ImageQualityResult &baseline)
{
    preAdjustmentMetrics_ = baseline;
    awaitingPostAdjustmentMetrics_ = baseline.valid;
    postAdjustmentThresholdNanoseconds_ = performanceClock_.nsecsElapsed();
    if (awaitingPostAdjustmentMetrics_) {
        directorResponse_->appendPlainText(
            tr("\n\nWaiting for the next sampled camera frame to report measured changes…"));
    } else {
        directorResponse_->appendPlainText(
            tr("\n\nA before-adjustment image measurement was unavailable; no comparison is reported."));
    }
}

void MainWindow::undoDirectorChanges()
{
    if (directorSnapshots_.isEmpty()) {
        setStatus(tr("There is no assistant adjustment to undo."), true);
        return;
    }
    const AdjustmentSnapshot snapshot = directorSnapshots_.last();
    if (!restoreSnapshot(snapshot.values, snapshot.effects)) {
        setStatus(tr("Some settings could not be restored. The undo snapshot is retained for retry."),
                  true);
        return;
    }
    directorSnapshots_.removeLast();
    setStatus(tr("Restored the previous camera settings and software effects."));
    refreshDirectorButtons();
}

void MainWindow::resetDirectorChanges()
{
    if (!camera_) {
        return;
    }
    directorSnapshots_.push_back({currentValues(), currentImageEffects_});
    if (!restoreSnapshot(originalCameraValues_, originalImageEffects_)) {
        setStatus(tr("Reset was partial; unsupported or disconnected camera controls were not changed."),
                  true);
        return;
    }
    pendingDirectorActions_.clear();
    directorResponse_->setPlainText(tr("Restored the selected camera's initial settings and neutral software effects."));
    setStatus(tr("Restored the original camera settings and software effects."));
    refreshDirectorButtons();
}

void MainWindow::capturePhoto()
{
    if (latestRawFrame_.isNull()) {
        setStatus(tr("No preview frame is available to save."), true);
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save preview photo"), QStandardPaths::writableLocation(
            QStandardPaths::PicturesLocation) + QStringLiteral("/webcam-photo.png"),
        tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if (path.isEmpty()) {
        return;
    }
    QImage output = ImageEffectsProcessor::apply(latestRawFrame_, currentImageEffects_);
    if (output.isNull()) {
        setStatus(tr("Could not process the preview frame for saving."), true);
        return;
    }
    QImageWriter writer(path);
    if (!writer.write(output)) {
        setStatus(tr("Could not save photo: %1").arg(writer.errorString()), true);
        return;
    }
    setStatus(tr("Saved the locally processed preview photo to %1.").arg(path));
}

void MainWindow::refreshDirectorButtons()
{
    const bool busy = (groqClient_ && groqClient_->isBusy()) || awaitingDirectorCredential_;
    if (askDirectorButton_) {
        askDirectorButton_->setEnabled(groqConnected_ && groqOptInBox_
                                       && groqOptInBox_->isChecked() && !busy);
    }
    if (cancelDirectorButton_) {
        cancelDirectorButton_->setEnabled(busy);
    }
    if (applyDirectorButton_) {
        applyDirectorButton_->setEnabled(camera_ && !pendingDirectorActions_.isEmpty());
    }
    if (undoDirectorButton_) {
        undoDirectorButton_->setEnabled(camera_ && !directorSnapshots_.isEmpty());
    }
    if (resetDirectorButton_) {
        resetDirectorButton_->setEnabled(
            camera_ && (!originalCameraValues_.isEmpty()
                       || currentImageEffects_ != originalImageEffects_));
    }
    if (capturePhotoButton_) {
        capturePhotoButton_->setEnabled(!latestRawFrame_.isNull());
    }
}

void MainWindow::displayFrame(const QVideoFrame &frame)
{
    const qint64 timestampNanoseconds = performanceClock_.nsecsElapsed();
    if (frame.isValid()) {
        cameraPerformance_.recordFrame(timestampNanoseconds);
    }

    const QImage image = frame.toImage();
    if (image.isNull()) {
        return;
    }
    latestRawFrame_ = image;
    capturePhotoButton_->setEnabled(true);
    renderPreview(image);
    analyzeImageQuality(image, timestampNanoseconds);
}
