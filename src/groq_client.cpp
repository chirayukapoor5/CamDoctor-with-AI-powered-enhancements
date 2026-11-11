#include "groq_client.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QTimer>

#include <utility>

namespace {
constexpr auto kEndpoint = "https://api.groq.com/openai/v1/chat/completions";
constexpr auto kDefaultModel = "openai/gpt-oss-20b";
constexpr qint64 kMaximumResponseBytes = 1024 * 1024;

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

QUrl modelsEndpoint(const QUrl &chatEndpoint)
{
    QUrl endpoint = chatEndpoint;
    endpoint.setPath(QStringLiteral("/openai/v1/models"));
    return endpoint;
}
} // namespace

GroqClient::GroqClient(QObject *parent)
    : GroqClient({}, QUrl(QString::fromLatin1(kEndpoint)), 30000, false, parent)
{
}

GroqClient::GroqClient(QString apiKey, QUrl endpoint, const int timeoutMilliseconds,
                       const bool allowHttpForTests, QObject *parent)
    : QObject(parent),
      network_(this),
      apiKey_(std::move(apiKey)),
      model_(QString::fromLatin1(kDefaultModel)),
      endpoint_(std::move(endpoint)),
      timeoutMilliseconds_(qBound(1000, timeoutMilliseconds, 120000))
{
    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, [this] {
        if (activeReply_) {
            timedOut_ = true;
            activeReply_->abort();
        }
    });

    const bool secureEndpoint = endpoint_.scheme() == QLatin1String("https");
    const bool testLoopback = allowHttpForTests
        && endpoint_.scheme() == QLatin1String("http")
        && (endpoint_.host() == QLatin1String("127.0.0.1")
            || endpoint_.host() == QLatin1String("localhost"));
    if (!secureEndpoint && !testLoopback) {
        endpoint_.clear();
    }
}

bool GroqClient::isConfigured() const
{
    return !apiKey_.trimmed().isEmpty() && endpoint_.isValid() && !endpoint_.isEmpty();
}

bool GroqClient::isBusy() const
{
    return activeReply_ != nullptr;
}

bool GroqClient::request(const QJsonArray &messages, const QJsonArray &tools, QString *error)
{
    return request(messages, tools, apiKey_, model_, error);
}

bool GroqClient::request(const QJsonArray &messages, const QJsonArray &tools,
                         const QString &apiKey, const QString &model, QString *error)
{
    if (isBusy()) {
        setError(error, QStringLiteral("A Groq request is already in progress."));
        return false;
    }
    if (apiKey.trimmed().isEmpty() || !endpoint_.isValid() || endpoint_.isEmpty()) {
        setError(error, QStringLiteral("Connect a Groq account in Settings before making AI requests."));
        return false;
    }
    if (model.trimmed().isEmpty()) {
        setError(error, QStringLiteral("Select a Groq model in Settings."));
        return false;
    }
    const QJsonObject body{
        {QStringLiteral("model"), model},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("tool_choice"), QStringLiteral("auto")},
        {QStringLiteral("temperature"), 0.2},
        {QStringLiteral("max_tokens"), 1200},
    };
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (payload.size() > 64 * 1024) {
        setError(error, QStringLiteral("The assistant request exceeds the 64 KiB limit."));
        return false;
    }

    QNetworkRequest networkRequest(endpoint_);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    operation_ = Operation::Chat;
    return startRequest(networkRequest, payload);
}

bool GroqClient::testConnection(const QString &apiKey, const QString &model)
{
    if (isBusy() || apiKey.trimmed().isEmpty() || model.trimmed().isEmpty()
        || !endpoint_.isValid() || endpoint_.isEmpty()) {
        return false;
    }
    pendingApiKey_ = apiKey;
    pendingModel_ = model;
    operation_ = Operation::ConnectionModelList;
    QNetworkRequest request(modelsEndpoint(endpoint_));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    return startRequest(request);
}

bool GroqClient::refreshModels(const QString &apiKey)
{
    if (isBusy() || apiKey.trimmed().isEmpty() || !endpoint_.isValid() || endpoint_.isEmpty()) {
        return false;
    }
    operation_ = Operation::ModelList;
    QNetworkRequest request(modelsEndpoint(endpoint_));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    return startRequest(request);
}

