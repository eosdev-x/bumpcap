#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDBusInterface>
#include <QDBusReply>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <memory>
#include <optional>

#include "core/config/ConfigManager.h"
#include "core/log/Log.h"
#include "core/model/KernelInfo.h"
#include "core/model/SourceId.h"
#include "core/pkg/DnfCliBackend.h"
#include "core/repo/KernelRepository.h"
#include "core/sources/FedoraStableSource.h"
#include "core/state/StateStore.h"

namespace kh::cli {
namespace {

constexpr int kSuccess = 0;
constexpr int kUpdatesAvailable = 1;
constexpr int kError = 2;
constexpr int kCliTimeoutMs = 30000;

struct Options {
    bool json = false;
    bool yes = false;
    bool force = false;
    QString source_filter;
    QString set_default_kernel;
    QString boot_once_kernel;
};

struct RefreshResult {
    bool ok = false;
    bool timed_out = false;
    QStringList errors;
    QList<kh::model::KernelInfo> kernels;
};

struct KernelCounts {
    int available = 0;
    int installed = 0;
    int updates = 0;
};

QTextStream &Out() {
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &Err() {
    static QTextStream stream(stderr);
    return stream;
}

void PrintError(const QString &message, const Options &options) {
    if (options.json) {
        QJsonObject object;
        object.insert(QStringLiteral("ok"), false);
        object.insert(QStringLiteral("error"), message);
        Out() << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
        return;
    }
    Err() << "error: " << message << Qt::endl;
}

QJsonObject JsonOk(const QString &message = QString()) {
    QJsonObject object;
    object.insert(QStringLiteral("ok"), true);
    if (!message.isEmpty()) {
        object.insert(QStringLiteral("message"), message);
    }
    return object;
}

void PrintOk(const QString &message, const Options &options) {
    if (options.json) {
        Out() << QJsonDocument(JsonOk(message)).toJson(QJsonDocument::Compact) << Qt::endl;
        return;
    }
    if (!message.isEmpty()) {
        Out() << message << Qt::endl;
    }
}

bool OpenState(kh::state::StateStore *store, const Options &options) {
    if (store->open()) {
        return true;
    }
    PrintError(store->lastError(), options);
    return false;
}

QStringList PositionalArgs(const QCommandLineParser &parser) {
    QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        args.removeFirst();
    }
    return args;
}

bool IsInstalledStatus(kh::model::KernelStatus status) {
    return status == kh::model::KernelStatus::Installed ||
           status == kh::model::KernelStatus::InstalledRunning;
}

QString KernelStatusToString(kh::model::KernelStatus status) {
    switch (status) {
    case kh::model::KernelStatus::Available:
        return QStringLiteral("available");
    case kh::model::KernelStatus::Installed:
        return QStringLiteral("installed");
    case kh::model::KernelStatus::InstalledRunning:
        return QStringLiteral("installed-running");
    case kh::model::KernelStatus::UpdateAvailable:
        return QStringLiteral("update-available");
    }
    return QStringLiteral("unknown");
}

QJsonObject KernelToJson(const kh::model::KernelInfo &kernel) {
    QJsonObject object;
    object.insert(QStringLiteral("sourceId"), kh::model::SourceIdToString(kernel.sourceId));
    object.insert(QStringLiteral("sourceDisplayName"), kernel.sourceDisplayName);
    object.insert(QStringLiteral("version"), kernel.version);
    object.insert(QStringLiteral("shortVersion"), kernel.shortVersion);
    object.insert(QStringLiteral("status"), KernelStatusToString(kernel.status));
    object.insert(QStringLiteral("isPinned"), kernel.isPinned);
    object.insert(QStringLiteral("notes"), kernel.notes);
    if (!kernel.releaseDate.isNull()) {
        object.insert(QStringLiteral("releaseDate"),
                      kernel.releaseDate.toString(Qt::ISODateWithMs));
    }
    QJsonArray packages;
    for (const QString &package : kernel.subPackages) {
        packages.append(package);
    }
    object.insert(QStringLiteral("subPackages"), packages);
    return object;
}

QJsonArray KernelsToJson(const QList<kh::model::KernelInfo> &kernels) {
    QJsonArray array;
    for (const kh::model::KernelInfo &kernel : kernels) {
        array.append(KernelToJson(kernel));
    }
    return array;
}

QStringList SplitSourceFilter(const Options &options) {
    QStringList filters =
        options.source_filter.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &filter : filters) {
        filter = filter.trimmed();
    }
    filters.removeAll(QString());
    return filters;
}

bool SourceFilterAllows(kh::model::SourceId source_id,
                        const kh::config::ConfigManager &config,
                        const Options &options,
                        QString *error) {
    const QString source_key = kh::model::SourceIdToString(source_id);
    const QStringList filters = SplitSourceFilter(options);
    if (filters.isEmpty()) {
        return config.sourceEnabled(source_id);
    }

    for (const QString &filter : filters) {
        const std::optional<kh::model::SourceId> parsed =
            kh::model::SourceIdFromString(QStringView{filter});
        if (!parsed.has_value()) {
            *error = QStringLiteral("Unknown source: %1").arg(filter);
            return false;
        }
        if (filter == source_key) {
            return true;
        }
    }
    return false;
}

bool AddSources(kh::repo::KernelRepository *repo,
                const kh::config::ConfigManager &config,
                const Options &options,
                QString *error) {
    if (SourceFilterAllows(kh::model::SourceId::FedoraStable, config, options, error)) {
        repo->addSource(std::make_shared<kh::sources::FedoraStableSource>());
    }
    if (!error->isEmpty()) {
        return false;
    }

    for (const QString &filter : SplitSourceFilter(options)) {
        if (filter != kh::model::SourceIdToString(kh::model::SourceId::FedoraStable)) {
            *error = QStringLiteral("Source is not supported by this CLI yet: %1").arg(filter);
            return false;
        }
    }
    return true;
}

RefreshResult RefreshKernels(const Options &options) {
    RefreshResult result;

    kh::config::ConfigManager config;
    if (!config.load()) {
        result.errors.push_back(config.lastError());
        return result;
    }

    kh::state::StateStore store;
    if (!store.open()) {
        result.errors.push_back(store.lastError());
        return result;
    }

    kh::pkg::DnfCliBackend backend;
    kh::repo::KernelRepository repo;
    repo.setStateStore(&store);
    repo.setPackageBackend(&backend);

    QString source_error;
    if (!AddSources(&repo, config, options, &source_error)) {
        result.errors.push_back(source_error);
        return result;
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    bool finished = false;
    QObject::connect(&repo,
                     &kh::repo::KernelRepository::kernelListChanged,
                     &loop,
                     [&result](const QList<kh::model::KernelInfo> &kernels) {
                         result.kernels = kernels;
                     });
    QObject::connect(&repo,
                     &kh::repo::KernelRepository::refreshFailed,
                     &loop,
                     [&result](const QString &error) {
                         result.errors.push_back(error);
                     });
    QObject::connect(&repo, &kh::repo::KernelRepository::refreshFinished, &loop, [&]() {
        finished = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        result.timed_out = true;
        loop.quit();
    });

    repo.refresh();
    if (!finished) {
        timeout.start(kCliTimeoutMs);
        loop.exec();
    }

    result.ok = finished && !result.timed_out && result.errors.isEmpty();
    if (result.timed_out) {
        result.errors.push_back(QStringLiteral("Timed out while refreshing kernels"));
    }
    return result;
}

bool KernelLooksNewerThanInstalled(const kh::model::KernelInfo &candidate,
                                   const QList<kh::model::KernelInfo> &kernels) {
    if (candidate.status == kh::model::KernelStatus::UpdateAvailable) {
        return true;
    }
    if (candidate.status != kh::model::KernelStatus::Available) {
        return false;
    }

    bool found_installed = false;
    QDateTime latest_installed_date;
    QString latest_installed_version;
    for (const kh::model::KernelInfo &kernel : kernels) {
        if (kernel.sourceId != candidate.sourceId || !IsInstalledStatus(kernel.status)) {
            continue;
        }
        found_installed = true;
        if (!kernel.releaseDate.isNull() &&
            (latest_installed_date.isNull() || kernel.releaseDate > latest_installed_date)) {
            latest_installed_date = kernel.releaseDate;
        }
        if (kernel.version > latest_installed_version) {
            latest_installed_version = kernel.version;
        }
    }

    if (!found_installed) {
        return false;
    }
    if (!candidate.releaseDate.isNull() && !latest_installed_date.isNull()) {
        return candidate.releaseDate > latest_installed_date;
    }
    return candidate.version > latest_installed_version;
}

KernelCounts CountKernels(const QList<kh::model::KernelInfo> &kernels) {
    KernelCounts counts;
    for (const kh::model::KernelInfo &kernel : kernels) {
        if (IsInstalledStatus(kernel.status)) {
            ++counts.installed;
        } else {
            ++counts.available;
        }
        if (KernelLooksNewerThanInstalled(kernel, kernels)) {
            ++counts.updates;
        }
    }
    return counts;
}

void SortKernels(QList<kh::model::KernelInfo> *kernels) {
    std::sort(kernels->begin(), kernels->end(), [](const auto &left, const auto &right) {
        if (left.sourceId != right.sourceId) {
            return kh::model::SourceIdToString(left.sourceId) <
                   kh::model::SourceIdToString(right.sourceId);
        }
        if (left.releaseDate.isValid() && right.releaseDate.isValid() &&
            left.releaseDate != right.releaseDate) {
            return left.releaseDate > right.releaseDate;
        }
        return left.version > right.version;
    });
}

void PrintKernels(const QList<kh::model::KernelInfo> &kernels, const Options &options) {
    if (options.json) {
        Out() << QJsonDocument(KernelsToJson(kernels)).toJson(QJsonDocument::Compact)
              << Qt::endl;
        return;
    }

    if (kernels.isEmpty()) {
        Out() << "No kernels were found." << Qt::endl;
        return;
    }

    for (const kh::model::KernelInfo &kernel : kernels) {
        QStringList markers;
        markers.push_back(KernelStatusToString(kernel.status));
        markers.push_back(kernel.sourceDisplayName);
        if (kernel.isPinned) {
            markers.push_back(QStringLiteral("pinned"));
        }

        Out() << kernel.version << " [" << markers.join(QStringLiteral(", ")) << "]";
        if (!kernel.notes.isEmpty()) {
            Out() << " - " << kernel.notes;
        }
        Out() << Qt::endl;
    }
}

int ListCommand(const Options &options) {
    RefreshResult refresh = RefreshKernels(options);
    if (!refresh.ok) {
        PrintError(refresh.errors.join(QStringLiteral("; ")), options);
        return kError;
    }

    SortKernels(&refresh.kernels);
    PrintKernels(refresh.kernels, options);
    return kSuccess;
}

int CheckCommand(const Options &options) {
    RefreshResult refresh = RefreshKernels(options);
    if (!refresh.ok) {
        PrintError(refresh.errors.join(QStringLiteral("; ")), options);
        return kError;
    }

    const KernelCounts counts = CountKernels(refresh.kernels);
    if (options.json) {
        QJsonObject object;
        object.insert(QStringLiteral("available"), counts.available);
        object.insert(QStringLiteral("installed"), counts.installed);
        object.insert(QStringLiteral("updates"), counts.updates);
        Out() << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
    } else {
        Out() << counts.available << " kernels available, " << counts.installed
              << " installed, " << counts.updates << " updates" << Qt::endl;
    }
    return counts.updates > 0 ? kUpdatesAvailable : kSuccess;
}

std::optional<kh::model::KernelInfo> FindKernelByVersion(
    const QList<kh::model::KernelInfo> &kernels,
    const QString &version) {
    for (const kh::model::KernelInfo &kernel : kernels) {
        if (kernel.version == version) {
            return kernel;
        }
    }
    return std::nullopt;
}

int RunPackageOperation(const kh::model::KernelInfo &kernel,
                        bool install,
                        const Options &options) {
    kh::pkg::DnfCliBackend backend;
    kh::pkg::OperationHandle handle;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    bool finished = false;
    bool success = false;
    QString error_message;
    QString last_progress;

    QObject::connect(&backend,
                     &kh::pkg::IPackageBackend::operationProgress,
                     &loop,
                     [&](const kh::pkg::OperationHandle &progress_handle,
                         int percent,
                         const QString &status_text) {
                         if (progress_handle.id != handle.id || options.json ||
                             status_text.isEmpty() || status_text == last_progress) {
                             return;
                         }
                         last_progress = status_text;
                         if (percent >= 0) {
                             Err() << percent << "% ";
                         }
                         Err() << status_text << Qt::endl;
                     });
    QObject::connect(&backend,
                     &kh::pkg::IPackageBackend::operationFailed,
                     &loop,
                     [&](const kh::pkg::OperationHandle &failed_handle,
                         const QString &message) {
                         if (failed_handle.id == handle.id) {
                             error_message = message;
                         }
                     });
    QObject::connect(&backend,
                     &kh::pkg::IPackageBackend::operationFinished,
                     &loop,
                     [&](const kh::pkg::OperationHandle &finished_handle, bool operation_success) {
                         if (finished_handle.id != handle.id) {
                             return;
                         }
                         finished = true;
                         success = operation_success;
                         loop.quit();
                     });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        backend.cancelOperation(handle);
        error_message = QStringLiteral("Timed out while %1 %2")
                            .arg(install ? QStringLiteral("installing")
                                         : QStringLiteral("removing"),
                                 kernel.version);
        loop.quit();
    });

