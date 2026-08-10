#include "core/sources/KernelPackageUtils.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimeZone>
#include <QXmlStreamReader>

#include <sys/utsname.h>

#include <algorithm>

#include "core/log/Log.h"

namespace kh::sources {
namespace {

bool PackageNameMatches(const QString &name, const QStringList &allowed_names) {
    return allowed_names.contains(name);
}

QDateTime BuildTimeFromEpoch(const QString &seconds_text) {
    bool ok = false;
    const qint64 seconds = seconds_text.toLongLong(&ok);
    if (!ok || seconds <= 0) {
        return {};
    }
    return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC);
}

QString VersionWithArchitecture(const QString &version, const QString &architecture) {
    if (architecture.isEmpty() || version.endsWith(QLatin1Char('.') + architecture)) {
        return version;
    }
    return version + QLatin1Char('.') + architecture;
}

QString ElementLocalName(const QXmlStreamReader &reader) {
    return reader.name().toString();
}

}  // namespace

QStringList FedoraKernelPackageNames() {
    return {QStringLiteral("kernel"),
            QStringLiteral("kernel-core"),
            QStringLiteral("kernel-modules"),
            QStringLiteral("kernel-modules-extra"),
            QStringLiteral("kernel-devel"),
            QStringLiteral("kernel-headers")};
}

QStringList CachyKernelPackageNames(kh::model::SourceId source_id) {
    const bool lts = source_id == kh::model::SourceId::CachyOsLts;
    const QString base = lts ? QStringLiteral("kernel-cachyos-lts")
                             : QStringLiteral("kernel-cachyos");
    return {base,
            base + QStringLiteral("-core"),
            base + QStringLiteral("-modules"),
            base + QStringLiteral("-devel"),
            base + QStringLiteral("-headers")};
}

QStringList PackageNamesForSource(kh::model::SourceId source_id) {
    if (source_id == kh::model::SourceId::CachyOsStable ||
        source_id == kh::model::SourceId::CachyOsLts) {
        return CachyKernelPackageNames(source_id);
    }
    return FedoraKernelPackageNames();
}

QList<kh::model::KernelInfo> GroupPackageRecords(
    const QList<PackageRecord> &records,
    kh::model::SourceId source_id,
    const QString &source_display_name) {
    QHash<QString, kh::model::KernelInfo> grouped;
    const QStringList allowed_names = PackageNamesForSource(source_id);
    for (const PackageRecord &record : records) {
        if (!PackageNameMatches(record.name, allowed_names) || record.version.isEmpty()) {
            continue;
        }

        kh::model::KernelInfo &kernel = grouped[record.version];
        if (kernel.version.isEmpty()) {
            kernel.sourceId = source_id;
            kernel.sourceDisplayName = source_display_name;
            kernel.version = record.version;
            kernel.shortVersion = ShortKernelVersion(record.version);
            kernel.status = kh::model::KernelStatus::Available;
        }
        if (!kernel.subPackages.contains(record.name)) {
            kernel.subPackages.push_back(record.name);
        }
        if (!record.buildTime.isNull() &&
            (kernel.releaseDate.isNull() || record.buildTime < kernel.releaseDate)) {
            kernel.releaseDate = record.buildTime;
        }
        if (kernel.sha256.isEmpty() && !record.checksum.isEmpty()) {
            kernel.sha256 = record.checksum;
        }
        if (kernel.changelog.isEmpty() && !record.changelog.isEmpty()) {
            kernel.changelog = record.changelog;
        }
    }

    QList<kh::model::KernelInfo> kernels = grouped.values();
    std::sort(kernels.begin(), kernels.end(), [](const auto &left, const auto &right) {
        return left.version > right.version;
    });
    return kernels;
}

