#include <QtTest>

#include "core/LoginCredentialValidator.h"

class LoginCredentialValidatorTest : public QObject {
    Q_OBJECT

private slots:
    void acceptsFixedDemonstrationCredentials();
    void identifiesMissingCredentials_data();
    void identifiesMissingCredentials();
    void rejectsIncorrectCredentials_data();
    void rejectsIncorrectCredentials();
};

void LoginCredentialValidatorTest::acceptsFixedDemonstrationCredentials()
{
    QCOMPARE(utms::LoginCredentialValidator::validate(QStringLiteral("root"), QStringLiteral("123456")),
             utms::LoginValidationResult::kAccepted);
}

void LoginCredentialValidatorTest::identifiesMissingCredentials_data()
{
    QTest::addColumn<QString>("account");
    QTest::addColumn<QString>("password");

    QTest::newRow("missing-account") << QString() << QStringLiteral("123456");
    QTest::newRow("missing-password") << QStringLiteral("root") << QString();
    QTest::newRow("missing-both") << QString() << QString();
}

void LoginCredentialValidatorTest::identifiesMissingCredentials()
{
    QFETCH(QString, account);
    QFETCH(QString, password);

    QCOMPARE(utms::LoginCredentialValidator::validate(account, password),
             utms::LoginValidationResult::kMissingCredentials);
}

void LoginCredentialValidatorTest::rejectsIncorrectCredentials_data()
{
    QTest::addColumn<QString>("account");
    QTest::addColumn<QString>("password");

    QTest::newRow("incorrect-account") << QStringLiteral("operator") << QStringLiteral("123456");
    QTest::newRow("incorrect-password") << QStringLiteral("root") << QStringLiteral("654321");
    QTest::newRow("surrounding-whitespace") << QStringLiteral(" root ") << QStringLiteral("123456");
}

void LoginCredentialValidatorTest::rejectsIncorrectCredentials()
{
    QFETCH(QString, account);
    QFETCH(QString, password);

    QCOMPARE(utms::LoginCredentialValidator::validate(account, password),
             utms::LoginValidationResult::kInvalidCredentials);
}

QTEST_APPLESS_MAIN(LoginCredentialValidatorTest)

#include "test_login_credentials.moc"
