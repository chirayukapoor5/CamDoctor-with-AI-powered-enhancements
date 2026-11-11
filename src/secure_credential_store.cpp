#include "secure_credential_store.h"

#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#else
#include <qtkeychain/keychain.h>
#endif

namespace {
QString windowsTarget(const QString &service, const QString &account)
{
    return service + QLatin1Char('/') + account;
}
} // namespace

SecureCredentialStore::SecureCredentialStore(QObject *parent)
    : SecureCredentialStore(QStringLiteral("CamDoctor"),
                            QStringLiteral("groq-api-key"), parent)
{
}

SecureCredentialStore::SecureCredentialStore(QString service, QString account, QObject *parent)
    : QObject(parent), service_(std::move(service)), account_(std::move(account))
{
}

bool SecureCredentialStore::isBusy() const
{
    return busy_;
}

bool SecureCredentialStore::read()
{
    if (busy_) {
        return false;
    }
    busy_ = true;
#ifdef Q_OS_WIN
    QTimer::singleShot(0, this, [this] {
        const QString target = windowsTarget(service_, account_);
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0,
                       &credential)) {
            const DWORD error = GetLastError();
            busy_ = false;
            if (error == ERROR_NOT_FOUND) {
                emit readFinished(false, {}, QStringLiteral("No Groq API key is saved."));
            } else {
                emit readFinished(false, {}, QStringLiteral("Windows Credential Manager could not read the Groq API key."));
            }
            return;
        }

        QString secret;
        if (credential->CredentialBlob
            && credential->CredentialBlobSize % sizeof(wchar_t) == 0) {
            secret = QString::fromWCharArray(
                reinterpret_cast<const wchar_t *>(credential->CredentialBlob),
                static_cast<qsizetype>(credential->CredentialBlobSize / sizeof(wchar_t)));
        }
        CredFree(credential);
        busy_ = false;
        if (secret.isEmpty()) {
            emit readFinished(false, {}, QStringLiteral("The saved Groq API key is empty or invalid."));
            return;
        }
        emit readFinished(true, secret, {});
    });
#else
    auto *job = new QKeychain::ReadPasswordJob(service_, this);
    job->setKey(account_);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job] {
        const auto errorCode = job->error();
        const QString secret = errorCode == QKeychain::NoError ? job->textData() : QString();
        const QString error = errorCode == QKeychain::EntryNotFound
            ? QStringLiteral("No Groq API key is saved.")
            : errorCode != QKeychain::NoError
                ? QStringLiteral("Secure credential storage could not read the Groq API key: %1")
                      .arg(job->errorString())
                : secret.isEmpty()
                    ? QStringLiteral("The saved Groq API key is empty or invalid.")
                    : QString();
        busy_ = false;
        emit readFinished(errorCode == QKeychain::NoError && !secret.isEmpty(), secret, error);
    });
    job->start();
#endif
    return true;
}

bool SecureCredentialStore::write(const QString &secret)
{
    if (busy_ || secret.trimmed().isEmpty()) {
        return false;
    }
    busy_ = true;
#ifdef Q_OS_WIN
    const QString target = windowsTarget(service_, account_);
    const QString value = secret;
    QTimer::singleShot(0, this, [this, target, value] {
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(
            reinterpret_cast<LPCWSTR>(target.utf16()));
        credential.CredentialBlobSize = static_cast<DWORD>(
            value.size() * static_cast<qsizetype>(sizeof(wchar_t)));
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(
            const_cast<wchar_t *>(reinterpret_cast<const wchar_t *>(value.utf16())));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (!CredWriteW(&credential, 0)) {
            busy_ = false;
            emit writeFinished(false, QStringLiteral(
                "Windows Credential Manager could not save the Groq API key."));
            return;
        }
        busy_ = false;
        emit writeFinished(true, {});
    });
#else
    auto *job = new QKeychain::WritePasswordJob(service_, this);
    job->setKey(account_);
    job->setTextData(secret);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job] {
        const bool success = job->error() == QKeychain::NoError;
        const QString error = success
            ? QString()
            : QStringLiteral("Secure credential storage could not save the Groq API key: %1")
                  .arg(job->errorString());
        busy_ = false;
        emit writeFinished(success, error);
    });
    job->start();
#endif
    return true;
}

bool SecureCredentialStore::remove()
{
    if (busy_) {
        return false;
    }
    busy_ = true;
#ifdef Q_OS_WIN
    QTimer::singleShot(0, this, [this] {
        const QString target = windowsTarget(service_, account_);
        if (!CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0)) {
            const DWORD error = GetLastError();
            busy_ = false;
            if (error == ERROR_NOT_FOUND) {
                emit removeFinished(true, {});
            } else {
                emit removeFinished(false, QStringLiteral(
                    "Windows Credential Manager could not remove the Groq API key."));
            }
            return;
        }
        busy_ = false;
        emit removeFinished(true, {});
    });
#else
    auto *job = new QKeychain::DeletePasswordJob(service_, this);
    job->setKey(account_);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [this, job] {
        const bool success = job->error() == QKeychain::NoError
            || job->error() == QKeychain::EntryNotFound;
        const QString error = success
            ? QString()
            : QStringLiteral("Secure credential storage could not remove the Groq API key: %1")
                  .arg(job->errorString());
        busy_ = false;
        emit removeFinished(success, error);
    });
    job->start();
#endif
    return true;
}