    handle = install ? backend.installKernel(kernel)
                     : backend.removeKernel(kernel, options.force);
    timeout.start(kCliTimeoutMs);
    loop.exec();

    if (!finished || !success) {
        if (error_message.isEmpty()) {
            error_message = QStringLiteral("Package operation failed");
        }
        PrintError(error_message, options);
        return kError;
    }

    const QString message = QStringLiteral("%1 %2")
                                .arg(kernel.version,
                                     install ? QStringLiteral("installed")
                                             : QStringLiteral("removed"));
    PrintOk(message, options);
    return kSuccess;
}

int InstallCommand(const QStringList &args, const Options &options) {
    if (args.size() != 1) {
        PrintError(QStringLiteral("install requires exactly one <version> argument"), options);
        return kError;
    }

    RefreshResult refresh = RefreshKernels(options);
    if (!refresh.ok) {
        PrintError(refresh.errors.join(QStringLiteral("; ")), options);
        return kError;
    }

    const QString version = args.at(0);
    const std::optional<kh::model::KernelInfo> kernel =
        FindKernelByVersion(refresh.kernels, version);
    if (!kernel.has_value()) {
        PrintError(QStringLiteral("Kernel not found: %1").arg(version), options);
        return kError;
    }
    if (IsInstalledStatus(kernel->status)) {
        PrintOk(QStringLiteral("%1 is already installed").arg(version), options);
        return kSuccess;
    }

    return RunPackageOperation(*kernel, true, options);
}

