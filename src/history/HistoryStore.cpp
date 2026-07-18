#include "history/HistoryStore.h"

#include <cmath>
#include <limits>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <QVariant>

#include "core/GeofenceGeometry.h"
#include "history/HistoryCsvExporter.h"

namespace utms {
namespace {

void setError(QString *error_message, const QString &message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
}

QString storeText(const char *source_text) { return QCoreApplication::translate("HistoryStore", source_text); }

void setTransactionError(QSqlDatabase &database, QString *error_message, const QString &operation_error) {
    if (!database.rollback()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "%1；事务回滚失败：%2"))
                                    .arg(operation_error, database.lastError().text()));
        return;
    }
    setError(error_message, operation_error);
}

bool executeSchemaStatement(QSqlDatabase &database, const QString &statement, QString *error_message) {
    QSqlQuery query(database);
    if (query.exec(statement)) {
        return true;
    }

    setError(
        error_message,
        storeText(QT_TRANSLATE_NOOP("HistoryStore", "执行历史数据库结构语句失败：%1")).arg(query.lastError().text()));
    return false;
}

std::optional<int> samplingRateValue(HistorySamplingRate sampling_rate) {
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

std::optional<HistorySamplingRate> samplingRateFromValue(int value) {
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

bool isValidTargetTypeValue(int value) {
    return value >= static_cast<int>(TargetType::kCar) && value <= static_cast<int>(TargetType::kUnknown);
}

int targetTypeMask(const QVector<TargetType> &target_types) {
    int mask = 0;
    for (TargetType type : target_types) {
        mask |= 1 << static_cast<int>(type);
    }
    return mask;
}

std::optional<QVector<TargetType>> targetTypesFromMask(int mask) {
    constexpr int kAllTargetTypesMask = (1 << (static_cast<int>(TargetType::kUnknown) + 1)) - 1;
    if (mask <= 0 || (mask & ~kAllTargetTypesMask) != 0) {
        return std::nullopt;
    }
    QVector<TargetType> target_types;
    for (TargetType type : kTargetTypes) {
        if ((mask & (1 << static_cast<int>(type))) != 0) {
            target_types.append(type);
        }
    }
    return target_types;
}

bool isValidAlertRuleTypeValue(int value) {
    return value >= static_cast<int>(AlertRuleType::kStableEntry) &&
           value <= static_cast<int>(AlertRuleType::kStableExit);
}

bool isValidAlertSeverityValue(int value) {
    return value >= static_cast<int>(AlertSeverity::kInfo) && value <= static_cast<int>(AlertSeverity::kSevere);
}

QVariant optionalDoubleValue(const std::optional<double> &value) {
    return value.has_value() ? QVariant(value.value()) : QVariant();
}

QVariant optionalIntegerValue(const std::optional<qint64> &value) {
    return value.has_value() ? QVariant(value.value()) : QVariant();
}

qint64 frameTimeMs(const RadarFrame &frame) {
    if (frame.sender_timestamp_seconds.has_value() && std::isfinite(frame.sender_timestamp_seconds.value())) {
        return qRound64(frame.sender_timestamp_seconds.value() * 1'000.0);
    }
    return frame.received_at.toMSecsSinceEpoch();
}

QJsonObject positionObject(const GeoPosition &position) {
    return {{QStringLiteral("latitude"), position.latitude}, {QStringLiteral("longitude"), position.longitude}};
}

std::optional<GeoPosition> positionFromObject(const QJsonValue &value) {
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue latitude = object.value(QStringLiteral("latitude"));
    const QJsonValue longitude = object.value(QStringLiteral("longitude"));
    if (!latitude.isDouble() || !longitude.isDouble()) {
        return std::nullopt;
    }
    return GeoPosition{latitude.toDouble(), longitude.toDouble()};
}

QString shapeStorageName(GeofenceShape shape) {
    switch (shape) {
    case GeofenceShape::kCircle:
        return QStringLiteral("circle");
    case GeofenceShape::kRectangle:
        return QStringLiteral("rectangle");
    case GeofenceShape::kPolygon:
        return QStringLiteral("polygon");
    }
    return {};
}

std::optional<GeofenceShape> shapeFromStorageName(const QString &name) {
    if (name == QStringLiteral("circle")) {
        return GeofenceShape::kCircle;
    }
    if (name == QStringLiteral("rectangle")) {
        return GeofenceShape::kRectangle;
    }
    if (name == QStringLiteral("polygon")) {
        return GeofenceShape::kPolygon;
    }
    return std::nullopt;
}

QJsonObject geometryObject(const Geofence &geofence) {
    if (const auto *circle = std::get_if<CircleGeofence>(&geofence.geometry); circle != nullptr) {
        return {{QStringLiteral("center"), positionObject(circle->center)},
                {QStringLiteral("radius_m"), circle->radius_m}};
    }
    if (const auto *rectangle = std::get_if<RectangleGeofence>(&geofence.geometry); rectangle != nullptr) {
        return {{QStringLiteral("southwest"), positionObject(rectangle->southwest)},
                {QStringLiteral("northeast"), positionObject(rectangle->northeast)}};
    }

    QJsonArray vertices;
    for (const GeoPosition &vertex : std::get<PolygonGeofence>(geofence.geometry).vertices) {
        vertices.append(positionObject(vertex));
    }
    return {{QStringLiteral("vertices"), vertices}};
}

std::optional<GeofenceGeometry> geometryFromObject(GeofenceShape shape, const QJsonObject &object) {
    switch (shape) {
    case GeofenceShape::kCircle: {
        const std::optional<GeoPosition> center = positionFromObject(object.value(QStringLiteral("center")));
        const QJsonValue radius = object.value(QStringLiteral("radius_m"));
        if (!center.has_value() || !radius.isDouble()) {
            return std::nullopt;
        }
        return CircleGeofence{center.value(), radius.toDouble()};
    }
    case GeofenceShape::kRectangle: {
        const std::optional<GeoPosition> southwest = positionFromObject(object.value(QStringLiteral("southwest")));
        const std::optional<GeoPosition> northeast = positionFromObject(object.value(QStringLiteral("northeast")));
        if (!southwest.has_value() || !northeast.has_value()) {
            return std::nullopt;
        }
        return RectangleGeofence{southwest.value(), northeast.value()};
    }
    case GeofenceShape::kPolygon: {
        const QJsonValue vertices_value = object.value(QStringLiteral("vertices"));
        if (!vertices_value.isArray()) {
            return std::nullopt;
        }
        QVector<GeoPosition> vertices;
        for (const QJsonValue &vertex_value : vertices_value.toArray()) {
            const std::optional<GeoPosition> vertex = positionFromObject(vertex_value);
            if (!vertex.has_value()) {
                return std::nullopt;
            }
            vertices.append(vertex.value());
        }
        return PolygonGeofence{vertices};
    }
    }
    return std::nullopt;
}

QByteArray geometryJson(const Geofence &geofence) {
    return QJsonDocument(geometryObject(geofence)).toJson(QJsonDocument::Compact);
}

} // namespace

HistoryStore::HistoryStore(const QString &database_path)
    : database_path_(database_path),
      connection_name_(QStringLiteral("utms_history_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

HistoryStore::~HistoryStore() { close(); }

bool HistoryStore::initialize(QString *error_message) {
    if (initialized_) {
        return true;
    }

    const QFileInfo database_info(database_path_);
    QDir database_directory = database_info.dir();
    if (!database_directory.exists() && !database_directory.mkpath(QStringLiteral("."))) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "无法创建历史数据库目录：%1"))
                                    .arg(database_directory.absolutePath()));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name_);
    database.setDatabaseName(database_path_);
    if (!database.open()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "无法打开历史数据库 %1：%2"))
                                    .arg(database_path_, database.lastError().text()));
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_name_);
        return false;
    }

    if (!executeSchemaStatement(database, QStringLiteral("PRAGMA foreign_keys = ON"), error_message) ||
        !executeSchemaStatement(database, QStringLiteral("PRAGMA journal_mode = WAL"), error_message) ||
        !executeSchemaStatement(database, QStringLiteral("PRAGMA busy_timeout = 250"), error_message) ||
        !executeSchemaStatement(database,
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
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE TABLE IF NOT EXISTS history_frames ("
                                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                               "session_id INTEGER NOT NULL,"
                                               "frame_time_ms INTEGER NOT NULL,"
                                               "received_at_ms INTEGER NOT NULL,"
                                               "sender_timestamp_seconds REAL,"
                                               "sequence INTEGER,"
                                               "ego_latitude REAL,"
                                               "ego_longitude REAL,"
                                               "FOREIGN KEY(session_id) REFERENCES history_sessions(id) "
                                               "ON DELETE CASCADE"
                                               ")"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE TABLE IF NOT EXISTS history_targets ("
                                               "frame_id INTEGER NOT NULL,"
                                               "track_id INTEGER NOT NULL,"
                                               "target_type INTEGER NOT NULL CHECK(target_type BETWEEN 0 AND 4),"
                                               "latitude REAL NOT NULL,"
                                               "longitude REAL NOT NULL,"
                                               "velocity_mps REAL,"
                                               "distance_m REAL,"
                                               "first_seen_at_ms INTEGER,"
                                               "PRIMARY KEY(frame_id, track_id),"
                                               "FOREIGN KEY(frame_id) REFERENCES history_frames(id) "
                                               "ON DELETE CASCADE"
                                               ")"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_frames_session "
                                               "ON history_frames(session_id)"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_frames_time "
                                               "ON history_frames(frame_time_ms)"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_targets_track "
                                               "ON history_targets(track_id)"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_targets_type "
                                               "ON history_targets(target_type)"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE TABLE IF NOT EXISTS geofences ("
                                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                               "name TEXT NOT NULL,"
                                               "shape TEXT NOT NULL CHECK(shape IN "
                                               "('circle', 'rectangle', 'polygon')),"
                                               "geometry_json TEXT NOT NULL,"
                                               "enabled INTEGER NOT NULL CHECK(enabled IN (0, 1)),"
                                               "visible INTEGER NOT NULL CHECK(visible IN (0, 1)),"
                                               "created_at_ms INTEGER NOT NULL,"
                                               "updated_at_ms INTEGER NOT NULL"
                                               ")"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE TABLE IF NOT EXISTS alert_rules ("
                                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                               "name TEXT NOT NULL,"
                                               "rule_type INTEGER NOT NULL CHECK(rule_type BETWEEN 0 AND 1),"
                                               "geofence_id INTEGER NOT NULL,"
                                               "target_type_mask INTEGER NOT NULL CHECK(target_type_mask "
                                               "BETWEEN 1 AND 31),"
                                               "severity INTEGER NOT NULL CHECK(severity BETWEEN 0 AND 2),"
                                               "confirmation_ms INTEGER NOT NULL CHECK(confirmation_ms BETWEEN "
                                               "0 AND 60000),"
                                               "enabled INTEGER NOT NULL CHECK(enabled IN (0, 1)),"
                                               "note TEXT NOT NULL DEFAULT '',"
                                               "created_at_ms INTEGER NOT NULL,"
                                               "updated_at_ms INTEGER NOT NULL,"
                                               "FOREIGN KEY(geofence_id) REFERENCES geofences(id) ON DELETE "
                                               "CASCADE"
                                               ")"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alert_rules_geofence "
                                               "ON alert_rules(geofence_id)"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE TABLE IF NOT EXISTS target_alerts ("
                                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                               "occurred_at_ms INTEGER NOT NULL,"
                                               "rule_id INTEGER NOT NULL,"
                                               "rule_name TEXT NOT NULL,"
                                               "rule_type INTEGER NOT NULL CHECK(rule_type BETWEEN 0 AND 1),"
                                               "severity INTEGER NOT NULL CHECK(severity BETWEEN 0 AND 2),"
                                               "geofence_id INTEGER NOT NULL,"
                                               "geofence_name TEXT NOT NULL,"
                                               "track_id INTEGER NOT NULL,"
                                               "target_type INTEGER NOT NULL CHECK(target_type BETWEEN 0 AND 4),"
                                               "latitude REAL NOT NULL,"
                                               "longitude REAL NOT NULL,"
                                               "velocity_mps REAL,"
                                               "distance_m REAL,"
                                               "description TEXT NOT NULL,"
                                               "acknowledged INTEGER NOT NULL DEFAULT 0 CHECK(acknowledged IN "
                                               "(0, 1)),"
                                               "acknowledged_at_ms INTEGER,"
                                               "acknowledged_by TEXT,"
                                               "handling_note TEXT NOT NULL DEFAULT ''"
                                               ")"),
                                error_message) ||
        !executeSchemaStatement(database,
                                QStringLiteral("CREATE INDEX IF NOT EXISTS idx_target_alerts_time "
                                               "ON target_alerts(occurred_at_ms)"),
                                error_message)) {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_name_);
        return false;
    }

    initialized_ = true;
    return true;
}

std::optional<HistoryConfiguration> HistoryStore::loadConfiguration(QString *error_message) const {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT key, value FROM app_config"))) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "读取历史配置失败：%1")).arg(query.lastError().text()));
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
                setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史采样频率配置无效")));
                return std::nullopt;
            }
            configuration.sampling_rate = sampling_rate.value();
        } else if (key == QStringLiteral("history_retention_days")) {
            bool converted = false;
            const int retention_days = query.value(1).toString().toInt(&converted);
            if (!converted || retention_days < 1 || retention_days > 30) {
                setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史保留天数配置无效")));
                return std::nullopt;
            }
            configuration.retention_days = retention_days;
        }
    }
    return configuration;
}