bool GroqClient::startRequest(const QNetworkRequest &request, const QByteArray &payload)
{
    timedOut_ = false;
    cancelled_ = false;
    responseTooLarge_ = false;
    activeReply_ = payload.isEmpty() ? network_.get(request) : network_.post(request, payload);
    connect(activeReply_, &QNetworkReply::downloadProgress, this,
            [this](const qint64 received, qint64) {
                if (activeReply_ && received > kMaximumResponseBytes) {
                    responseTooLarge_ = true;
                    activeReply_->abort();
                }
            });
    connect(activeReply_, &QNetworkReply::finished, this,
            [this, reply = activeReply_] { handleFinished(reply); });
    timeoutTimer_->start(timeoutMilliseconds_);
    return true;
}

void GroqClient::cancel()
{
    if (activeReply_) {
        cancelled_ = true;
        activeReply_->abort();
    }
}

void GroqClient::handleFinished(QNetworkReply *reply)
{
    if (reply != activeReply_) {
        reply->deleteLater();
        return;
    }

    timeoutTimer_->stop();
    activeReply_ = nullptr;
    const bool timedOut = std::exchange(timedOut_, false);
    const bool cancelled = std::exchange(cancelled_, false);
    const bool responseTooLarge = std::exchange(responseTooLarge_, false);
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    const QByteArray response = reply->readAll();
    reply->deleteLater();

    if (cancelled) {
        reportOperationError(QStringLiteral("Groq request cancelled."));
        return;
    }
    if (timedOut) {
        reportOperationError(QStringLiteral("Groq request timed out."));
        return;
    }
    if (responseTooLarge) {
        reportOperationError(QStringLiteral("Groq response exceeded the 1 MiB size limit."));
        return;
    }
    if (status == 429) {
        reportOperationError(QStringLiteral("Groq rate limit reached. Wait before trying again."));
        return;
    }
    if (status == 401 || status == 403) {
        reportOperationError(QStringLiteral("Groq rejected the API key or account permissions."));
        return;
    }
    if (status < 200 || status >= 300 || networkError != QNetworkReply::NoError) {
        reportOperationError(status > 0
            ? QStringLiteral("Groq request failed (HTTP %1).").arg(status)
            : QStringLiteral("Groq network request failed."));
        return;
    }

    if (operation_ == Operation::ModelList || operation_ == Operation::ConnectionModelList) {
        handleModelList(response);
        return;
    }
    if (operation_ == Operation::ToolProbe) {
        handleToolProbe(response);
        return;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit finished({}, QStringLiteral("Groq returned invalid JSON."));
        return;
    }
    const auto choices = document.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject()) {
        emit finished({}, QStringLiteral("Groq returned no assistant response."));
        return;
    }
    const auto message = choices.first().toObject().value(QStringLiteral("message"));
    if (!message.isObject()) {
        emit finished({}, QStringLiteral("Groq returned an invalid assistant message."));
        return;
    }
    const auto assistantMessage = message.toObject();
    const auto calls = assistantMessage.value(QStringLiteral("tool_calls"));
    if (!calls.isUndefined()) {
        if (!calls.isArray() || calls.toArray().size() > 8) {
            emit finished({}, QStringLiteral("Groq returned an invalid number of tool calls."));
            return;
        }
        for (const auto &callValue : calls.toArray()) {
            const auto call = callValue.toObject();
            const auto function = call.value(QStringLiteral("function")).toObject();
            const auto arguments = function.value(QStringLiteral("arguments"));
            if (call.value(QStringLiteral("type")).toString() != QLatin1String("function")
                || call.value(QStringLiteral("id")).toString().isEmpty()
                || function.value(QStringLiteral("name")).toString().isEmpty()
                || !arguments.isString()) {
                emit finished({}, QStringLiteral("Groq returned a malformed tool call."));
                return;
            }
            QJsonParseError argumentError;
            const auto argumentDocument =
                QJsonDocument::fromJson(arguments.toString().toUtf8(), &argumentError);
            if (argumentError.error != QJsonParseError::NoError || !argumentDocument.isObject()) {
                emit finished({}, QStringLiteral("Groq returned invalid tool arguments."));
                return;
            }
        }
    }
    if (assistantMessage.value(QStringLiteral("content")).toString().isEmpty()
        && calls.toArray().isEmpty()) {
        emit finished({}, QStringLiteral("Groq returned neither an explanation nor tool calls."));
        return;
    }
    emit finished(assistantMessage, {});
}