int RemoveCommand(const QStringList &args, const Options &options) {
    if (args.size() != 1) {
        PrintError(QStringLiteral("remove requires exactly one <version> argument"), options);
        return kError;
    }

    RefreshResult refresh = RefreshKernels(options);
    if (!refresh.ok) {
        PrintError(refresh.errors.join(QStringLiteral("; ")), options);
        return kError;
    }

    const QString version = args.at(0);
    const std::optional<kh::model::KernelInfo> kernel =
        FindKernelByVersion(refresh.kernels, version);
    if (!kernel.has_value()) {
        PrintError(QStringLiteral("Kernel not found: %1").arg(version), options);
        return kError;
    }
    if (!IsInstalledStatus(kernel->status)) {
        PrintError(QStringLiteral("%1 is not installed").arg(version), options);
        return kError;
    }

    return RunPackageOperation(*kernel, false, options);
}

int PinCommand(const QStringList &args, bool pin, const Options &options) {
    if (args.size() != 1) {
        PrintError(QStringLiteral("%1 requires exactly one <version> argument")
                       .arg(pin ? QStringLiteral("pin") : QStringLiteral("unpin")),
                   options);
        return kError;
    }
    kh::state::StateStore store;
    if (!OpenState(&store, options)) {
        return kError;
    }
    const bool ok = pin ? store.setPinned(args.at(0), kh::model::SourceId::FedoraStable)
                        : store.removePin(args.at(0));
    if (!ok) {
        PrintError(store.lastError(), options);
        return kError;
    }
    PrintOk(QStringLiteral("%1 %2").arg(args.at(0), pin ? QStringLiteral("pinned")
                                                        : QStringLiteral("unpinned")),
            options);
    return kSuccess;
}

