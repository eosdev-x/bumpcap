#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "core/model/SourceId.h"

namespace kh::config {

class ConfigManager : public QObject {
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);
    explicit ConfigManager(QString config_path, QObject *parent = nullptr);

    bool load();
    bool save() const;
    bool resetToDefaults();

    QString configPath() const;
    QString lastError() const;
    QJsonObject config() const;

    bool sourceEnabled(kh::model::SourceId source_id) const;
    void setSourceEnabled(kh::model::SourceId source_id, bool enabled);
    int sourcePriority(kh::model::SourceId source_id) const;
    void setSourcePriority(kh::model::SourceId source_id, int priority);

    bool notificationsEnabled() const;
    void setNotificationsEnabled(bool enabled);
    int checkIntervalMinutes() const;
    void setCheckIntervalMinutes(int minutes);

    QString packageBackend() const;
    void setPackageBackend(const QString &backend);
    bool installHeaders() const;
    void setInstallHeaders(bool enabled);
    bool confirmBeforeInstallRemove() const;
    void setConfirmBeforeInstallRemove(bool enabled);

    QString afterInstallAction() const;
    void setAfterInstallAction(const QString &action);
    QString logLevel() const;
    void setLogLevel(const QString &level);

    // "light" / "dark" / "system". Consumed by kh::gui::theme::ThemeManager.
    QString themeMode() const;
    void setThemeMode(const QString &mode);

    static QJsonObject DefaultConfig();
    static QString DefaultConfigPath();

signals:
    void configChanged();

private:
    QJsonObject sourceObject(kh::model::SourceId source_id) const;
    void setSourceObject(kh::model::SourceId source_id, const QJsonObject &source);
    void setNestedValue(const QString &group, const QString &key, const QJsonValue &value);
    QJsonObject nestedObject(const QString &group) const;
    void mergeWithDefaults();
    void setLastError(const QString &error) const;

    QString config_path_;
    QJsonObject config_;
    mutable QString last_error_;
};

}  // namespace kh::config

