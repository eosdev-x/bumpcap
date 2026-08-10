#include "core/config/ConfigManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

#include <utility>

#include "core/log/Log.h"

namespace kh::config {
namespace {

QJsonObject SourceConfig(bool enabled, int priority) {
    return {
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("priority"), priority},
    };
}

QJsonObject MergeJsonObjects(const QJsonObject &defaults, const QJsonObject &overrides) {
    QJsonObject merged = defaults;
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (it.value().isObject() && merged.value(it.key()).isObject()) {
            merged[it.key()] = MergeJsonObjects(merged.value(it.key()).toObject(),
                                                it.value().toObject());
        } else {
            merged[it.key()] = it.value();
        }
    }
    return merged;
}

}  // namespace

ConfigManager::ConfigManager(QObject *parent)
    : ConfigManager(DefaultConfigPath(), parent) {}

ConfigManager::ConfigManager(QString config_path, QObject *parent)
    : QObject(parent), config_path_(std::move(config_path)), config_(DefaultConfig()) {}

bool ConfigManager::load() {
    QFile file(config_path_);
    if (!file.exists()) {
        config_ = DefaultConfig();
        return save();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setLastError(QStringLiteral("Unable to open config file %1: %2")
                         .arg(config_path_, file.errorString()));
        qCWarning(kh::log::config) << last_error_;
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(QStringLiteral("Unable to parse config file %1: %2")
                         .arg(config_path_, parse_error.errorString()));
        qCWarning(kh::log::config) << last_error_;
        return false;
    }

    config_ = document.object();
    mergeWithDefaults();
    return true;
}

bool ConfigManager::save() const {
    const QFileInfo file_info(config_path_);
    QDir directory = file_info.dir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("Unable to create config directory %1")
                         .arg(directory.absolutePath()));
        qCWarning(kh::log::config) << last_error_;
        return false;
    }

    QSaveFile file(config_path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setLastError(QStringLiteral("Unable to write config file %1: %2")
                         .arg(config_path_, file.errorString()));
        qCWarning(kh::log::config) << last_error_;
        return false;
    }

    const QJsonDocument document(config_);
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setLastError(QStringLiteral("Unable to commit config file %1: %2")
                         .arg(config_path_, file.errorString()));
        qCWarning(kh::log::config) << last_error_;
        return false;
    }
    return true;
}

bool ConfigManager::resetToDefaults() {
    config_ = DefaultConfig();
    const bool saved = save();
    if (saved) {
        emit configChanged();
    }
    return saved;
}

QString ConfigManager::configPath() const {
    return config_path_;
}

QString ConfigManager::lastError() const {
    return last_error_;
}

QJsonObject ConfigManager::config() const {
    return config_;
}

bool ConfigManager::sourceEnabled(kh::model::SourceId source_id) const {
    return sourceObject(source_id).value(QStringLiteral("enabled")).toBool();
}

void ConfigManager::setSourceEnabled(kh::model::SourceId source_id, bool enabled) {
    QJsonObject source = sourceObject(source_id);
    source[QStringLiteral("enabled")] = enabled;
    setSourceObject(source_id, source);
    emit configChanged();
}

int ConfigManager::sourcePriority(kh::model::SourceId source_id) const {
    return sourceObject(source_id).value(QStringLiteral("priority")).toInt();
}

void ConfigManager::setSourcePriority(kh::model::SourceId source_id, int priority) {
    QJsonObject source = sourceObject(source_id);
    source[QStringLiteral("priority")] = priority;
    setSourceObject(source_id, source);
    emit configChanged();
}

bool ConfigManager::notificationsEnabled() const {
    return nestedObject(QStringLiteral("notifications"))
        .value(QStringLiteral("enabled"))
        .toBool(true);
}

void ConfigManager::setNotificationsEnabled(bool enabled) {
    setNestedValue(QStringLiteral("notifications"), QStringLiteral("enabled"), enabled);
    emit configChanged();
}

int ConfigManager::checkIntervalMinutes() const {
    return nestedObject(QStringLiteral("notifications"))
        .value(QStringLiteral("checkIntervalMinutes"))
        .toInt(360);
}

void ConfigManager::setCheckIntervalMinutes(int minutes) {
    setNestedValue(QStringLiteral("notifications"),
                   QStringLiteral("checkIntervalMinutes"),
                   qMax(1, minutes));
    emit configChanged();
}

QString ConfigManager::packageBackend() const {
    return nestedObject(QStringLiteral("install"))
        .value(QStringLiteral("backend"))
        .toString(QStringLiteral("auto"));
}

void ConfigManager::setPackageBackend(const QString &backend) {
    setNestedValue(QStringLiteral("install"), QStringLiteral("backend"), backend);
    emit configChanged();
}

bool ConfigManager::installHeaders() const {
    return nestedObject(QStringLiteral("install"))
        .value(QStringLiteral("installHeaders"))
        .toBool(false);
}