int NoteCommand(const QStringList &args, const Options &options) {
    if (args.size() < 2) {
        PrintError(QStringLiteral("note requires <version> and text arguments"), options);
        return kError;
    }
    kh::state::StateStore store;
    if (!OpenState(&store, options)) {
        return kError;
    }
    const QString version = args.at(0);
    QStringList note_parts = args;
    note_parts.removeFirst();
    const QString note = note_parts.join(QLatin1Char(' '));
    if (!store.setNote(version, note)) {
        PrintError(store.lastError(), options);
        return kError;
    }
    PrintOk(QStringLiteral("note saved for %1").arg(version), options);
    return kSuccess;
}

QJsonArray ExportNotesArray(const QString &database_path, QString *error) {
    QJsonArray notes;
    const QString connection_name =
        QStringLiteral("bumpcap-cli-export-%1").arg(QCoreApplication::applicationPid());
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name);
    database.setDatabaseName(database_path);
    if (!database.open()) {
        *error = database.lastError().text();
        QSqlDatabase::removeDatabase(connection_name);
        return notes;
    }
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("SELECT version, note, updated_at FROM notes ORDER BY version"))) {
        *error = query.lastError().text();
        database.close();
        QSqlDatabase::removeDatabase(connection_name);
        return notes;
    }
    while (query.next()) {
        QJsonObject object;
        object.insert(QStringLiteral("version"), query.value(0).toString());
        object.insert(QStringLiteral("note"), query.value(1).toString());
        object.insert(QStringLiteral("updatedAt"), query.value(2).toLongLong());
        notes.append(object);
    }
    database.close();
    QSqlDatabase::removeDatabase(connection_name);
    return notes;
}

