#include "groq_settings_dialog.h"

#include "groq_client.h"
#include "secure_credential_store.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

GroqSettingsDialog::GroqSettingsDialog(GroqClient *client, QString selectedModel, QWidget *parent)
    : QDialog(parent), client_(client),
      credentialStore_(new SecureCredentialStore(this))
{
    setWindowTitle(tr("AI Provider — Groq"));
    setModal(true);
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(
        tr("Your key is stored in this operating system's protected credential store. "
           "AI prompts and numeric camera context are sent to Groq; camera frames and photos are not."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("API key:"), this), 0, 0);
    keyInput_ = new QLineEdit(this);
    keyInput_->setEchoMode(QLineEdit::Password);
    keyInput_->setPlaceholderText(tr("Paste a Groq API key"));
    keyInput_->setClearButtonEnabled(true);
    grid->addWidget(keyInput_, 0, 1);
    showKeyButton_ = new QPushButton(tr("Show"), this);
    showKeyButton_->setCheckable(true);
    grid->addWidget(showKeyButton_, 0, 2);

    grid->addWidget(new QLabel(tr("Model:"), this), 1, 0);
    modelCombo_ = new QComboBox(this);
    modelCombo_->setEditable(false);
    modelCombo_->setInsertPolicy(QComboBox::NoInsert);
    if (selectedModel.trimmed().isEmpty()) {
        selectedModel = QStringLiteral("openai/gpt-oss-20b");
    }
    modelCombo_->addItem(selectedModel);
    grid->addWidget(modelCombo_, 1, 1);
    refreshModelsButton_ = new QPushButton(tr("Refresh"), this);
    grid->addWidget(refreshModelsButton_, 1, 2);
    layout->addLayout(grid);

    auto *keyLink = new QPushButton(tr("Get a key from console.groq.com/keys"), this);
    keyLink->setFlat(true);
    keyLink->setStyleSheet(QStringLiteral("text-align: left; text-decoration: underline;"));
    layout->addWidget(keyLink);
    statusLabel_ = new QLabel(tr("Not connected. Test a saved key or enter a new one."), this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto *buttons = new QHBoxLayout;
    testButton_ = new QPushButton(tr("Test Connection"), this);
    saveButton_ = new QPushButton(tr("Save & Connect"), this);
    removeButton_ = new QPushButton(tr("Remove API Key"), this);
    auto *closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(testButton_);
    buttons->addWidget(saveButton_);
    buttons->addWidget(removeButton_);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(showKeyButton_, &QPushButton::toggled, this, [this](const bool visible) {
        keyInput_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        showKeyButton_->setText(visible ? tr("Hide") : tr("Show"));
    });
    connect(keyLink, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://console.groq.com/keys")));
    });
    connect(testButton_, &QPushButton::clicked, this, [this] { beginTest(false); });
    connect(saveButton_, &QPushButton::clicked, this, [this] { beginTest(true); });
    connect(removeButton_, &QPushButton::clicked, this, [this] {
        setBusy(true);
        if (!credentialStore_->remove()) {
            setBusy(false);
            statusLabel_->setText(tr("Credential storage is busy; try again."));
        }
    });
    connect(refreshModelsButton_, &QPushButton::clicked, this, &GroqSettingsDialog::refreshModelList);
    connect(modelCombo_, &QComboBox::currentTextChanged, this, &GroqSettingsDialog::modelChanged);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(credentialStore_, &SecureCredentialStore::readFinished, this,
            [this](const bool success, const QString &secret, const QString &error) {
                if (credentialReadPurpose_ == CredentialReadPurpose::None) {
                    return;
                }
                const CredentialReadPurpose purpose = credentialReadPurpose_;
                credentialReadPurpose_ = CredentialReadPurpose::None;
                if (!success) {
                    setBusy(false);
                    statusLabel_->setText(error);
                    return;
                }
                if (purpose == CredentialReadPurpose::Refresh) {
                    setBusy(true);
                    if (!client_->refreshModels(secret)) {
                        setBusy(false);
                        statusLabel_->setText(tr("Could not refresh the Groq model list."));
                    } else {
                        connectionOperationActive_ = true;
                    }
                    return;
                }
                testingSavedCredential_ = true;
                beginCredentialTest(secret);
            });
    connect(credentialStore_, &SecureCredentialStore::writeFinished, this,
            [this](const bool success, const QString &error) {
                setBusy(false);
                if (!success) {
                    pendingSecret_.clear();
                    statusLabel_->setText(error);
                    return;
                }
                pendingSecret_.clear();
                keyInput_->clear();
                statusLabel_->setText(tr("Connected. The selected model passed tool-calling verification."));
                emit connectionChanged(true);
            });
    connect(credentialStore_, &SecureCredentialStore::removeFinished, this,
            [this](const bool success, const QString &error) {
                setBusy(false);
                if (!success) {
                    statusLabel_->setText(error);
                    return;
                }
                keyInput_->clear();
                pendingSecret_.clear();
                statusLabel_->setText(tr("Groq API key removed from secure storage."));
                emit connectionChanged(false);
            });
    connect(client_, &GroqClient::modelsReceived, this,
            [this](const QStringList &models, const QString &error) {
                if (!error.isEmpty()) {
                    connectionOperationActive_ = false;
                    setBusy(false);
                    statusLabel_->setText(error);
                    return;
                }
                if (!client_->isBusy()) {
                    connectionOperationActive_ = false;
                    setBusy(false);
                }
                const QString selected = modelCombo_->currentText();
                modelCombo_->blockSignals(true);
                modelCombo_->clear();
                modelCombo_->addItems(models);
                const int selectedIndex = modelCombo_->findText(selected);
                modelCombo_->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
                modelCombo_->blockSignals(false);
                emit modelChanged(modelCombo_->currentText());
            });
    connect(client_, &GroqClient::connectionTestFinished, this,
            [this](const bool success, const QString &message) {
                connectionOperationActive_ = false;
                setBusy(false);
                statusLabel_->setText(message);
                if (!success) {
                    pendingSecret_.clear();
                    if (testingSavedCredential_
                        && !message.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive)) {
                        emit connectionChanged(false);
                    }
                    testingSavedCredential_ = false;
                    pendingSave_ = false;
                    return;
                }
                if (pendingSave_) {
                    pendingSave_ = false;
                    setBusy(true);
                    if (!credentialStore_->write(pendingSecret_)) {
                        setBusy(false);
                        pendingSecret_.clear();
                        statusLabel_->setText(tr("Credential storage is busy or the key is empty."));
                    }
                } else if (testingSavedCredential_) {
                    testingSavedCredential_ = false;
                    pendingSecret_.clear();
                    statusLabel_->setText(tr("Connected. Authentication and tool calling were verified."));
                    emit connectionChanged(true);
                } else {
                    pendingSecret_.clear();
                    statusLabel_->setText(tr("Verified. Select Save & Connect to store the key and enable the assistant."));
                }
            });
}

