#include "LoginCredentialValidator.h"

#include <QString>

namespace utms {

LoginValidationResult LoginCredentialValidator::validate(const QString &account, const QString &password)
{
    if (account.isEmpty() || password.isEmpty()) {
        return LoginValidationResult::kMissingCredentials;
    }
    if (account == QStringLiteral("root") && password == QStringLiteral("123456")) {
        return LoginValidationResult::kAccepted;
    }
    return LoginValidationResult::kInvalidCredentials;
}

} // namespace utms
