#include "core/Logger.h"

#include <cstdio>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace utms {
namespace {

struct LoggerState {
    QMutex mutex_;
    QFile file_;
    QString log_directory_;
    qint64 max_file_size_bytes_ = Logger::kDefaultMaxFileSizeBytes;
    int max_file_count_ = Logger::kDefaultMaxFileCount;
    QtMessageHandler previous_handler_ = nullptr;
    bool installed_ = false;
};

LoggerState &loggerState() {
    static LoggerState state;
    return state;
}

QString currentLogPath(const LoggerState &state) {
    return QDir(state.log_directory_).filePath(QStringLiteral("utms.log"));
}

QString archivedLogPath(const LoggerState &state, int archive_index) {
    return QDir(state.log_directory_).filePath(QStringLiteral("utms.%1.log").arg(archive_index));
}

bool openCurrentLog(LoggerState &state) {
    state.file_.setFileName(currentLogPath(state));
    return state.file_.open(QIODevice::WriteOnly | QIODevice::Append);
}

bool rotateLogs(LoggerState &state) {
    state.file_.close();

    if (state.max_file_count_ <= 1) {
        if (!QFile::remove(currentLogPath(state)) && QFileInfo::exists(currentLogPath(state))) {
            return false;
        }
        return openCurrentLog(state);
    }

    const QString oldest_archive = archivedLogPath(state, state.max_file_count_ - 1);
    if (!QFile::remove(oldest_archive) && QFileInfo::exists(oldest_archive)) {
        return false;
    }

    // 从最旧归档向后移动，避免重命名时覆盖仍需保留的文件。
    for (int archive_index = state.max_file_count_ - 2; archive_index >= 1; --archive_index) {
        const QString source = archivedLogPath(state, archive_index);
        if (QFileInfo::exists(source) && !QFile::rename(source, archivedLogPath(state, archive_index + 1))) {
            return false;
        }
    }

    const QString current_path = currentLogPath(state);
    if (QFileInfo::exists(current_path) && !QFile::rename(current_path, archivedLogPath(state, 1))) {
        return false;
    }
    return openCurrentLog(state);
}

QByteArray formattedMessage(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    const char *level = "INFO";
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARNING";
        break;
    case QtCriticalMsg:
        level = "CRITICAL";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }

    QString line = QStringLiteral("%1 [%2]").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), QString::fromLatin1(level));
    if (context.category != nullptr && context.category[0] != '\0') {
        line += QStringLiteral(" [%1]").arg(QString::fromUtf8(context.category));
    }
    line += QStringLiteral(" %1\n").arg(message);
    return line.toUtf8();
}

void runtimeMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    LoggerState &state = loggerState();
    QByteArray line = formattedMessage(type, context, message);
    QtMessageHandler previous_handler = nullptr;

    {
        QMutexLocker locker(&state.mutex_);
        previous_handler = state.previous_handler_;
        if (state.file_.isOpen()) {
            if (line.size() > state.max_file_size_bytes_) {
                line.truncate(static_cast<qsizetype>(state.max_file_size_bytes_ - 1));
                line.append('\n');
            }
            if (state.file_.size() > 0 && state.file_.size() + line.size() > state.max_file_size_bytes_) {
                if (!rotateLogs(state)) {
                    state.file_.close();
                }
            }
            if (state.file_.isOpen()) {
                const qint64 bytes_written = state.file_.write(line);
                if (bytes_written != line.size() || !state.file_.flush()) {
                    state.file_.close();
                }
            }
        }
    }

    if (previous_handler != nullptr) {
        previous_handler(type, context, message);
        return;
    }

#ifndef NDEBUG
    const size_t bytes_written = std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    if (bytes_written != static_cast<size_t>(line.size()) || std::fflush(stderr) != 0) {
        return;
    }
#endif
}

} // namespace

bool Logger::install(const QString &log_directory, qint64 max_file_size_bytes, int max_file_count) {
    LoggerState &state = loggerState();
    QMutexLocker locker(&state.mutex_);
    if (state.installed_) {
        return true;
    }
    if (log_directory.isEmpty() || max_file_size_bytes <= 0 || max_file_count <= 0) {
        return false;
    }

    QDir directory(log_directory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return false;
    }

    state.log_directory_ = directory.absolutePath();
    state.max_file_size_bytes_ = max_file_size_bytes;
    state.max_file_count_ = max_file_count;
    if (!openCurrentLog(state)) {
        state.log_directory_.clear();
        return false;
    }
    if (state.file_.size() >= state.max_file_size_bytes_ && !rotateLogs(state)) {
        state.file_.close();
        state.log_directory_.clear();
        return false;
    }

    state.previous_handler_ = qInstallMessageHandler(runtimeMessageHandler);
    state.installed_ = true;
    return true;
}

void Logger::shutdown() {
    LoggerState &state = loggerState();
    QMutexLocker locker(&state.mutex_);
    if (!state.installed_) {
        return;
    }

    qInstallMessageHandler(state.previous_handler_);
    state.previous_handler_ = nullptr;
    if (state.file_.isOpen() && !state.file_.flush()) {
        const QByteArray warning = QByteArrayLiteral("Logger: failed to flush runtime log during shutdown\n");
        const size_t bytes_written = std::fwrite(warning.constData(), 1, static_cast<size_t>(warning.size()), stderr);
        if (bytes_written != static_cast<size_t>(warning.size())) {
            state.file_.close();
        }
    }
    state.file_.close();
    state.installed_ = false;
    state.log_directory_.clear();
}

QString Logger::logFilePath() {
    LoggerState &state = loggerState();
    QMutexLocker locker(&state.mutex_);
    return state.log_directory_.isEmpty() ? QString() : currentLogPath(state);
}

} // namespace utms
