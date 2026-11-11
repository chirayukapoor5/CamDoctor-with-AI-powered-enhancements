#include "groq_client.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include <memory>

namespace {
void replyToNextConnection(QTcpServer &server, const int status, const QByteArray &body)
{
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, status, body] {
        QTcpSocket *socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, status, body] {
            socket->readAll();
            const QByteArray reason = status == 200 ? QByteArrayLiteral("OK")
                : status == 429 ? QByteArrayLiteral("Too Many Requests")
                                : QByteArrayLiteral("Error");
            QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " "
                + reason + "\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
            response += body;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    });
}

void replyToSequence(QTcpServer &server, const QVector<QPair<int, QByteArray>> &responses,
                     int *authenticatedRequests = nullptr)
{
    auto nextResponse = std::make_shared<int>(0);
    QObject::connect(&server, &QTcpServer::newConnection, &server,
                     [&server, responses, nextResponse, authenticatedRequests] {
        QTcpSocket *socket = server.nextPendingConnection();
        const int index = *nextResponse;
        ++*nextResponse;
        if (index >= responses.size()) {
            socket->deleteLater();
            return;
        }
        const auto [status, body] = responses.at(index);
        QObject::connect(socket, &QTcpSocket::readyRead, socket,
                         [socket, status, body, authenticatedRequests] {
            const QByteArray requestData = socket->readAll();
            if (authenticatedRequests
                && requestData.toLower().contains(
                    QByteArrayLiteral("authorization: bearer dummy-test-credential"))) {
                ++*authenticatedRequests;
            }
            const QByteArray reason = status == 200 ? QByteArrayLiteral("OK")
                : status == 429 ? QByteArrayLiteral("Too Many Requests")
                                : QByteArrayLiteral("Error");
            QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " "
                + reason + "\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
            response += body;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    });
}

QUrl endpointFor(const QTcpServer &server)
{
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(QStringLiteral("127.0.0.1"));
    endpoint.setPort(server.serverPort());
    endpoint.setPath(QStringLiteral("/chat/completions"));
    return endpoint;
}

QByteArray assistantReply(const QByteArray &message)
{
    return QByteArrayLiteral("{\"choices\":[{\"message\":") + message
        + QByteArrayLiteral("}]}");
}
} // namespace

class GroqClientTests final : public QObject {
    Q_OBJECT

private slots:
    void doesNotLoadApiKeysFromEnvironment()
    {
        const QByteArray original = qgetenv("GROQ_API_KEY");
        const bool wasSet = qEnvironmentVariableIsSet("GROQ_API_KEY");
        qputenv("GROQ_API_KEY", QByteArrayLiteral("dummy-test-credential"));
        GroqClient client;
        QVERIFY(!client.isConfigured());
        if (wasSet) {
            qputenv("GROQ_API_KEY", original);
        } else {
            qunsetenv("GROQ_API_KEY");
        }
    }

    void parsesMockedToolCalls()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray body = assistantReply(QByteArrayLiteral(
            "{\"role\":\"assistant\",\"content\":\"Use monochrome.\",\"tool_calls\":["
            "{\"id\":\"call-1\",\"type\":\"function\",\"function\":{\"name\":\"apply_image_effects\","
            "\"arguments\":\"{\\\"grayscale\\\":true}\"}}]}"));
        replyToNextConnection(server, 200, body);
        GroqClient client(QStringLiteral("test-only-key"), endpointFor(server), 3000, true);
        QSignalSpy spy(&client, &GroqClient::finished);
        QVERIFY(spy.isValid());
        QVERIFY(client.request({QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                            {QStringLiteral("content"), QStringLiteral("mono")}}},
                               {}, nullptr));
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
        QCOMPARE(spy.first().at(1).toString(), QString());
        const auto message = spy.first().at(0).toJsonObject();
        QCOMPARE(message.value(QStringLiteral("content")).toString(), QStringLiteral("Use monochrome."));
        QCOMPARE(message.value(QStringLiteral("tool_calls")).toArray().size(), 1);
    }

    void reportsInvalidToolArgumentsAndRateLimits()
    {
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            const QByteArray body = assistantReply(QByteArrayLiteral(
                "{\"content\":\"\",\"tool_calls\":[{\"id\":\"call-bad\",\"type\":\"function\","
                "\"function\":{\"name\":\"set_exposure\",\"arguments\":\"{bad}\"}}]}"));
            replyToNextConnection(server, 200, body);
            GroqClient client(QStringLiteral("test-only-key"), endpointFor(server), 3000, true);
            QSignalSpy spy(&client, &GroqClient::finished);
            QVERIFY(client.request({}, {}, nullptr));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("invalid tool arguments")));
        }
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            replyToNextConnection(server, 429, QByteArrayLiteral("{}"));
            GroqClient client(QStringLiteral("test-only-key"), endpointFor(server), 3000, true);
            QSignalSpy spy(&client, &GroqClient::finished);
            QVERIFY(client.request({}, {}, nullptr));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("rate limit")));
        }
    }

    void rejectsInvalidApiJson()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        replyToNextConnection(server, 200, QByteArrayLiteral("not-json"));
        GroqClient client(QStringLiteral("test-only-key"), endpointFor(server), 3000, true);
        QSignalSpy spy(&client, &GroqClient::finished);
        QVERIFY(client.request({}, {}, nullptr));
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
        QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("invalid JSON")));
    }

    void testsAuthenticationAndSelectedModelToolCalling()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray models = QByteArrayLiteral(
            "{\"data\":[{\"id\":\"openai/gpt-oss-20b\"},{\"id\":\"other-model\"}]}");
        const QByteArray probe = QByteArrayLiteral(
            "{\"choices\":[{\"message\":{\"tool_calls\":[{\"type\":\"function\","
            "\"id\":\"verify-1\",\"function\":{\"name\":\"camdoctor_connection_probe\","
            "\"arguments\":\"{}\"}}]}}]}");
        int authenticatedRequests = 0;
        replyToSequence(server, {{200, models}, {200, probe}}, &authenticatedRequests);
        GroqClient client(QString(), endpointFor(server), 3000, true);
        QSignalSpy modelSpy(&client, &GroqClient::modelsReceived);
        QSignalSpy connectionSpy(&client, &GroqClient::connectionTestFinished);
        QVERIFY(client.testConnection(QStringLiteral("dummy-test-credential"),
                                      QStringLiteral("openai/gpt-oss-20b")));
        QTRY_COMPARE_WITH_TIMEOUT(connectionSpy.size(), 1, 4000);
        QVERIFY(modelSpy.size() == 1);
        QCOMPARE(modelSpy.first().at(0).toStringList().size(), 2);
        QVERIFY(connectionSpy.first().at(0).toBool());
        QVERIFY(connectionSpy.first().at(1).toString().contains(QStringLiteral("verified")));
        QCOMPARE(authenticatedRequests, 2);
    }

    void rejectsInvalidCredentialsAndUnsupportedToolModels()
    {
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            replyToNextConnection(server, 401, QByteArrayLiteral("{}"));
            GroqClient client(QString(), endpointFor(server), 3000, true);
            QSignalSpy spy(&client, &GroqClient::connectionTestFinished);
            QVERIFY(client.testConnection(QStringLiteral("dummy-test-credential"),
                                          QStringLiteral("openai/gpt-oss-20b")));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
            QVERIFY(!spy.first().at(0).toBool());
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("rejected")));
        }
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            replyToNextConnection(server, 403, QByteArrayLiteral("{}"));
            GroqClient client(QString(), endpointFor(server), 3000, true);
            QSignalSpy spy(&client, &GroqClient::connectionTestFinished);
            QVERIFY(client.testConnection(QStringLiteral("dummy-test-credential"),
                                          QStringLiteral("openai/gpt-oss-20b")));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
            QVERIFY(!spy.first().at(0).toBool());
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("permissions")));
        }
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            const QByteArray models = QByteArrayLiteral(
                "{\"data\":[{\"id\":\"openai/gpt-oss-20b\"}]}");
            const QByteArray noTool = QByteArrayLiteral(
                "{\"choices\":[{\"message\":{\"content\":\"No tool call.\"}}]}");
            replyToSequence(server, {{200, models}, {200, noTool}});
            GroqClient client(QString(), endpointFor(server), 3000, true);
            QSignalSpy spy(&client, &GroqClient::connectionTestFinished);
            QVERIFY(client.testConnection(QStringLiteral("dummy-test-credential"),
                                          QStringLiteral("openai/gpt-oss-20b")));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
            QVERIFY(!spy.first().at(0).toBool());
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("tool-call")));
        }
    }

    void handlesModelEndpointRateLimitsAndMalformedData()
    {
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            replyToNextConnection(server, 429, QByteArrayLiteral("{}"));
            GroqClient client(QString(), endpointFor(server), 3000, true);
            QSignalSpy spy(&client, &GroqClient::connectionTestFinished);
            QVERIFY(client.testConnection(QStringLiteral("dummy-test-credential"),
                                          QStringLiteral("openai/gpt-oss-20b")));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("rate limit")));
        }
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            replyToNextConnection(server, 200, QByteArrayLiteral("{malformed"));
            GroqClient client(QString(), endpointFor(server), 3000, true);
            QSignalSpy spy(&client, &GroqClient::connectionTestFinished);
            QVERIFY(client.testConnection(QStringLiteral("dummy-test-credential"),
                                          QStringLiteral("openai/gpt-oss-20b")));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
            QVERIFY(!spy.first().at(0).toBool());
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("malformed")));
        }
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            replyToNextConnection(server, 200,
                                  QByteArrayLiteral("{\"data\":[{\"id\":\"other-model\"}]}"));
            GroqClient client(QString(), endpointFor(server), 3000, true);
            QSignalSpy spy(&client, &GroqClient::connectionTestFinished);
            QVERIFY(client.testConnection(QStringLiteral("dummy-test-credential"),
                                          QStringLiteral("openai/gpt-oss-20b")));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
            QVERIFY(!spy.first().at(0).toBool());
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("not available")));
        }
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            GroqClient client(QString(), endpointFor(server), 1000, true);
            QSignalSpy spy(&client, &GroqClient::connectionTestFinished);
            QVERIFY(client.testConnection(QStringLiteral("dummy-test-credential"),
                                          QStringLiteral("openai/gpt-oss-20b")));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 3000);
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("timed out")));
        }
    }

    void handlesMultipleSequentialRequests()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        replyToNextConnection(server, 200, assistantReply(QByteArrayLiteral(
            "{\"role\":\"assistant\",\"content\":\"ready\"}")));
        GroqClient client(QStringLiteral("test-only-key"), endpointFor(server), 3000, true);
        QSignalSpy spy(&client, &GroqClient::finished);
        QVERIFY(client.request({}, {}, nullptr));
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 4000);
        QVERIFY(client.request({}, {}, nullptr));
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 4000);
        QCOMPARE(spy.at(0).at(1).toString(), QString());
        QCOMPARE(spy.at(1).at(1).toString(), QString());
    }

    void supportsTimeoutCancellationAndNoCredentialFallback()
    {
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            GroqClient client(QStringLiteral("test-only-key"), endpointFor(server), 1000, true);
            QSignalSpy spy(&client, &GroqClient::finished);
            QVERIFY(client.request({}, {}, nullptr));
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 3000);
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("timed out")));
        }
        {
            QTcpServer server;
            QVERIFY(server.listen(QHostAddress::LocalHost));
            GroqClient client(QStringLiteral("test-only-key"), endpointFor(server), 3000, true);
            QSignalSpy spy(&client, &GroqClient::finished);
            QVERIFY(client.request({}, {}, nullptr));
            client.cancel();
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 3000);
            QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("cancelled")));
        }
        {
            GroqClient client(QString(), QUrl(QStringLiteral("https://api.groq.com/")), 1000, false);
            QString error;
            QVERIFY(!client.request({}, {}, &error));
            QVERIFY(error.contains(QStringLiteral("Connect a Groq account")));
        }
    }
};

QTEST_GUILESS_MAIN(GroqClientTests)
#include "groq_client_tests.moc"
