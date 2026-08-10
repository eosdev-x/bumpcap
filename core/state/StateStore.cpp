#include "core/state/StateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThread>
#include <QTimeZone>
#include <QVariant>
#include <QtGlobal>

#include <utility>

#include "core/log/Log.h"

void InitBumpcapMigrationResources() {
    Q_INIT_RESOURCE(migrations);
}

namespace kh::state {
namespace {

qint64 NowEpochSeconds() {
    return QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
}

QDateTime FromEpochSeconds(qint64 seconds) {
    return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC);
}

QString ConnectionNameFor(const QString &database_path) {
    return QStringLiteral("bumpcap-state-%1-%2")
        .arg(QString::number(reinterpret_cast<quintptr>(QThread::currentThread()), 16),
             QString::number(qHash(database_path), 16));
}

QSqlDatabase Database(const QString &connection_name) {
    return QSqlDatabase::database(connection_name);
}

QString MigrationResourcePath(int version) {
    return QStringLiteral(":/kh/state/migrations/%1_init.sql")
        .arg(version, 4, 10, QLatin1Char('0'));
}

}  // namespace

StateStore::StateStore(QObject *parent)
    : StateStore(DefaultDatabasePath(), parent) {}

StateStore::StateStore(QString database_path, QObject *parent)
    : QObject(parent),
      database_path_(std::move(database_path)),
      connection_name_(ConnectionNameFor(database_path_)) {}

StateStore::~StateStore() {
    close();
}