QList<kh::model::KernelInfo> ParseRepoqueryOutput(
    const QByteArray &output,
    kh::model::SourceId source_id,
    const QString &source_display_name) {
    QList<PackageRecord> records;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line_bytes : lines) {
        const QString line = QString::fromUtf8(line_bytes).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList columns = line.split(QLatin1Char('\t'));
        if (columns.size() < 2) {
            continue;
        }
        PackageRecord record;
        record.name = columns.value(0).trimmed();
        record.version = columns.value(1).trimmed();
        record.buildTime = BuildTimeFromEpoch(columns.value(2).trimmed());
        records.push_back(record);
    }
    return GroupPackageRecords(records, source_id, source_display_name);
}

QList<kh::model::KernelInfo> ParsePrimaryXml(
    const QByteArray &xml,
    kh::model::SourceId source_id,
    const QString &source_display_name) {
    QList<PackageRecord> records;
    QXmlStreamReader reader(xml);
    PackageRecord current;
    bool in_package = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && ElementLocalName(reader) == QStringLiteral("package")) {
            in_package = true;
            current = {};
            continue;
        }
        if (!in_package || !reader.isStartElement()) {
            if (reader.isEndElement() && ElementLocalName(reader) == QStringLiteral("package")) {
                in_package = false;
                const QString version =
                    VersionWithArchitecture(current.version, current.architecture);
                current.version = version;
                records.push_back(current);
            }
            continue;
        }

        const QString name = ElementLocalName(reader);
        if (name == QStringLiteral("name")) {
            current.name = reader.readElementText();
        } else if (name == QStringLiteral("arch")) {
            current.architecture = reader.readElementText();
        } else if (name == QStringLiteral("version")) {
            const QXmlStreamAttributes attrs = reader.attributes();
            const QString epoch = attrs.value(QStringLiteral("epoch")).toString();
            const QString version = attrs.value(QStringLiteral("ver")).toString();
            const QString release = attrs.value(QStringLiteral("rel")).toString();
            current.version = epoch.isEmpty() || epoch == QLatin1String("0")
                                  ? version + QLatin1Char('-') + release
                                  : epoch + QLatin1Char(':') + version + QLatin1Char('-') +
                                        release;
        } else if (name == QStringLiteral("time")) {
            current.buildTime = BuildTimeFromEpoch(
                reader.attributes().value(QStringLiteral("build")).toString());
        } else if (name == QStringLiteral("checksum")) {
            current.checksum = reader.readElementText();
        } else if (name == QStringLiteral("summary")) {
            current.summary = reader.readElementText();
        } else if (name == QStringLiteral("changelog")) {
            if (current.changelog.isEmpty()) {
                current.changelog = reader.readElementText();
            }
        }
    }
    if (reader.hasError()) {
        qCWarning(kh::log::sources) << "Unable to parse primary.xml"
                                    << reader.errorString();
    }
    return GroupPackageRecords(records, source_id, source_display_name);
}

QString ShortKernelVersion(const QString &version) {
    const int dash = version.indexOf(QLatin1Char('-'));
    const QString without_release = dash > 0 ? version.left(dash) : version;
    const int epoch = without_release.indexOf(QLatin1Char(':'));
    return epoch >= 0 ? without_release.mid(epoch + 1) : without_release;
}

QString RunningKernelVersion() {
    struct utsname name;
    if (uname(&name) != 0) {
        return {};
    }
    return QString::fromLocal8Bit(name.release);
}

bool IsKernelRunning(const kh::model::KernelInfo &kernel) {
    const QString running = RunningKernelVersion();
    return !running.isEmpty() && kernel.version == running;
}

QString SystemReleaseVersion() {
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("VERSION_ID="))) {
            QString value = line.mid(QStringLiteral("VERSION_ID=").size()).trimmed();
            value.remove(QLatin1Char('"'));
            return value;
        }
    }
    return {};
}

bool IsRawhideSystem() {
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll()).toLower();
    return content.contains(QStringLiteral("rawhide")) ||
           content.contains(QStringLiteral("variant_id=rawhide"));
}

QString FindExecutable(const QStringList &candidates) {
    for (const QString &candidate : candidates) {
        const QString path = QStandardPaths::findExecutable(candidate);
        if (!path.isEmpty()) {
            return path;
        }
    }
    return {};
}

}  // namespace kh::sources