bool HistoryStore::saveConfiguration(const HistoryConfiguration &configuration, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }

    const std::optional<int> sampling_rate = samplingRateValue(configuration.sampling_rate);
    if (!sampling_rate.has_value()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史采样频率配置无效")));
        return false;
    }
    if (configuration.retention_days < 1 || configuration.retention_days > 30) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史保留天数必须为 1–30 天")));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    if (!database.transaction()) {
        setError(
            error_message,
            storeText(QT_TRANSLATE_NOOP("HistoryStore", "启动历史配置事务失败：%1")).arg(database.lastError().text()));
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO app_config(key, value) VALUES(?, ?)"));
    query.addBindValue(QStringLiteral("history_sampling_rate"));
    query.addBindValue(QString::number(sampling_rate.value()));
    if (!query.exec()) {
        const QString operation_error =
            storeText(QT_TRANSLATE_NOOP("HistoryStore", "保存历史采样频率失败：%1")).arg(query.lastError().text());
        setTransactionError(database, error_message, operation_error);
        return false;
    }

    query.finish();
    query.bindValue(0, QStringLiteral("history_retention_days"));
    query.bindValue(1, QString::number(configuration.retention_days));
    if (!query.exec()) {
        const QString operation_error =
            storeText(QT_TRANSLATE_NOOP("HistoryStore", "保存历史保留天数失败：%1")).arg(query.lastError().text());
        setTransactionError(database, error_message, operation_error);
        return false;
    }

    if (!database.commit()) {
        const QString operation_error =
            storeText(QT_TRANSLATE_NOOP("HistoryStore", "提交历史配置失败：%1")).arg(database.lastError().text());
        setTransactionError(database, error_message, operation_error);
        return false;
    }
    return true;
}

