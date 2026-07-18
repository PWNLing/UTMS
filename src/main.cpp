#include <cstdlib>
#include <exception>

#include <QApplication>
#include <QDir>

#include "core/Logger.h"
#include "ui/LoginDialog.h"
#include "ui/mainwindow.h"

namespace {

[[noreturn]] void logUnhandledException() {
    try {
        const std::exception_ptr exception = std::current_exception();
        if (exception != nullptr) {
            std::rethrow_exception(exception);
        }
        qCritical() << "Application: terminated by an unknown fatal error";
    } catch (const std::exception &exception) {
        qCritical() << "Application: unhandled exception:" << exception.what();
    } catch (...) {
        qCritical() << "Application: unhandled non-standard exception";
    }
    std::abort();
}

} // namespace

int main(int argc, char *argv[]) {
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS")) {
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--log-level=3");
    }
    QApplication a(argc, argv);
    const QString log_directory = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"));
    if (!utms::Logger::install(log_directory)) {
        qWarning() << "Application: failed to initialize runtime log directory" << log_directory;
    }
    std::set_terminate(logUnhandledException);
    qInfo() << "Application: started";

    LoginDialog login_dialog;
    if (login_dialog.exec() != QDialog::Accepted) {
        qInfo() << "Application: login cancelled; exiting";
        utms::Logger::shutdown();
        return 0;
    }

    int exit_code = 0;
    {
        MainWindow w;
        w.show();
        exit_code = a.exec();
    }

    qInfo() << "Application: stopped with exit code" << exit_code;
    utms::Logger::shutdown();
    return exit_code;
}
