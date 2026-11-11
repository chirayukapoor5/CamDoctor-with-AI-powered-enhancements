#include "secure_credential_store.h"

#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

class SecureCredentialStoreTests final : public QObject {
    Q_OBJECT

private slots:
    void isolatedCredentialCanBeSavedReplacedReadAndRemoved()
    {
        const QString service = QStringLiteral("CamDoctor-test-%1")
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        SecureCredentialStore store(service, QStringLiteral("groq-api-key"));

        QSignalSpy readSpy(&store, &SecureCredentialStore::readFinished);
        QSignalSpy writeSpy(&store, &SecureCredentialStore::writeFinished);
        QSignalSpy removeSpy(&store, &SecureCredentialStore::removeFinished);
        QVERIFY(readSpy.isValid());
        QVERIFY(writeSpy.isValid());
        QVERIFY(removeSpy.isValid());

        QVERIFY(store.read());
        QTRY_COMPARE_WITH_TIMEOUT(readSpy.size(), 1, 3000);
        QVERIFY(!readSpy.first().at(0).toBool());

        const QString firstValue = QStringLiteral("dummy-test-credential-one");
        QVERIFY(store.write(firstValue));
        QTRY_COMPARE_WITH_TIMEOUT(writeSpy.size(), 1, 3000);
        QVERIFY(writeSpy.first().at(0).toBool());

        SecureCredentialStore reopenedStore(service, QStringLiteral("groq-api-key"));
        QSignalSpy reopenedReadSpy(&reopenedStore, &SecureCredentialStore::readFinished);
        QVERIFY(reopenedReadSpy.isValid());
        QVERIFY(reopenedStore.read());
        QTRY_COMPARE_WITH_TIMEOUT(reopenedReadSpy.size(), 1, 3000);
        QVERIFY(reopenedReadSpy.first().at(0).toBool());
        QVERIFY(reopenedReadSpy.first().at(1).toString() == firstValue);

        const QString replacement = QStringLiteral("dummy-test-credential-two");
        QVERIFY(store.write(replacement));
        QTRY_COMPARE_WITH_TIMEOUT(writeSpy.size(), 2, 3000);
        QVERIFY(writeSpy.at(1).at(0).toBool());

        QVERIFY(reopenedStore.read());
        QTRY_COMPARE_WITH_TIMEOUT(reopenedReadSpy.size(), 2, 3000);
        QVERIFY(reopenedReadSpy.at(1).at(0).toBool());
        QVERIFY(reopenedReadSpy.at(1).at(1).toString() == replacement);

        QVERIFY(store.remove());
        QTRY_COMPARE_WITH_TIMEOUT(removeSpy.size(), 1, 3000);
        QVERIFY(removeSpy.first().at(0).toBool());
        QVERIFY(reopenedStore.read());
        QTRY_COMPARE_WITH_TIMEOUT(reopenedReadSpy.size(), 3, 3000);
        QVERIFY(!reopenedReadSpy.at(2).at(0).toBool());
    }
};

QTEST_GUILESS_MAIN(SecureCredentialStoreTests)
#include "secure_credential_store_tests.moc"
