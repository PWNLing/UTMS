#pragma once

class QString;

namespace utms {

enum class LoginValidationResult {
    kAccepted,
    kMissingCredentials,
    kInvalidCredentials
};

class LoginCredentialValidator {
public:
    static LoginValidationResult validate(const QString &account, const QString &password);
};

} // namespace utms