bool StateStore::open() {
    if (isOpen()) {
        return true;
    }
    if (!ensureDatabaseDirectory()) {
        return false;
    }

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name_);
    database.setDatabaseName(database_path_);
    if (!database.open()) {
        setLastError(QStringLiteral("Unable to open state database %1: %2")
                         .arg(database_path_, database.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    QFile::setPermissions(database_path_,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    InitBumpcapMigrationResources();
    if (!applyMigrations()) {
        close();
        return false;
    }
    return true;
}

void StateStore::close() {
    if (!QSqlDatabase::contains(connection_name_)) {
        return;
    }
    {
        QSqlDatabase database = QSqlDatabase::database(connection_name_);
        if (database.isOpen()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name_);
}

bool StateStore::isOpen() const {
    return QSqlDatabase::contains(connection_name_) &&
           QSqlDatabase::database(connection_name_).isOpen();
}

QString StateStore::databasePath() const {
    return database_path_;
}

QString StateStore::lastError() const {
    return last_error_;
}

bool StateStore::setPinned(const QString &version, kh::model::SourceId source_id) {
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO pins (version, source_id, pinned_at) VALUES (?, ?, ?)"));
    query.addBindValue(version);
    query.addBindValue(kh::model::SourceIdToString(source_id));
    query.addBindValue(NowEpochSeconds());
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to store pin for %1: %2")
                         .arg(version, query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

bool StateStore::removePin(const QString &version) {
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral("DELETE FROM pins WHERE version = ?"));
    query.addBindValue(version);
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to remove pin for %1: %2")
                         .arg(version, query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

bool StateStore::isPinned(const QString &version) const {
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral("SELECT 1 FROM pins WHERE version = ? LIMIT 1"));
    query.addBindValue(version);
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to query pin for %1: %2")
                         .arg(version, query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return query.next();
}

QList<PinRecord> StateStore::pins() const {
    QList<PinRecord> records;
    QSqlQuery query(Database(connection_name_));
    if (!query.exec(QStringLiteral(
            "SELECT version, source_id, pinned_at FROM pins ORDER BY pinned_at DESC"))) {
        setLastError(QStringLiteral("Unable to list pins: %1").arg(query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return records;
    }
    while (query.next()) {
        const std::optional<kh::model::SourceId> source_id =
            kh::model::SourceIdFromString(query.value(1).toString());
        if (!source_id.has_value()) {
            qCWarning(kh::log::state) << "Skipping pin with unknown source id"
                                      << query.value(1).toString();
            continue;
        }
        records.push_back({query.value(0).toString(),
                           *source_id,
                           FromEpochSeconds(query.value(2).toLongLong())});
    }
    return records;
}

bool StateStore::setNote(const QString &version, const QString &note) {
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO notes (version, note, updated_at) VALUES (?, ?, ?)"));
    query.addBindValue(version);
    query.addBindValue(note);
    query.addBindValue(NowEpochSeconds());
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to store note for %1: %2")
                         .arg(version, query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

QString StateStore::note(const QString &version) const {
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral("SELECT note FROM notes WHERE version = ?"));
    query.addBindValue(version);
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to query note for %1: %2")
                         .arg(version, query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return {};
    }
    return query.next() ? query.value(0).toString() : QString();
}

bool StateStore::markSeen(kh::model::SourceId source_id,
                          const QString &version,
                          bool notified) {
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO seen_versions "
        "(source_id, version, first_seen_at, notified) VALUES (?, ?, ?, ?)"));
    query.addBindValue(kh::model::SourceIdToString(source_id));
    query.addBindValue(version);
    query.addBindValue(NowEpochSeconds());
    query.addBindValue(notified ? 1 : 0);
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to mark %1 as seen: %2")
                         .arg(version, query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

QStringList StateStore::seenVersions(kh::model::SourceId source_id) const {
    QStringList versions;
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral(
        "SELECT version FROM seen_versions WHERE source_id = ? ORDER BY first_seen_at DESC"));
    query.addBindValue(kh::model::SourceIdToString(source_id));
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to list seen versions: %1")
                         .arg(query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return versions;
    }
    while (query.next()) {
        versions.push_back(query.value(0).toString());
    }
    return versions;
}

bool StateStore::setNotified(kh::model::SourceId source_id,
                             const QString &version,
                             bool notified) {
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral(
        "UPDATE seen_versions SET notified = ? WHERE source_id = ? AND version = ?"));
    query.addBindValue(notified ? 1 : 0);
    query.addBindValue(kh::model::SourceIdToString(source_id));
    query.addBindValue(version);
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to update notification state for %1: %2")
                         .arg(version, query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

bool StateStore::recordInstallAction(const QString &version,
                                     kh::model::SourceId source_id,
                                     const QString &action,
                                     bool success) {
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral(
        "INSERT INTO install_history "
        "(version, source_id, action, performed_at, success) VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(version);
    query.addBindValue(kh::model::SourceIdToString(source_id));
    query.addBindValue(action);
    query.addBindValue(NowEpochSeconds());
    query.addBindValue(success ? 1 : 0);
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to record install history for %1: %2")
                         .arg(version, query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

QList<InstallHistoryRecord> StateStore::installHistory(int limit) const {
    QList<InstallHistoryRecord> records;
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral(
        "SELECT id, version, source_id, action, performed_at, success "
        "FROM install_history ORDER BY performed_at DESC LIMIT ?"));
    query.addBindValue(qMax(1, limit));
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to list install history: %1")
                         .arg(query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return records;
    }
    while (query.next()) {
        const std::optional<kh::model::SourceId> source_id =
            kh::model::SourceIdFromString(query.value(2).toString());
        if (!source_id.has_value()) {
            qCWarning(kh::log::state) << "Skipping history row with unknown source id"
                                      << query.value(2).toString();
            continue;
        }
        records.push_back({query.value(0).toLongLong(),
                           query.value(1).toString(),
                           *source_id,
                           query.value(3).toString(),
                           FromEpochSeconds(query.value(4).toLongLong()),
                           query.value(5).toInt() != 0});
    }
    return records;
}

QString StateStore::DefaultDatabasePath() {
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(root).filePath(QStringLiteral("state.db"));
}

bool StateStore::ensureDatabaseDirectory() const {
    const QFileInfo file_info(database_path_);
    QDir directory = file_info.dir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("Unable to create state directory %1")
                         .arg(directory.absolutePath()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

bool StateStore::applyMigrations() {
    QSqlDatabase database = Database(connection_name_);
    if (!database.transaction()) {
        setLastError(QStringLiteral("Unable to start migration transaction: %1")
                         .arg(database.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }

    const int current_version = schemaVersion();
    if (current_version < 0) {
        database.rollback();
        return false;
    }

    for (int version = current_version + 1; version <= 1; ++version) {
        QFile migration_file(MigrationResourcePath(version));
        if (!migration_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            setLastError(QStringLiteral("Unable to open migration %1").arg(version));
            qCWarning(kh::log::state) << last_error_;
            database.rollback();
            return false;
        }
        if (!executeSqlScript(QString::fromUtf8(migration_file.readAll())) ||
            !setSchemaVersion(version)) {
            database.rollback();
            return false;
        }
    }

    if (!database.commit()) {
        setLastError(QStringLiteral("Unable to commit migrations: %1")
                         .arg(database.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

int StateStore::schemaVersion() const {
    QSqlQuery exists_query(Database(connection_name_));
    if (!exists_query.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_meta'"))) {
        setLastError(QStringLiteral("Unable to inspect schema metadata: %1")
                         .arg(exists_query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return -1;
    }
    if (!exists_query.next()) {
        return 0;
    }

    QSqlQuery version_query(Database(connection_name_));
    version_query.prepare(QStringLiteral("SELECT value FROM schema_meta WHERE key = ?"));
    version_query.addBindValue(QStringLiteral("schema_version"));
    if (!version_query.exec()) {
        setLastError(QStringLiteral("Unable to read schema version: %1")
                         .arg(version_query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return -1;
    }
    if (!version_query.next()) {
        return 0;
    }
    return version_query.value(0).toInt();
}

bool StateStore::setSchemaVersion(int version) {
    QSqlQuery query(Database(connection_name_));
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO schema_meta (key, value) VALUES ('schema_version', ?)"));
    query.addBindValue(QString::number(version));
    if (!query.exec()) {
        setLastError(QStringLiteral("Unable to store schema version %1: %2")
                         .arg(version)
                         .arg(query.lastError().text()));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

bool StateStore::executeSqlScript(const QString &script) {
    const QStringList statements = script.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &statement : statements) {
        const QString trimmed = statement.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (!executeStatement(trimmed)) {
            return false;
        }
    }
    return true;
}

bool StateStore::executeStatement(const QString &sql) const {
    QSqlQuery query(Database(connection_name_));
    if (!query.exec(sql)) {
        setLastError(QStringLiteral("Unable to execute SQL migration statement: %1; SQL: %2")
                         .arg(query.lastError().text(), sql));
        qCWarning(kh::log::state) << last_error_;
        return false;
    }
    return true;
}

void StateStore::setLastError(const QString &error) const {
    last_error_ = error;
}

}  // namespace kh::state