GroqSettingsDialog::~GroqSettingsDialog()
{
    if (connectionOperationActive_) {
        client_->cancel();
    }
    pendingSecret_.fill(QChar(0));
    pendingSecret_.clear();
}

void GroqSettingsDialog::beginTest(const bool saveAfterTest)
{
    if (client_->isBusy() || credentialStore_->isBusy()) {
        statusLabel_->setText(tr("Another Groq or credential operation is in progress."));
        return;
    }
    pendingSave_ = saveAfterTest;
    testingSavedCredential_ = false;
    if (!keyInput_->text().trimmed().isEmpty()) {
        pendingSecret_ = keyInput_->text().trimmed();
        beginCredentialTest(pendingSecret_);
        return;
    }
    readCredentialForTest();
}

void GroqSettingsDialog::readCredentialForTest()
{
    credentialReadPurpose_ = CredentialReadPurpose::Test;
    setBusy(true);
    if (!credentialStore_->read()) {
        credentialReadPurpose_ = CredentialReadPurpose::None;
        setBusy(false);
        statusLabel_->setText(tr("Credential storage is busy; try again."));
    }
}

void GroqSettingsDialog::beginCredentialTest(const QString &secret)
{
    pendingSecret_ = secret;
    setBusy(true);
    if (!client_->testConnection(secret, modelCombo_->currentText())) {
        setBusy(false);
        pendingSecret_.clear();
        statusLabel_->setText(tr("Could not start the connection test. Check the key, model, and network."));
        return;
    }
    connectionOperationActive_ = true;
}

void GroqSettingsDialog::refreshModelList()
{
    if (client_->isBusy() || credentialStore_->isBusy()) {
        statusLabel_->setText(tr("Another Groq or credential operation is in progress."));
        return;
    }
    const QString key = keyInput_->text().trimmed();
    if (!key.isEmpty()) {
        setBusy(true);
        if (!client_->refreshModels(key)) {
            setBusy(false);
            statusLabel_->setText(tr("Could not refresh the Groq model list."));
        } else {
            connectionOperationActive_ = true;
        }
        return;
    }
    credentialReadPurpose_ = CredentialReadPurpose::Refresh;
    setBusy(true);
    if (!credentialStore_->read()) {
        credentialReadPurpose_ = CredentialReadPurpose::None;
        setBusy(false);
        statusLabel_->setText(tr("Credential storage is busy; try again."));
    }
}

void GroqSettingsDialog::setBusy(const bool busy)
{
    testButton_->setEnabled(!busy);
    saveButton_->setEnabled(!busy);
    removeButton_->setEnabled(!busy);
    refreshModelsButton_->setEnabled(!busy);
    modelCombo_->setEnabled(!busy);
    keyInput_->setEnabled(!busy);
}
