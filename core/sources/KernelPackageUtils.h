#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/model/KernelInfo.h"
#include "core/model/SourceId.h"

namespace kh::sources {

struct PackageRecord {
    QString name;
    QString version;
    QString architecture;
    QDateTime buildTime;
    QString summary;
    QString checksum;
    QString changelog;
};

QStringList FedoraKernelPackageNames();
QStringList CachyKernelPackageNames(kh::model::SourceId source_id);
QStringList PackageNamesForSource(kh::model::SourceId source_id);

QList<kh::model::KernelInfo> GroupPackageRecords(
    const QList<PackageRecord> &records,
    kh::model::SourceId source_id,
    const QString &source_display_name);

QList<kh::model::KernelInfo> ParseRepoqueryOutput(
    const QByteArray &output,
    kh::model::SourceId source_id,
    const QString &source_display_name);

QList<kh::model::KernelInfo> ParsePrimaryXml(
    const QByteArray &xml,
    kh::model::SourceId source_id,
    const QString &source_display_name);

QString ShortKernelVersion(const QString &version);
QString RunningKernelVersion();
bool IsKernelRunning(const kh::model::KernelInfo &kernel);
QString SystemReleaseVersion();
bool IsRawhideSystem();
QString FindExecutable(const QStringList &candidates);

}  // namespace kh::sources
