#include "history/HistoryStore.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace utms {
namespace {

void setError(QString *error_message, const QString &message)
{
    if (error_message != nullptr) {
        *error_message = message;
    }
}

bool executeSchemaStatement(QSqlDatabase &database, const QString &statement, QString *error_message)
{
    QSqlQuery query(database);
    if (query.exec(statement)) {
        return true;
    }

    setError(error_message, QStringLiteral("执行历史数据库结构语句失败: %1").arg(query.lastError().text()));
    return false;
}

std::optional<int> samplingRateValue(HistorySamplingRate sampling_rate)
{
    switch (sampling_rate) {
    case HistorySamplingRate::kEveryFrame:
        return 0;
    case HistorySamplingRate::kOneFps:
        return 1;
    case HistorySamplingRate::kTwoFps:
        return 2;
    case HistorySamplingRate::kFiveFps:
        return 5;
    }
    return std::nullopt;
}

std::optional<HistorySamplingRate> samplingRateFromValue(int value)
{
    switch (value) {
    case 0:
        return HistorySamplingRate::kEveryFrame;
    case 1:
        return HistorySamplingRate::kOneFps;
    case 2:
        return HistorySamplingRate::kTwoFps;
    case 5:
        return HistorySamplingRate::kFiveFps;
    default:
        return std::nullopt;
    }
}

} // namespace

