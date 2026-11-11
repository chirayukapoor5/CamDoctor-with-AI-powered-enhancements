#pragma once

#include <QObject>
#include <QString>

class SecureCredentialStore final : public QObject {
    Q_OBJECT

public:
    explicit SecureCredentialStore(QObject *parent = nullptr);
    SecureCredentialStore(QString service, QString account, QObject *parent = nullptr);

    [[nodiscard]] bool isBusy() const;
    bool read();
    bool write(const QString &secret);
    bool remove();

signals:
    void readFinished(bool success, const QString &secret, const QString &error);
    void writeFinished(bool success, const QString &error);
    void removeFinished(bool success, const QString &error);

private:
    QString service_;
    QString account_;
    bool busy_{};
};