std::optional<qint64> HistoryStore::startSession(const QDateTime &started_at, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }
    if (!started_at.isValid()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史会话开始时间无效")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery active_query(database);
    if (!active_query.exec(QStringLiteral("SELECT id FROM history_sessions "
                                          "WHERE state = 'active' LIMIT 1"))) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "检查活动历史会话失败：%1"))
                                    .arg(active_query.lastError().text()));
        return std::nullopt;
    }
    if (active_query.next()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "已经存在活动历史会话")));
        return std::nullopt;
    }

    QSqlQuery insert_query(database);
    insert_query.prepare(QStringLiteral("INSERT INTO history_sessions(started_at_ms, state) "
                                        "VALUES(?, 'active')"));
    insert_query.addBindValue(started_at.toMSecsSinceEpoch());
    if (!insert_query.exec()) {
        setError(
            error_message,
            storeText(QT_TRANSLATE_NOOP("HistoryStore", "创建历史会话失败：%1")).arg(insert_query.lastError().text()));
        return std::nullopt;
    }

    bool converted = false;
    const qint64 session_id = insert_query.lastInsertId().toLongLong(&converted);
    if (!converted || session_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史会话已创建但无法读取会话 ID")));
        return std::nullopt;
    }
    return session_id;
}

bool HistoryStore::closeActiveSession(const QDateTime &ended_at, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (!ended_at.isValid()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史会话结束时间无效")));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE history_sessions "
                                 "SET ended_at_ms = ?, state = 'closed' "
                                 "WHERE state = 'active'"));
    query.addBindValue(ended_at.toMSecsSinceEpoch());
    if (!query.exec()) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "关闭历史会话失败：%1")).arg(query.lastError().text()));
        return false;
    }
    return true;
}

std::optional<int> HistoryStore::recoverAbandonedSessions(const QDateTime &recovered_at, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }
    if (!recovered_at.isValid()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史会话恢复时间无效")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE history_sessions "
                                 "SET ended_at_ms = ?, state = 'abnormal' "
                                 "WHERE state = 'active'"));
    query.addBindValue(recovered_at.toMSecsSinceEpoch());
    if (!query.exec()) {
        setError(
            error_message,
            storeText(QT_TRANSLATE_NOOP("HistoryStore", "修复异常历史会话失败：%1")).arg(query.lastError().text()));
        return std::nullopt;
    }

    const qint64 recovered_count = query.numRowsAffected();
    if (recovered_count < 0 || recovered_count > std::numeric_limits<int>::max()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "无法确定异常历史会话恢复数量")));
        return std::nullopt;
    }
    return static_cast<int>(recovered_count);
}

