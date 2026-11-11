#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QStringList>
#include <QUrl>

class QTimer;

class GroqClient final : public QObject {
    Q_OBJECT

public:
    explicit GroqClient(QObject *parent = nullptr);
    GroqClient(QString apiKey, QUrl endpoint, int timeoutMilliseconds,
               bool allowHttpForTests, QObject *parent = nullptr);

    [[nodiscard]] bool isConfigured() const;
    [[nodiscard]] bool isBusy() const;
    bool request(const QJsonArray &messages, const QJsonArray &tools, QString *error = nullptr);
    bool request(const QJsonArray &messages, const QJsonArray &tools, const QString &apiKey,
                 const QString &model, QString *error = nullptr);
    bool testConnection(const QString &apiKey, const QString &model);
    bool refreshModels(const QString &apiKey);
    void cancel();

signals:
    void finished(const QJsonObject &assistantMessage, const QString &error);
    void modelsReceived(const QStringList &models, const QString &error);
    void connectionTestFinished(bool success, const QString &message);

private:
    enum class Operation { Chat, ModelList, ConnectionModelList, ToolProbe };
    bool startRequest(const QNetworkRequest &request, const QByteArray &payload = {});
    void reportOperationError(const QString &error);
    void handleModelList(const QByteArray &response);
    void handleToolProbe(const QByteArray &response);
    void handleFinished(QNetworkReply *reply);

    QNetworkAccessManager network_;
    QString apiKey_;
    QString model_;
    QUrl endpoint_;
    int timeoutMilliseconds_{};
    QNetworkReply *activeReply_{};
    QTimer *timeoutTimer_{};
    bool timedOut_{};
    bool cancelled_{};
    bool responseTooLarge_{};
    Operation operation_{Operation::Chat};
    QString pendingApiKey_;
    QString pendingModel_;
};
