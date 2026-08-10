#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include "core/model/SourceId.h"

namespace kh::state {

struct PinRecord {
    QString version;
    kh::model::SourceId sourceId = kh::model::SourceId::FedoraStable;
    QDateTime pinnedAt;
};

struct InstallHistoryRecord {
    qint64 id = 0;
    QString version;
    kh::model::SourceId sourceId = kh::model::SourceId::FedoraStable;
    QString action;
    QDateTime performedAt;
    bool success = false;
};

class StateStore : public QObject {
    Q_OBJECT

public:
    explicit StateStore(QObject *parent = nullptr);
    explicit StateStore(QString database_path, QObject *parent = nullptr);
    ~StateStore() override;

    bool open();
    void close();
    bool isOpen() const;

    QString databasePath() const;
    QString lastError() const;

    bool setPinned(const QString &version, kh::model::SourceId source_id);
    bool removePin(const QString &version);
    bool isPinned(const QString &version) const;
    QList<PinRecord> pins() const;

    bool setNote(const QString &version, const QString &note);
    QString note(const QString &version) const;

    bool markSeen(kh::model::SourceId source_id, const QString &version, bool notified);
    QStringList seenVersions(kh::model::SourceId source_id) const;
    bool setNotified(kh::model::SourceId source_id, const QString &version, bool notified);

    bool recordInstallAction(const QString &version,
                             kh::model::SourceId source_id,
                             const QString &action,
                             bool success);
    QList<InstallHistoryRecord> installHistory(int limit = 100) const;

    static QString DefaultDatabasePath();

private:
    bool ensureDatabaseDirectory() const;
    bool applyMigrations();
    int schemaVersion() const;
    bool setSchemaVersion(int version);
    bool executeSqlScript(const QString &script);
    bool executeStatement(const QString &sql) const;
    void setLastError(const QString &error) const;

    QString database_path_;
    QString connection_name_;
    mutable QString last_error_;
};

}  // namespace kh::state