std::optional<QVector<HistorySession>> HistoryStore::loadSessions(QString *error_message) const {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT id, started_at_ms, ended_at_ms, state "
                                   "FROM history_sessions ORDER BY id"))) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "读取历史会话失败：%1")).arg(query.lastError().text()));
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
            setError(error_message,
                     storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史会话 %1 的状态无效")).arg(session.id));
            return std::nullopt;
        }
        sessions.append(session);
    }
    return sessions;
}

bool HistoryStore::appendFrame(qint64 session_id, const RadarFrame &frame, QString *error_message) {
    return appendFrames(session_id, {frame}, error_message);
}

bool HistoryStore::appendFrames(qint64 session_id, const QVector<RadarFrame> &frames, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (session_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史帧的会话无效")));
        return false;
    }
    for (const RadarFrame &frame : frames) {
        if (!frame.received_at.isValid()) {
            setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史帧的接收时间无效")));
            return false;
        }
    }
    if (frames.isEmpty()) {
        return true;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    if (!database.transaction()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "启动历史帧写入事务失败：%1"))
                                    .arg(database.lastError().text()));
        return false;
    }

    QSqlQuery frame_query(database);
    frame_query.prepare(QStringLiteral("INSERT INTO history_frames("
                                       "session_id, frame_time_ms, received_at_ms, sender_timestamp_seconds, "
                                       "sequence, ego_latitude, ego_longitude) VALUES(?, ?, ?, ?, ?, ?, ?)"));
    QSqlQuery target_query(database);
    target_query.prepare(QStringLiteral("INSERT INTO history_targets("
                                        "frame_id, track_id, target_type, latitude, longitude, velocity_mps, "
                                        "distance_m, first_seen_at_ms) VALUES(?, ?, ?, ?, ?, ?, ?, ?)"));

    for (const RadarFrame &frame : frames) {
        frame_query.bindValue(0, session_id);
        frame_query.bindValue(1, frameTimeMs(frame));
        frame_query.bindValue(2, frame.received_at.toMSecsSinceEpoch());
        frame_query.bindValue(3, optionalDoubleValue(frame.sender_timestamp_seconds));
        frame_query.bindValue(4, optionalIntegerValue(frame.sequence));
        frame_query.bindValue(5, frame.ego_position.has_value() ? QVariant(frame.ego_position->latitude) : QVariant());
        frame_query.bindValue(6, frame.ego_position.has_value() ? QVariant(frame.ego_position->longitude) : QVariant());
        if (!frame_query.exec()) {
            const QString operation_error =
                storeText(QT_TRANSLATE_NOOP("HistoryStore", "写入历史帧失败：%1")).arg(frame_query.lastError().text());
            setTransactionError(database, error_message, operation_error);
            return false;
        }
        bool converted = false;
        const qint64 frame_id = frame_query.lastInsertId().toLongLong(&converted);
        frame_query.finish();
        if (!converted || frame_id <= 0) {
            setTransactionError(database, error_message,
                                storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史帧已写入但无法读取帧 ID")));
            return false;
        }

        for (const TrackData &track : frame.tracks) {
            target_query.bindValue(0, frame_id);
            target_query.bindValue(1, track.track_id);
            target_query.bindValue(2, static_cast<int>(track.type));
            target_query.bindValue(3, track.position.latitude);
            target_query.bindValue(4, track.position.longitude);
            target_query.bindValue(5, optionalDoubleValue(track.velocity_mps));
            target_query.bindValue(6, optionalDoubleValue(track.distance_m));
            target_query.bindValue(7, track.first_seen_at.isValid() ? QVariant(track.first_seen_at.toMSecsSinceEpoch())
                                                                    : QVariant());
            if (!target_query.exec()) {
                const QString operation_error =
                    storeText(QT_TRANSLATE_NOOP("HistoryStore", "写入历史帧目标 %1 失败：%2"))
                        .arg(track.track_id)
                        .arg(target_query.lastError().text());
                setTransactionError(database, error_message, operation_error);
                return false;
            }
            target_query.finish();
        }
    }

    if (!database.commit()) {
        const QString operation_error =
            storeText(QT_TRANSLATE_NOOP("HistoryStore", "提交历史帧事务失败：%1")).arg(database.lastError().text());
        setTransactionError(database, error_message, operation_error);
        return false;
    }
    return true;
}