void ConfigManager::setInstallHeaders(bool enabled) {
    setNestedValue(QStringLiteral("install"), QStringLiteral("installHeaders"), enabled);
    emit configChanged();
}

bool ConfigManager::confirmBeforeInstallRemove() const {
    return nestedObject(QStringLiteral("install"))
        .value(QStringLiteral("confirmBeforeInstallRemove"))
        .toBool(true);
}

void ConfigManager::setConfirmBeforeInstallRemove(bool enabled) {
    setNestedValue(QStringLiteral("install"),
                   QStringLiteral("confirmBeforeInstallRemove"),
                   enabled);
    emit configChanged();
}

QString ConfigManager::afterInstallAction() const {
    return nestedObject(QStringLiteral("bootloader"))
        .value(QStringLiteral("afterInstallAction"))
        .toString(QStringLiteral("prompt"));
}

void ConfigManager::setAfterInstallAction(const QString &action) {
    setNestedValue(QStringLiteral("bootloader"), QStringLiteral("afterInstallAction"), action);
    emit configChanged();
}

QString ConfigManager::logLevel() const {
    return nestedObject(QStringLiteral("advanced"))
        .value(QStringLiteral("logLevel"))
        .toString(QStringLiteral("info"));
}

void ConfigManager::setLogLevel(const QString &level) {
    setNestedValue(QStringLiteral("advanced"), QStringLiteral("logLevel"), level);
    emit configChanged();
}

QString ConfigManager::themeMode() const {
    return nestedObject(QStringLiteral("ui"))
        .value(QStringLiteral("themeMode"))
        .toString(QStringLiteral("system"));
}

void ConfigManager::setThemeMode(const QString &mode) {
    setNestedValue(QStringLiteral("ui"), QStringLiteral("themeMode"), mode);
    emit configChanged();
}

QJsonObject ConfigManager::DefaultConfig() {
    return {
        {QStringLiteral("configVersion"), 1},
        {QStringLiteral("sources"),
         QJsonObject{
             {QStringLiteral("fedora-stable"), SourceConfig(true, 0)},
             {QStringLiteral("fedora-rawhide"), SourceConfig(false, 1)},
             {QStringLiteral("cachyos-stable"), SourceConfig(false, 2)},
             {QStringLiteral("cachyos-lts"), SourceConfig(false, 3)},
             {QStringLiteral("kernelorg-mainline"), SourceConfig(false, 4)},
         }},
        {QStringLiteral("notifications"),
         QJsonObject{
             {QStringLiteral("enabled"), true},
             {QStringLiteral("checkIntervalMinutes"), 360},
             {QStringLiteral("sourceOverrides"),
              QJsonObject{{QStringLiteral("fedora-rawhide"), false}}},
         }},
        {QStringLiteral("install"),
         QJsonObject{
             {QStringLiteral("backend"), QStringLiteral("auto")},
             {QStringLiteral("installHeaders"), false},
             {QStringLiteral("confirmBeforeInstallRemove"), true},
         }},
        {QStringLiteral("bootloader"),
         QJsonObject{{QStringLiteral("afterInstallAction"), QStringLiteral("prompt")}}},
        {QStringLiteral("ui"),
         QJsonObject{
             {QStringLiteral("detailPanelMode"), QStringLiteral("dock")},
             {QStringLiteral("compactRows"), false},
             {QStringLiteral("themeMode"), QStringLiteral("system")},
             {QStringLiteral("columnOrder"),
              QJsonArray{
                  QStringLiteral("source"),
                  QStringLiteral("version"),
                  QStringLiteral("released"),
                  QStringLiteral("status"),
                  QStringLiteral("notes"),
              }},
         }},
        {QStringLiteral("advanced"),
         QJsonObject{{QStringLiteral("logLevel"), QStringLiteral("info")}}},
    };
}

QString ConfigManager::DefaultConfigPath() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(root).filePath(QStringLiteral("bumpcap/config.json"));
}

QJsonObject ConfigManager::sourceObject(kh::model::SourceId source_id) const {
    const QString key = kh::model::SourceIdToString(source_id);
    return nestedObject(QStringLiteral("sources")).value(key).toObject();
}

void ConfigManager::setSourceObject(kh::model::SourceId source_id, const QJsonObject &source) {
    QJsonObject sources = nestedObject(QStringLiteral("sources"));
    sources[kh::model::SourceIdToString(source_id)] = source;
    config_[QStringLiteral("sources")] = sources;
}

void ConfigManager::setNestedValue(const QString &group,
                                   const QString &key,
                                   const QJsonValue &value) {
    QJsonObject object = nestedObject(group);
    object[key] = value;
    config_[group] = object;
}

QJsonObject ConfigManager::nestedObject(const QString &group) const {
    return config_.value(group).toObject();
}

void ConfigManager::mergeWithDefaults() {
    config_ = MergeJsonObjects(DefaultConfig(), config_);
}

void ConfigManager::setLastError(const QString &error) const {
    last_error_ = error;
}

}  // namespace kh::config