void GroqClient::reportOperationError(const QString &error)
{
    if (operation_ == Operation::Chat) {
        emit finished({}, error);
    } else if (operation_ == Operation::ModelList) {
        emit modelsReceived({}, error);
    } else {
        pendingApiKey_.clear();
        pendingModel_.clear();
        emit connectionTestFinished(false, error);
    }
}

void GroqClient::handleModelList(const QByteArray &response)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || !document.object().value(QStringLiteral("data")).isArray()) {
        reportOperationError(QStringLiteral("Groq returned a malformed model list."));
        return;
    }
    QStringList models;
    for (const QJsonValue &value : document.object().value(QStringLiteral("data")).toArray()) {
        if (value.isObject()) {
            const QString id = value.toObject().value(QStringLiteral("id")).toString().trimmed();
            if (!id.isEmpty() && !models.contains(id)) {
                models.push_back(id);
            }
        }
    }
    if (models.isEmpty()) {
        reportOperationError(QStringLiteral("Groq returned no available models."));
        return;
    }
    if (operation_ == Operation::ModelList) {
        emit modelsReceived(models, {});
        return;
    }
    if (!models.contains(pendingModel_)) {
        emit modelsReceived(models, {});
        reportOperationError(QStringLiteral("The selected model is not available to this Groq account."));
        return;
    }

    QJsonObject tool{
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("camdoctor_connection_probe")},
            {QStringLiteral("description"), QStringLiteral("Confirms tool calling support.")},
            {QStringLiteral("parameters"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("properties"), QJsonObject{}},
                {QStringLiteral("required"), QJsonArray{}},
                {QStringLiteral("additionalProperties"), false},
            }},
        }},
    };
    const QJsonObject body{
        {QStringLiteral("model"), pendingModel_},
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), QStringLiteral("Call the supplied verification tool.")}},
        }},
        {QStringLiteral("tools"), QJsonArray{tool}},
        {QStringLiteral("tool_choice"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("camdoctor_connection_probe")},
            }},
        }},
        {QStringLiteral("max_tokens"), 32},
        {QStringLiteral("temperature"), 0},
    };
    QNetworkRequest request(endpoint_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + pendingApiKey_.toUtf8());
    operation_ = Operation::ToolProbe;
    startRequest(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    emit modelsReceived(models, {});
}

void GroqClient::handleToolProbe(const QByteArray &response)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        reportOperationError(QStringLiteral("Groq returned malformed tool-call verification data."));
        return;
    }
    const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject()) {
        reportOperationError(QStringLiteral("The selected model did not return a tool-call response."));
        return;
    }
    const QJsonObject message = choices.first().toObject()
                                   .value(QStringLiteral("message")).toObject();
    const QJsonArray calls = message.value(QStringLiteral("tool_calls")).toArray();
    bool verified = false;
    for (const QJsonValue &callValue : calls) {
        const QJsonObject function = callValue.toObject().value(QStringLiteral("function")).toObject();
        if (function.value(QStringLiteral("name")).toString()
                == QLatin1String("camdoctor_connection_probe")) {
            QJsonParseError argumentsError;
            const QJsonDocument arguments = QJsonDocument::fromJson(
                function.value(QStringLiteral("arguments")).toString().toUtf8(), &argumentsError);
            verified = argumentsError.error == QJsonParseError::NoError
                && arguments.isObject() && arguments.object().isEmpty();
            break;
        }
    }
    pendingApiKey_.clear();
    pendingModel_.clear();
    emit connectionTestFinished(
        verified,
        verified ? QStringLiteral("Authentication and model tool calling verified.")
                 : QStringLiteral("The selected model did not pass tool-call verification."));
}