std::optional<HistoryQueryResult> HistoryStore::queryHistory(const HistoryQuery &history_query,
                                                             QString *error_message) const {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }
    if (history_query.start_time.has_value() && !history_query.start_time->isValid()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史查询开始时间无效")));
        return std::nullopt;
    }
    if (history_query.end_time.has_value() && !history_query.end_time->isValid()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史查询结束时间无效")));
        return std::nullopt;
    }
    if (history_query.start_time.has_value() && history_query.end_time.has_value() &&
        history_query.start_time.value() > history_query.end_time.value()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史查询开始时间晚于结束时间")));
        return std::nullopt;
    }

    // LEFT JOIN 保留零目标快照；末尾排序让同一帧的目标行连续，便于重建完整
    // HistoryFrameRecord。
    QString sql = QStringLiteral("SELECT f.id, f.session_id, f.frame_time_ms, f.received_at_ms, "
                                 "f.sender_timestamp_seconds, "
                                 "f.sequence, f.ego_latitude, f.ego_longitude, t.track_id, t.target_type, "
                                 "t.latitude, "
                                 "t.longitude, t.velocity_mps, t.distance_m, t.first_seen_at_ms "
                                 "FROM history_frames f LEFT JOIN history_targets t ON t.frame_id = f.id "
                                 "WHERE "
                                 "1 = 1");
    QVariantList bindings;
    if (history_query.start_time.has_value()) {
        sql += QStringLiteral(" AND f.frame_time_ms >= ?");
        bindings.append(history_query.start_time->toMSecsSinceEpoch());
    }
    if (history_query.end_time.has_value()) {
        sql += QStringLiteral(" AND f.frame_time_ms <= ?");
        bindings.append(history_query.end_time->toMSecsSinceEpoch());
    }
    if (history_query.session_id.has_value()) {
        sql += QStringLiteral(" AND f.session_id = ?");
        bindings.append(history_query.session_id.value());
    }
    if (history_query.track_id.has_value()) {
        sql += QStringLiteral(" AND t.track_id = ?");
        bindings.append(history_query.track_id.value());
    }
    if (history_query.target_type.has_value()) {
        sql += QStringLiteral(" AND t.target_type = ?");
        bindings.append(static_cast<int>(history_query.target_type.value()));
    }
    sql += QStringLiteral(" ORDER BY f.frame_time_ms, f.id, t.track_id");

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(sql);
    for (const QVariant &binding : bindings) {
        query.addBindValue(binding);
    }
    if (!query.exec()) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "查询历史记录失败：%1")).arg(query.lastError().text()));
        return std::nullopt;
    }

    HistoryQueryResult result;
    result.query = history_query;
    while (query.next()) {
        const qint64 frame_id = query.value(0).toLongLong();
        if (result.frames.isEmpty() || result.frames.constLast().frame_id != frame_id) {
            HistoryFrameRecord frame;
            frame.frame_id = frame_id;
            frame.session_id = query.value(1).toLongLong();
            frame.frame_time = QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong(), QTimeZone::UTC);
            frame.received_at = QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong(), QTimeZone::UTC);
            if (!query.value(4).isNull()) {
                frame.sender_timestamp_seconds = query.value(4).toDouble();
            }
            if (!query.value(5).isNull()) {
                frame.sequence = query.value(5).toLongLong();
            }
            if (!query.value(6).isNull() && !query.value(7).isNull()) {
                frame.ego_position = GeoPosition{query.value(6).toDouble(), query.value(7).toDouble()};
            }
            result.frames.append(frame);
        }
        if (query.value(8).isNull()) {
            continue;
        }

        TrackData track;
        track.track_id = query.value(8).toLongLong();
        const int target_type_value = query.value(9).toInt();
        if (!isValidTargetTypeValue(target_type_value)) {
            setError(error_message,
                     storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史目标 %1 的类别无效")).arg(track.track_id));
            return std::nullopt;
        }
        track.type = static_cast<TargetType>(target_type_value);
        track.position = {query.value(10).toDouble(), query.value(11).toDouble()};
        if (!query.value(12).isNull()) {
            track.velocity_mps = query.value(12).toDouble();
        }
        if (!query.value(13).isNull()) {
            track.distance_m = query.value(13).toDouble();
        }
        if (!query.value(14).isNull()) {
            track.first_seen_at = QDateTime::fromMSecsSinceEpoch(query.value(14).toLongLong(), QTimeZone::UTC);
        }
        result.frames.last().tracks.append(track);
    }
    return result;
}

std::optional<int> HistoryStore::exportCsv(const HistoryQuery &history_query, std::optional<qint64> selected_track_id,
                                           const QString &output_path, QString *error_message) const {
    HistoryQuery export_query = history_query;
    if (selected_track_id.has_value()) {
        export_query.track_id = selected_track_id;
    }
    const std::optional<HistoryQueryResult> result = queryHistory(export_query, error_message);
    if (!result.has_value()) {
        return std::nullopt;
    }
    return HistoryCsvExporter::exportToFile(result.value(), output_path, error_message);
}

std::optional<int> HistoryStore::cleanupExpiredHistory(const QDateTime &cutoff, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }
    if (!cutoff.isValid()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史清理截止时间无效")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("DELETE FROM history_sessions "
                                 "WHERE state <> 'active' AND COALESCE(ended_at_ms, started_at_ms) < ?"));
    query.addBindValue(cutoff.toMSecsSinceEpoch());
    if (!query.exec()) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "清理过期历史失败：%1")).arg(query.lastError().text()));
        return std::nullopt;
    }
    const qint64 deleted_count = query.numRowsAffected();
    if (deleted_count < 0 || deleted_count > std::numeric_limits<int>::max()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "无法确定过期历史清理数量")));
        return std::nullopt;
    }
    return static_cast<int>(deleted_count);
}

bool HistoryStore::deleteSession(qint64 session_id, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (session_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待删除的历史会话 ID 无效")));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("DELETE FROM history_sessions WHERE id = ? AND state <> 'active'"));
    query.addBindValue(session_id);
    if (!query.exec()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "删除历史会话 %1 失败：%2"))
                                    .arg(session_id)
                                    .arg(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史会话 %1 不存在或仍在记录中")).arg(session_id));
        return false;
    }
    return true;
}

std::optional<int> HistoryStore::deleteAllSessions(QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    if (!database.transaction()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "开始删除全部历史会话失败：%1"))
                                    .arg(database.lastError().text()));
        return std::nullopt;
    }

    QSqlQuery active_session_query(database);
    if (!active_session_query.exec(QStringLiteral("SELECT COUNT(*) FROM history_sessions WHERE state = 'active'")) ||
        !active_session_query.next()) {
        const QString operation_error = storeText(QT_TRANSLATE_NOOP("HistoryStore", "检查活动历史会话失败：%1"))
                                            .arg(active_session_query.lastError().text());
        setTransactionError(database, error_message, operation_error);
        return std::nullopt;
    }
    if (active_session_query.value(0).toLongLong() > 0) {
        setTransactionError(
            database, error_message,
            storeText(QT_TRANSLATE_NOOP("HistoryStore", "仍有会话正在记录，请先停止 UDP 监听再删除全部会话")));
        return std::nullopt;
    }

    QSqlQuery delete_query(database);
    if (!delete_query.exec(QStringLiteral("DELETE FROM history_sessions"))) {
        const QString operation_error = storeText(QT_TRANSLATE_NOOP("HistoryStore", "删除全部历史会话失败：%1"))
                                            .arg(delete_query.lastError().text());
        setTransactionError(database, error_message, operation_error);
        return std::nullopt;
    }
    const qint64 deleted_count = delete_query.numRowsAffected();
    if (deleted_count < 0 || deleted_count > std::numeric_limits<int>::max()) {
        setTransactionError(database, error_message,
                            storeText(QT_TRANSLATE_NOOP("HistoryStore", "无法确定已删除的历史会话数量")));
        return std::nullopt;
    }
    if (!database.commit()) {
        const QString operation_error = storeText(QT_TRANSLATE_NOOP("HistoryStore", "提交全部历史会话删除失败：%1"))
                                            .arg(database.lastError().text());
        setTransactionError(database, error_message, operation_error);
        return std::nullopt;
    }
    return static_cast<int>(deleted_count);
}