int NotesExportCommand(const Options &options) {
    kh::state::StateStore store;
    if (!OpenState(&store, options)) {
        return kError;
    }
    const QString database_path = store.databasePath();
    store.close();

    QString error;
    const QJsonArray notes = ExportNotesArray(database_path, &error);
    if (!error.isEmpty()) {
        PrintError(QStringLiteral("Unable to export notes: %1").arg(error), options);
        return kError;
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("bumpcap-notes-v1"));
    root.insert(QStringLiteral("exportedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("notes"), notes);
    Out() << QJsonDocument(root).toJson(QJsonDocument::Indented);
    return kSuccess;
}

QJsonArray NotesArrayFromDocument(const QJsonDocument &document) {
    if (document.isArray()) {
        return document.array();
    }
    if (document.isObject()) {
        return document.object().value(QStringLiteral("notes")).toArray();
    }
    return {};
}

int NotesImportCommand(const QStringList &args, const Options &options) {
    if (args.size() != 1) {
        PrintError(QStringLiteral("notes-import requires exactly one <file> argument"), options);
        return kError;
    }
    QFile file(args.at(0));
    if (!file.open(QIODevice::ReadOnly)) {
        PrintError(QStringLiteral("Unable to open %1: %2").arg(args.at(0), file.errorString()),
                   options);
        return kError;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        PrintError(QStringLiteral("Invalid notes JSON: %1").arg(parse_error.errorString()),
                   options);
        return kError;
    }

    kh::state::StateStore store;
    if (!OpenState(&store, options)) {
        return kError;
    }
    int imported = 0;
    for (const QJsonValue &value : NotesArrayFromDocument(document)) {
        const QJsonObject object = value.toObject();
        const QString version = object.value(QStringLiteral("version")).toString();
        const QString note = object.value(QStringLiteral("note")).toString();
        if (version.isEmpty()) {
            continue;
        }
        if (!store.setNote(version, note)) {
            PrintError(store.lastError(), options);
            return kError;
        }
        ++imported;
    }
    PrintOk(QStringLiteral("imported %1 notes").arg(imported), options);
    return kSuccess;
}

int BootOrderCommand(const QStringList &args, const Options &options) {
    if (!args.isEmpty()) {
        PrintError(QStringLiteral("unknown boot-order argument: %1").arg(args.join(QLatin1Char(' '))),
                   options);
        return kError;
    }

    if (!options.boot_once_kernel.isEmpty()) {
        PrintError(QStringLiteral("boot-once is not implemented yet"), options);
        return kError;
    }

    if (!options.set_default_kernel.isEmpty()) {
        const QString kernel_path =
            options.set_default_kernel.startsWith(QStringLiteral("/boot/vmlinuz-"))
                ? options.set_default_kernel
                : QStringLiteral("/boot/vmlinuz-%1").arg(options.set_default_kernel);
        QDBusInterface iface(QStringLiteral("org.bumpcap.Helper1"),
                             QStringLiteral("/org/bumpcap/Helper1"),
                             QStringLiteral("org.bumpcap.Helper1"),
                             QDBusConnection::systemBus());
        QDBusReply<void> reply = iface.call(QStringLiteral("SetDefaultKernel"), kernel_path);
        if (!reply.isValid()) {
            PrintError(QStringLiteral("Unable to set default kernel: %1")
                           .arg(reply.error().message()),
                       options);
            return kError;
        }
        PrintOk(QStringLiteral("default kernel set to %1").arg(kernel_path), options);
        return kSuccess;
    }

    QProcess grubby;
    grubby.start(QStringLiteral("grubby"), {QStringLiteral("--info=ALL")});
    if (!grubby.waitForFinished(10000) || grubby.exitCode() != 0) {
        PrintError(QStringLiteral("Unable to query boot order with grubby"), options);
        return kError;
    }
    const QString output = QString::fromLocal8Bit(grubby.readAllStandardOutput());
    if (options.json) {
        QJsonObject object;
        object.insert(QStringLiteral("raw"), output);
        Out() << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
    } else {
        Out() << output;
    }
    return kSuccess;
}

void AddCommonOptions(QCommandLineParser *parser) {
    parser->addOption({QStringLiteral("json"), QStringLiteral("Emit machine-readable JSON")});
    parser->addOption({QStringLiteral("yes"), QStringLiteral("Skip confirmations")});
    parser->addOption({QStringLiteral("force"), QStringLiteral("Allow unsafe operations")});
    parser->addOption({QStringLiteral("source"),
                       QStringLiteral("Limit command to a comma-separated source list"),
                       QStringLiteral("sources")});
    parser->addOption({QStringLiteral("set-default"),
                       QStringLiteral("Set the default kernel for boot-order"),
                       QStringLiteral("version-or-path")});
    parser->addOption({QStringLiteral("boot-once"),
                       QStringLiteral("Boot a kernel once on next reboot for boot-order"),
                       QStringLiteral("version-or-path")});
}

int Dispatch(const QString &command, const QStringList &args, const Options &options) {
    if (command == QStringLiteral("list")) {
        return ListCommand(options);
    }
    if (command == QStringLiteral("check")) {
        return CheckCommand(options);
    }
    if (command == QStringLiteral("install")) {
        return InstallCommand(args, options);
    }
    if (command == QStringLiteral("remove")) {
        return RemoveCommand(args, options);
    }
    if (command == QStringLiteral("pin")) {
        return PinCommand(args, true, options);
    }
    if (command == QStringLiteral("unpin")) {
        return PinCommand(args, false, options);
    }
    if (command == QStringLiteral("note")) {
        return NoteCommand(args, options);
    }
    if (command == QStringLiteral("boot-order")) {
        return BootOrderCommand(args, options);
    }
    if (command == QStringLiteral("notes-export")) {
        return NotesExportCommand(options);
    }
    if (command == QStringLiteral("notes-import")) {
        return NotesImportCommand(args, options);
    }

    PrintError(QStringLiteral("unknown command: %1").arg(command), options);
    return kError;
}

}  // namespace
}  // namespace kh::cli

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("bumpcap-cli"));
    QCoreApplication::setOrganizationName(QStringLiteral("Bumpcap"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    kh::log::InitializeLogging(QtWarningMsg);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Bumpcap command-line interface"));
    parser.addHelpOption();
    parser.addVersionOption();
    kh::cli::AddCommonOptions(&parser);
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("Command: list, check, install, remove, pin, "
                                                "unpin, note, boot-order, notes-export, "
                                                "notes-import"));
    parser.addPositionalArgument(QStringLiteral("args"), QStringLiteral("Command arguments"),
                                 QStringLiteral("[args...]"));
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(kh::cli::kError);
    }

    kh::cli::Options options;
    options.json = parser.isSet(QStringLiteral("json"));
    options.yes = parser.isSet(QStringLiteral("yes"));
    options.force = parser.isSet(QStringLiteral("force"));
    options.source_filter = parser.value(QStringLiteral("source"));
    options.set_default_kernel = parser.value(QStringLiteral("set-default"));
    options.boot_once_kernel = parser.value(QStringLiteral("boot-once"));

    return kh::cli::Dispatch(positional.first(), kh::cli::PositionalArgs(parser), options);
}