HistoryStore::HistoryStore(const QString &database_path)
    : database_path_(database_path),
      connection_name_(QStringLiteral("utms_history_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{}

HistoryStore::~HistoryStore()
{
    close();
}

bool HistoryStore::initialize(QString *error_message)
{
    if (initialized_) {
        return true;
    }

    const QFileInfo database_info(database_path_);
    QDir database_directory = database_info.dir();
    if (!database_directory.exists() && !database_directory.mkpath(QStringLiteral("."))) {
        setError(error_message, QStringLiteral("无法创建历史数据库目录: %1").arg(database_directory.absolutePath()));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name_);
    database.setDatabaseName(database_path_);
    if (!database.open()) {
        setError(error_message,
                 QStringLiteral("无法打开历史数据库 %1: %2").arg(database_path_, database.lastError().text()));
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_name_);
        return false;
    }

    if (!executeSchemaStatement(database,
                                QStringLiteral("CREATE TABLE IF NOT EXISTS app_config ("
                                               "key TEXT PRIMARY KEY,"
                                               "value TEXT NOT NULL"
                                               ")"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("INSERT OR IGNORE INTO app_config(key, value) "
                                               "VALUES('history_sampling_rate', '2')"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("INSERT OR IGNORE INTO app_config(key, value) "
                                               "VALUES('history_retention_days', '7')"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE TABLE IF NOT EXISTS history_sessions ("
                                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                               "started_at_ms INTEGER NOT NULL,"
                                               "ended_at_ms INTEGER,"
                                               "state TEXT NOT NULL CHECK(state IN "
                                               "('active', 'closed', 'abnormal'))"
                                               ")"),
                                error_message)) {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_name_);
        return false;
    }

    initialized_ = true;
    return true;
}

std::optional<HistoryConfiguration> HistoryStore::loadConfiguration(QString *error_message)
{
    if (!initialized_) {
        setError(error_message, QStringLiteral("历史数据库尚未初始化"));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT key, value FROM app_config"))) {
        setError(error_message, QStringLiteral("读取历史配置失败: %1").arg(query.lastError().text()));
        return std::nullopt;
    }

    HistoryConfiguration configuration;
    while (query.next()) {
        const QString key = query.value(0).toString();
        if (key == QStringLiteral("history_sampling_rate")) {
            bool converted = false;
            const int value = query.value(1).toString().toInt(&converted);
            const std::optional<HistorySamplingRate> sampling_rate =
                converted ? samplingRateFromValue(value) : std::nullopt;
            if (!sampling_rate.has_value()) {
                setError(error_message, QStringLiteral("历史采样频率配置无效"));
                return std::nullopt;
            }
            configuration.sampling_rate = sampling_rate.value();
        } else if (key == QStringLiteral("history_retention_days")) {
            bool converted = false;
            const int retention_days = query.value(1).toString().toInt(&converted);
            if (!converted || retention_days < 1 || retention_days > 30) {
                setError(error_message, QStringLiteral("历史保留天数配置无效"));
                return std::nullopt;
            }
            configuration.retention_days = retention_days;
        }
    }
    return configuration;
}

bool HistoryStore::saveConfiguration(const HistoryConfiguration &configuration, QString *error_message)
{
    if (!initialized_) {
        setError(error_message, QStringLiteral("历史数据库尚未初始化"));
        return false;
    }

    const std::optional<int> sampling_rate = samplingRateValue(configuration.sampling_rate);
    if (!sampling_rate.has_value()) {
        setError(error_message, QStringLiteral("历史采样频率配置无效"));
        return false;
    }
    if (configuration.retention_days < 1 || configuration.retention_days > 30) {
        setError(error_message, QStringLiteral("历史保留天数必须为 1–30 天"));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    if (!database.transaction()) {
        setError(error_message, QStringLiteral("启动历史配置事务失败: %1").arg(database.lastError().text()));
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO app_config(key, value) VALUES(?, ?)"));
    query.addBindValue(QStringLiteral("history_sampling_rate"));
    query.addBindValue(QString::number(sampling_rate.value()));
    if (!query.exec()) {
        database.rollback();
        setError(error_message, QStringLiteral("保存历史采样频率失败: %1").arg(query.lastError().text()));
        return false;
    }

    query.finish();
    query.bindValue(0, QStringLiteral("history_retention_days"));
    query.bindValue(1, QString::number(configuration.retention_days));
    if (!query.exec()) {
        database.rollback();
        setError(error_message, QStringLiteral("保存历史保留天数失败: %1").arg(query.lastError().text()));
        return false;
    }

    if (!database.commit()) {
        setError(error_message, QStringLiteral("提交历史配置失败: %1").arg(database.lastError().text()));
        return false;
    }
    return true;
}

std::optional<qint64> HistoryStore::startSession(const QDateTime &started_at, QString *error_message)
{
    if (!initialized_) {
        setError(error_message, QStringLiteral("历史数据库尚未初始化"));
        return std::nullopt;
    }
    if (!started_at.isValid()) {
        setError(error_message, QStringLiteral("历史会话开始时间无效"));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery active_query(database);
    if (!active_query.exec(QStringLiteral("SELECT id FROM history_sessions "
                                          "WHERE state = 'active' LIMIT 1"))) {
        setError(error_message, QStringLiteral("检查活动历史会话失败: %1").arg(active_query.lastError().text()));
        return std::nullopt;
    }
    if (active_query.next()) {
        setError(error_message, QStringLiteral("已经存在活动历史会话"));
        return std::nullopt;
    }

    QSqlQuery insert_query(database);
    insert_query.prepare(QStringLiteral("INSERT INTO history_sessions(started_at_ms, state) "
                                        "VALUES(?, 'active')"));
    insert_query.addBindValue(started_at.toMSecsSinceEpoch());
    if (!insert_query.exec()) {
        setError(error_message, QStringLiteral("创建历史会话失败: %1").arg(insert_query.lastError().text()));
        return std::nullopt;
    }

    bool converted = false;
    const qint64 session_id = insert_query.lastInsertId().toLongLong(&converted);
    if (!converted || session_id <= 0) {
        setError(error_message, QStringLiteral("历史会话已创建但无法读取会话 ID"));
        return std::nullopt;
    }
    return session_id;
}

bool HistoryStore::closeActiveSession(const QDateTime &ended_at, QString *error_message)
{
    if (!initialized_) {
        setError(error_message, QStringLiteral("历史数据库尚未初始化"));
        return false;
    }
    if (!ended_at.isValid()) {
        setError(error_message, QStringLiteral("历史会话结束时间无效"));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE history_sessions "
                                 "SET ended_at_ms = ?, state = 'closed' "
                                 "WHERE state = 'active'"));
    query.addBindValue(ended_at.toMSecsSinceEpoch());
    if (!query.exec()) {
        setError(error_message, QStringLiteral("关闭历史会话失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool HistoryStore::recoverAbandonedSessions(const QDateTime &recovered_at, QString *error_message)
{
    if (!initialized_) {
        setError(error_message, QStringLiteral("历史数据库尚未初始化"));
        return false;
    }
    if (!recovered_at.isValid()) {
        setError(error_message, QStringLiteral("历史会话恢复时间无效"));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE history_sessions "
                                 "SET ended_at_ms = ?, state = 'abnormal' "
                                 "WHERE state = 'active'"));
    query.addBindValue(recovered_at.toMSecsSinceEpoch());
    if (!query.exec()) {
        setError(error_message, QStringLiteral("修复异常历史会话失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

std::optional<QVector<HistorySession>> HistoryStore::loadSessions(QString *error_message)
{
    if (!initialized_) {
        setError(error_message, QStringLiteral("历史数据库尚未初始化"));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT id, started_at_ms, ended_at_ms, state "
                                   "FROM history_sessions ORDER BY id"))) {
        setError(error_message, QStringLiteral("读取历史会话失败: %1").arg(query.lastError().text()));
        return std::nullopt;
    }

    QVector<HistorySession> sessions;
    while (query.next()) {
        HistorySession session;
        session.id = query.value(0).toLongLong();
        session.started_at = QDateTime::fromMSecsSinceEpoch(query.value(1).toLongLong(), QTimeZone::UTC);
        if (!query.value(2).isNull()) {
            session.ended_at = QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong(), QTimeZone::UTC);
        }

        const QString state = query.value(3).toString();
        if (state == QStringLiteral("active")) {
            session.state = HistorySessionState::kActive;
        } else if (state == QStringLiteral("closed")) {
            session.state = HistorySessionState::kClosed;
        } else if (state == QStringLiteral("abnormal")) {
            session.state = HistorySessionState::kAbnormal;
        } else {
            setError(error_message, QStringLiteral("历史会话 %1 的状态无效").arg(session.id));
            return std::nullopt;
        }
        sessions.append(session);
    }
    return sessions;
}

void HistoryStore::close()
{
    if (!QSqlDatabase::contains(connection_name_)) {
        initialized_ = false;
        return;
    }

    {
        QSqlDatabase database = QSqlDatabase::database(connection_name_, false);
        if (database.isValid()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name_);
    initialized_ = false;
}

} // namespace utms