std::optional<QVector<Geofence>> HistoryStore::loadGeofences(QString *error_message) const {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT id, name, shape, geometry_json, enabled, visible "
                                   "FROM geofences ORDER BY id"))) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "读取电子围栏失败：%1")).arg(query.lastError().text()));
        return std::nullopt;
    }

    QVector<Geofence> geofences;
    while (query.next()) {
        const std::optional<GeofenceShape> shape = shapeFromStorageName(query.value(2).toString());
        QJsonParseError parse_error;
        const QJsonDocument geometry_document = QJsonDocument::fromJson(query.value(3).toByteArray(), &parse_error);
        if (!shape.has_value() || parse_error.error != QJsonParseError::NoError || !geometry_document.isObject()) {
            setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "电子围栏 %1 的几何数据损坏"))
                                        .arg(query.value(0).toLongLong()));
            return std::nullopt;
        }
        const std::optional<GeofenceGeometry> geometry = geometryFromObject(shape.value(), geometry_document.object());
        if (!geometry.has_value()) {
            setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "电子围栏 %1 的几何数据无效"))
                                        .arg(query.value(0).toLongLong()));
            return std::nullopt;
        }

        Geofence geofence;
        geofence.id = query.value(0).toLongLong();
        geofence.name = query.value(1).toString();
        geofence.geometry = geometry.value();
        geofence.enabled = query.value(4).toBool();
        geofence.visible = query.value(5).toBool();
        const QString validation_error = validateGeofence(geofence);
        if (!validation_error.isEmpty()) {
            setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "电子围栏 %1 无效：%2"))
                                        .arg(geofence.id)
                                        .arg(validation_error));
            return std::nullopt;
        }
        geofences.append(geofence);
    }
    return geofences;
}

std::optional<qint64> HistoryStore::createGeofence(const Geofence &geofence, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }
    const QString validation_error = validateGeofence(geofence);
    if (!validation_error.isEmpty()) {
        setError(error_message, validation_error);
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT INTO geofences("
                                 "name, shape, geometry_json, enabled, visible, "
                                 "created_at_ms, updated_at_ms) "
                                 "VALUES(?, ?, ?, ?, ?, ?, ?)"));
    const qint64 now_ms = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    query.addBindValue(geofence.name.trimmed());
    query.addBindValue(shapeStorageName(geofenceShape(geofence)));
    query.addBindValue(QString::fromUtf8(geometryJson(geofence)));
    query.addBindValue(geofence.enabled);
    query.addBindValue(geofence.visible);
    query.addBindValue(now_ms);
    query.addBindValue(now_ms);
    if (!query.exec()) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "创建电子围栏失败：%1")).arg(query.lastError().text()));
        return std::nullopt;
    }

    bool converted = false;
    const qint64 geofence_id = query.lastInsertId().toLongLong(&converted);
    if (!converted || geofence_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "电子围栏已创建但无法读取 ID")));
        return std::nullopt;
    }
    return geofence_id;
}

bool HistoryStore::updateGeofence(const Geofence &geofence, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (geofence.id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待更新的电子围栏 ID 无效")));
        return false;
    }
    const QString validation_error = validateGeofence(geofence);
    if (!validation_error.isEmpty()) {
        setError(error_message, validation_error);
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE geofences SET name = ?, shape = ?, "
                                 "geometry_json = ?, enabled = ?, "
                                 "visible = ?, updated_at_ms = ? WHERE id = ?"));
    query.addBindValue(geofence.name.trimmed());
    query.addBindValue(shapeStorageName(geofenceShape(geofence)));
    query.addBindValue(QString::fromUtf8(geometryJson(geofence)));
    query.addBindValue(geofence.enabled);
    query.addBindValue(geofence.visible);
    query.addBindValue(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    query.addBindValue(geofence.id);
    if (!query.exec()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "更新电子围栏 %1 失败：%2"))
                                    .arg(geofence.id)
                                    .arg(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "电子围栏 %1 不存在")).arg(geofence.id));
        return false;
    }
    return true;
}

bool HistoryStore::updateGeofenceGeometry(const Geofence &geofence, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (geofence.id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待更新的电子围栏 ID 无效")));
        return false;
    }
    const QString validation_error = validateGeofence(geofence);
    if (!validation_error.isEmpty()) {
        setError(error_message, validation_error);
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE geofences SET shape = ?, geometry_json = ?, "
                                 "updated_at_ms = ? WHERE id = ?"));
    query.addBindValue(shapeStorageName(geofenceShape(geofence)));
    query.addBindValue(QString::fromUtf8(geometryJson(geofence)));
    query.addBindValue(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    query.addBindValue(geofence.id);
    if (!query.exec()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "更新电子围栏 %1 几何失败：%2"))
                                    .arg(geofence.id)
                                    .arg(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "电子围栏 %1 不存在")).arg(geofence.id));
        return false;
    }
    return true;
}

bool HistoryStore::setGeofenceEnabled(qint64 geofence_id, bool enabled, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (geofence_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待更新的电子围栏 ID 无效")));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE geofences SET enabled = ?, updated_at_ms = ? WHERE id = ?"));
    query.addBindValue(enabled);
    query.addBindValue(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    query.addBindValue(geofence_id);
    if (!query.exec()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "更新电子围栏 %1 启用状态失败：%2"))
                                    .arg(geofence_id)
                                    .arg(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "电子围栏 %1 不存在")).arg(geofence_id));
        return false;
    }
    return true;
}

