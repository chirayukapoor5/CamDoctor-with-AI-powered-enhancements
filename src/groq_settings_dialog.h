#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class SecureCredentialStore;
class GroqClient;

class GroqSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit GroqSettingsDialog(GroqClient *client, QString selectedModel, QWidget *parent = nullptr);
    ~GroqSettingsDialog() override;

signals:
    void connectionChanged(bool connected);
    void modelChanged(const QString &model);

private:
    enum class CredentialReadPurpose { None, Test, Refresh };
    void beginTest(bool saveAfterTest);
    void readCredentialForTest();
    void beginCredentialTest(const QString &secret);
    void refreshModelList();
    void setBusy(bool busy);

    GroqClient *client_{};
    SecureCredentialStore *credentialStore_{};
    QLineEdit *keyInput_{};
    QPushButton *showKeyButton_{};
    QPushButton *testButton_{};
    QPushButton *saveButton_{};
    QPushButton *removeButton_{};
    QPushButton *refreshModelsButton_{};
    QComboBox *modelCombo_{};
    QLabel *statusLabel_{};
    QString pendingSecret_;
    CredentialReadPurpose credentialReadPurpose_{CredentialReadPurpose::None};
    bool pendingSave_{};
    bool testingSavedCredential_{};
    bool connectionOperationActive_{};
};