bool HistoryStore::setGeofenceVisible(qint64 geofence_id, bool visible, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (geofence_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待更新的电子围栏 ID 无效")));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE geofences SET visible = ?, updated_at_ms = ? WHERE id = ?"));
    query.addBindValue(visible);
    query.addBindValue(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    query.addBindValue(geofence_id);
    if (!query.exec()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "更新电子围栏 %1 显示状态失败：%2"))
                                    .arg(geofence_id)
                                    .arg(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "电子围栏 %1 不存在")).arg(geofence_id));
        return false;
    }
    return true;
}

bool HistoryStore::deleteGeofence(qint64 geofence_id, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (geofence_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待删除的电子围栏 ID 无效")));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("DELETE FROM geofences WHERE id = ?"));
    query.addBindValue(geofence_id);
    if (!query.exec()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "删除电子围栏 %1 失败：%2"))
                                    .arg(geofence_id)
                                    .arg(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "电子围栏 %1 不存在")).arg(geofence_id));
        return false;
    }
    return true;
}

std::optional<QVector<AlertRule>> HistoryStore::loadAlertRules(QString *error_message) const {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT id, name, rule_type, geofence_id, target_type_mask, "
                                   "severity, "
                                   "confirmation_ms, enabled, note FROM alert_rules ORDER BY id"))) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "读取告警规则失败：%1")).arg(query.lastError().text()));
        return std::nullopt;
    }

    QVector<AlertRule> rules;
    while (query.next()) {
        const int rule_type_value = query.value(2).toInt();
        const int severity_value = query.value(5).toInt();
        const std::optional<QVector<TargetType>> target_types = targetTypesFromMask(query.value(4).toInt());
        if (!isValidAlertRuleTypeValue(rule_type_value) || !isValidAlertSeverityValue(severity_value) ||
            !target_types.has_value()) {
            setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "告警规则 %1 的持久化数据无效"))
                                        .arg(query.value(0).toLongLong()));
            return std::nullopt;
        }

        AlertRule rule;
        rule.id = query.value(0).toLongLong();
        rule.name = query.value(1).toString();
        rule.type = static_cast<AlertRuleType>(rule_type_value);
        rule.geofence_id = query.value(3).toLongLong();
        rule.target_types = target_types.value();
        rule.severity = static_cast<AlertSeverity>(severity_value);
        rule.confirmation_ms = query.value(6).toInt();
        rule.enabled = query.value(7).toBool();
        rule.note = query.value(8).toString();
        const QString validation_error = validateAlertRule(rule);
        if (!validation_error.isEmpty()) {
            setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "告警规则 %1 无效：%2"))
                                        .arg(rule.id)
                                        .arg(validation_error));
            return std::nullopt;
        }
        rules.append(rule);
    }
    return rules;
}

std::optional<qint64> HistoryStore::createAlertRule(const AlertRule &rule, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }
    const QString validation_error = validateAlertRule(rule);
    if (!validation_error.isEmpty()) {
        setError(error_message, validation_error);
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT INTO alert_rules("
                                 "name, rule_type, geofence_id, target_type_mask, "
                                 "severity, confirmation_ms, enabled, "
                                 "note, created_at_ms, updated_at_ms) VALUES(?, ?, ?, ?, "
                                 "?, ?, ?, ?, ?, ?)"));
    const qint64 now_ms = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    query.addBindValue(rule.name.trimmed());
    query.addBindValue(static_cast<int>(rule.type));
    query.addBindValue(rule.geofence_id);
    query.addBindValue(targetTypeMask(rule.target_types));
    query.addBindValue(static_cast<int>(rule.severity));
    query.addBindValue(rule.confirmation_ms);
    query.addBindValue(rule.enabled);
    query.addBindValue(rule.note.isNull() ? QStringLiteral("") : rule.note.trimmed());
    query.addBindValue(now_ms);
    query.addBindValue(now_ms);
    if (!query.exec()) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "创建告警规则失败：%1")).arg(query.lastError().text()));
        return std::nullopt;
    }
    const qint64 rule_id = query.lastInsertId().toLongLong();
    if (rule_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "告警规则已创建但无法读取 ID")));
        return std::nullopt;
    }
    return rule_id;
}

bool HistoryStore::updateAlertRule(const AlertRule &rule, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (rule.id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待更新的告警规则 ID 无效")));
        return false;
    }
    const QString validation_error = validateAlertRule(rule);
    if (!validation_error.isEmpty()) {
        setError(error_message, validation_error);
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE alert_rules SET name = ?, rule_type = ?, geofence_id = ?, "
                                 "target_type_mask = ?, severity = ?, confirmation_ms = ?, enabled = ?, "
                                 "note = ?, "
                                 "updated_at_ms = ? WHERE id = ?"));
    query.addBindValue(rule.name.trimmed());
    query.addBindValue(static_cast<int>(rule.type));
    query.addBindValue(rule.geofence_id);
    query.addBindValue(targetTypeMask(rule.target_types));
    query.addBindValue(static_cast<int>(rule.severity));
    query.addBindValue(rule.confirmation_ms);
    query.addBindValue(rule.enabled);
    query.addBindValue(rule.note.isNull() ? QStringLiteral("") : rule.note.trimmed());
    query.addBindValue(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    query.addBindValue(rule.id);
    if (!query.exec()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "更新告警规则 %1 失败：%2"))
                                    .arg(rule.id)
                                    .arg(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "告警规则 %1 不存在")).arg(rule.id));
        return false;
    }
    return true;
}

bool HistoryStore::setAlertRuleEnabled(qint64 rule_id, bool enabled, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (rule_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待更新的告警规则 ID 无效")));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("UPDATE alert_rules SET enabled = ?, updated_at_ms = ? WHERE id = ?"));
    query.addBindValue(enabled);
    query.addBindValue(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
    query.addBindValue(rule_id);
    if (!query.exec()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "更新告警规则 %1 启用状态失败：%2"))
                                    .arg(rule_id)
                                    .arg(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "告警规则 %1 不存在")).arg(rule_id));
        return false;
    }
    return true;
}

bool HistoryStore::deleteAlertRule(qint64 rule_id, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }
    if (rule_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待删除的告警规则 ID 无效")));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("DELETE FROM alert_rules WHERE id = ?"));
    query.addBindValue(rule_id);
    if (!query.exec()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "删除告警规则 %1 失败：%2"))
                                    .arg(rule_id)
                                    .arg(query.lastError().text()));
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "告警规则 %1 不存在")).arg(rule_id));
        return false;
    }
    return true;
}

std::optional<qint64> HistoryStore::appendTargetAlert(const TargetAlert &alert, QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }
    if (!alert.occurred_at.isValid() || alert.rule_id <= 0 || alert.geofence_id <= 0 ||
        !isValidAlertRuleTypeValue(static_cast<int>(alert.rule_type)) ||
        !isValidAlertSeverityValue(static_cast<int>(alert.severity)) ||
        !isValidTargetTypeValue(static_cast<int>(alert.target_type)) || !std::isfinite(alert.position.latitude) ||
        !std::isfinite(alert.position.longitude)) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待保存的告警数据无效")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("INSERT INTO target_alerts("
                                 "occurred_at_ms, rule_id, rule_name, rule_type, severity, "
                                 "geofence_id, geofence_name, "
                                 "track_id, target_type, latitude, longitude, "
                                 "velocity_mps, distance_m, description) "
                                 "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(alert.occurred_at.toMSecsSinceEpoch());
    query.addBindValue(alert.rule_id);
    query.addBindValue(alert.rule_name);
    query.addBindValue(static_cast<int>(alert.rule_type));
    query.addBindValue(static_cast<int>(alert.severity));
    query.addBindValue(alert.geofence_id);
    query.addBindValue(alert.geofence_name);
    query.addBindValue(alert.track_id);
    query.addBindValue(static_cast<int>(alert.target_type));
    query.addBindValue(alert.position.latitude);
    query.addBindValue(alert.position.longitude);
    query.addBindValue(optionalDoubleValue(alert.velocity_mps));
    query.addBindValue(optionalDoubleValue(alert.distance_m));
    query.addBindValue(alert.description);
    if (!query.exec()) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "保存目标告警失败：%1")).arg(query.lastError().text()));
        return std::nullopt;
    }
    const qint64 alert_id = query.lastInsertId().toLongLong();
    if (alert_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "目标告警已保存但无法读取 ID")));
        return std::nullopt;
    }
    return alert_id;
}

std::optional<TargetAlert> HistoryStore::loadTargetAlert(qint64 alert_id, QString *error_message) const {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return std::nullopt;
    }
    if (alert_id <= 0) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "待读取的告警 ID 无效")));
        return std::nullopt;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT occurred_at_ms, rule_id, rule_name, "
                                 "rule_type, severity, geofence_id, "
                                 "geofence_name, track_id, target_type, "
                                 "latitude, longitude, velocity_mps, distance_m, "
                                 "description FROM target_alerts WHERE id = ?"));
    query.addBindValue(alert_id);
    if (!query.exec()) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "读取目标告警失败：%1")).arg(query.lastError().text()));
        return std::nullopt;
    }
    if (!query.next()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "目标告警 %1 不存在")).arg(alert_id));
        return std::nullopt;
    }
    const int rule_type_value = query.value(3).toInt();
    const int severity_value = query.value(4).toInt();
    const int target_type_value = query.value(8).toInt();
    if (!isValidAlertRuleTypeValue(rule_type_value) || !isValidAlertSeverityValue(severity_value) ||
        !isValidTargetTypeValue(target_type_value)) {
        setError(error_message,
                 storeText(QT_TRANSLATE_NOOP("HistoryStore", "目标告警 %1 的持久化数据无效")).arg(alert_id));
        return std::nullopt;
    }

    TargetAlert alert;
    alert.id = alert_id;
    alert.occurred_at = QDateTime::fromMSecsSinceEpoch(query.value(0).toLongLong(), QTimeZone::UTC);
    alert.rule_id = query.value(1).toLongLong();
    alert.rule_name = query.value(2).toString();
    alert.rule_type = static_cast<AlertRuleType>(rule_type_value);
    alert.severity = static_cast<AlertSeverity>(severity_value);
    alert.geofence_id = query.value(5).toLongLong();
    alert.geofence_name = query.value(6).toString();
    alert.track_id = query.value(7).toLongLong();
    alert.target_type = static_cast<TargetType>(target_type_value);
    alert.position = {query.value(9).toDouble(), query.value(10).toDouble()};
    if (!query.value(11).isNull()) {
        alert.velocity_mps = query.value(11).toDouble();
    }
    if (!query.value(12).isNull()) {
        alert.distance_m = query.value(12).toDouble();
    }
    alert.description = query.value(13).toString();
    return alert;
}

bool HistoryStore::probeWriteAccess(QString *error_message) {
    if (!initialized_) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "历史数据库尚未初始化")));
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(connection_name_);
    if (!database.transaction()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "检查历史数据库写入状态失败：%1"))
                                    .arg(database.lastError().text()));
        return false;
    }
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("UPDATE app_config SET value = value "
                                   "WHERE key = 'history_sampling_rate'"))) {
        const QString operation_error = storeText(QT_TRANSLATE_NOOP("HistoryStore", "检查历史数据库写入状态失败：%1"))
                                            .arg(query.lastError().text());
        setTransactionError(database, error_message, operation_error);
        return false;
    }
    if (!database.rollback()) {
        setError(error_message, storeText(QT_TRANSLATE_NOOP("HistoryStore", "检查历史数据库写入状态时回滚失败：%1"))
                                    .arg(database.lastError().text()));
        return false;
    }
    return true;
}

qint64 HistoryStore::databaseSizeBytes() const {
    qint64 total_bytes = 0;
    const QStringList paths = {database_path_, database_path_ + QStringLiteral("-wal"),
                               database_path_ + QStringLiteral("-shm")};
    for (const QString &path : paths) {
        const QFileInfo file_info(path);
        if (file_info.exists() && file_info.isFile()) {
            total_bytes += file_info.size();
        }
    }
    return total_bytes;
}

void HistoryStore::close() {
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
