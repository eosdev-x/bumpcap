#include "core/pkg/PackageKitBackend.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include "core/log/Log.h"
#include "core/sources/KernelPackageUtils.h"

namespace kh::pkg {
namespace {

constexpr char kPackageKitService[] = "org.freedesktop.PackageKit";
constexpr char kPackageKitPath[] = "/org/freedesktop/PackageKit";
constexpr char kPackageKitInterface[] = "org.freedesktop.PackageKit";
constexpr char kTransactionInterface[] = "org.freedesktop.PackageKit.Transaction";
constexpr uint kFilterInstalled = 1u << 2;
constexpr uint kTransactionFlagOnlyTrusted = 1u << 0;

QString VersionFromPackageId(const QString &package_id) {
    const QStringList parts = package_id.split(QLatin1Char(';'));
    if (parts.size() < 3) {
        return {};
    }
    return parts.value(1) + QLatin1Char('.') + parts.value(2);
}

QString NameFromPackageId(const QString &package_id) {
    return package_id.section(QLatin1Char(';'), 0, 0);
}

}  // namespace

PackageKitBackend::PackageKitBackend(QObject *parent) : IPackageBackend(parent) {}

void PackageKitBackend::queryInstalled() {
    createTransaction(Role::QueryInstalled, OperationHandle{}, {}, {QStringLiteral("kernel*")});
}

OperationHandle PackageKitBackend::installKernel(const kh::model::KernelInfo &kernel) {
    OperationHandle handle = nextHandle();
    createTransaction(Role::ResolveForInstall, handle, kernel, packageSpecsForKernel(kernel));
    return handle;
}

OperationHandle PackageKitBackend::removeKernel(const kh::model::KernelInfo &kernel, bool force) {
    OperationHandle handle = nextHandle();
    QString reason;
    if (removalBlocked(kernel, force, &reason)) {
        QMetaObject::invokeMethod(
            this,
            [this, handle, reason]() {
                emit operationFailed(handle, reason);
                emit operationFinished(handle, false);
            },
            Qt::QueuedConnection);
        return handle;
    }
    createTransaction(Role::ResolveForRemove, handle, kernel, packageSpecsForKernel(kernel));
    return handle;
}

void PackageKitBackend::cancelOperation(const OperationHandle &handle) {
    QString path;
    for (auto it = transactions_.cbegin(); it != transactions_.cend(); ++it) {
        if (it->handle.id == handle.id) {
            path = it.key();
            break;
        }
    }
    if (path.isEmpty()) {
        return;
    }
    QDBusInterface transaction(QString::fromLatin1(kPackageKitService),
                               path,
                               QString::fromLatin1(kTransactionInterface),
                               QDBusConnection::systemBus());
    transaction.asyncCall(QStringLiteral("Cancel"));
}

void PackageKitBackend::onTransactionPackage(uint info,
                                             const QString &package_id,
                                             const QString &summary) {
    Q_UNUSED(info)
    Q_UNUSED(summary)
    const QString path = transactionPathFromMessage();
    auto it = transactions_.find(path);
    if (it == transactions_.end()) {
        return;
    }
    it->packageIds.push_back(package_id);
    if (it->role == Role::QueryInstalled) {
        kh::model::KernelInfo kernel;
        kernel.sourceId = kh::model::SourceId::FedoraStable;
        kernel.sourceDisplayName = kh::model::SourceIdDisplayName(kernel.sourceId);
        kernel.version = VersionFromPackageId(package_id);
        kernel.shortVersion = kh::sources::ShortKernelVersion(kernel.version);
        kernel.subPackages = {NameFromPackageId(package_id)};
        kernel.status = kh::sources::IsKernelRunning(kernel)
                            ? kh::model::KernelStatus::InstalledRunning
                            : kh::model::KernelStatus::Installed;
        it->queryResults.push_back(kernel);
    }
}

void PackageKitBackend::onTransactionPercentage(uint percentage) {
    const QString path = transactionPathFromMessage();
    const auto it = transactions_.constFind(path);
    if (it == transactions_.constEnd() || it->handle.id.isEmpty()) {
        return;
    }
    emit operationProgress(it->handle, qBound(0, static_cast<int>(percentage), 100), QString());
}

void PackageKitBackend::onTransactionStatus(uint status) {
    const QString path = transactionPathFromMessage();
    const auto it = transactions_.constFind(path);
    if (it == transactions_.constEnd() || it->handle.id.isEmpty()) {
        return;
    }
    emit operationProgress(it->handle, -1, QStringLiteral("PackageKit status %1").arg(status));
}

void PackageKitBackend::onTransactionError(uint code, const QString &details) {
    const QString path = transactionPathFromMessage();
    auto it = transactions_.find(path);
    if (it == transactions_.end()) {
        return;
    }
    it->error = QStringLiteral("PackageKit error %1: %2").arg(code).arg(details);
    if (!it->handle.id.isEmpty()) {
        emit operationFailed(it->handle, it->error);
    }
}

void PackageKitBackend::onTransactionFinished(uint exit_code, uint runtime) {
    Q_UNUSED(runtime)
    const QString path = transactionPathFromMessage();
    auto it = transactions_.find(path);
    if (it == transactions_.end()) {
        return;
    }
    disconnectTransactionSignals(path);
    TransactionState state = it.value();
    transactions_.erase(it);

    if (state.role == Role::QueryInstalled) {
        if (!state.error.isEmpty()) {
            emit installedQueryFailed(state.error);
            return;
        }
        QHash<QString, kh::model::KernelInfo> grouped;
        for (const kh::model::KernelInfo &kernel : state.queryResults) {
            kh::model::KernelInfo &existing = grouped[kernel.version];
            if (existing.version.isEmpty()) {
                existing = kernel;
            } else {
                existing.subPackages.append(kernel.subPackages);
            }
        }
        emit installedQueryFinished(grouped.values());
        return;
    }

    const bool packagekit_success = exit_code == 0 || exit_code == 1;
    if (!state.error.isEmpty() || !packagekit_success) {
        failOperation(state.handle,
                      state.error.isEmpty()
                          ? QStringLiteral("PackageKit transaction failed with exit code %1")
                                .arg(exit_code)
                          : state.error);
        return;
    }

    if (state.role == Role::ResolveForInstall) {
        callInstall(state);
        return;
    }
    if (state.role == Role::ResolveForRemove) {
        callRemove(state);
        return;
    }

    emit operationProgress(state.handle, 100, QStringLiteral("Done"));
    emit operationFinished(state.handle, true);
}

void PackageKitBackend::createTransaction(Role role,
                                          const OperationHandle &handle,
                                          const kh::model::KernelInfo &kernel,
                                          const QStringList &package_specs) {
    QDBusInterface daemon(QString::fromLatin1(kPackageKitService),
                          QString::fromLatin1(kPackageKitPath),
                          QString::fromLatin1(kPackageKitInterface),
                          QDBusConnection::systemBus());
    if (!daemon.isValid()) {
        const QString error = daemon.lastError().message();
        if (role == Role::QueryInstalled) {
            emit installedQueryFailed(error);
        } else {
            failOperation(handle, error);
        }
        return;
    }

    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(daemon.asyncCall(QStringLiteral("CreateTransaction")),
                                    this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [=]() {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            const QString error = reply.error().message();
            if (role == Role::QueryInstalled) {
                emit installedQueryFailed(error);
            } else {
                failOperation(handle, error);
            }
            return;
        }
        TransactionState state;
        state.handle = handle;
        state.role = role;
        state.kernel = kernel;
        state.packageSpecs = package_specs;
        const QString path = reply.value().path();
        transactions_.insert(path, state);
        connectTransactionSignals(path);
        if (role == Role::Install) {
            QDBusInterface transaction(QString::fromLatin1(kPackageKitService),
                                       path,
                                       QString::fromLatin1(kTransactionInterface),
                                       QDBusConnection::systemBus());
            transaction.asyncCall(QStringLiteral("InstallPackages"),
                                  kTransactionFlagOnlyTrusted,
                                  package_specs);
        } else if (role == Role::Remove) {
            QDBusInterface transaction(QString::fromLatin1(kPackageKitService),
                                       path,
                                       QString::fromLatin1(kTransactionInterface),
                                       QDBusConnection::systemBus());
            transaction.asyncCall(QStringLiteral("RemovePackages"),
                                  kTransactionFlagOnlyTrusted,
                                  package_specs,
                                  false,
                                  false);
        } else {
            callResolve(reply.value(), state);
        }
    });
}

void PackageKitBackend::callResolve(const QDBusObjectPath &transaction_path,
                                    const TransactionState &state) {
    QDBusInterface transaction(QString::fromLatin1(kPackageKitService),
                               transaction_path.path(),
                               QString::fromLatin1(kTransactionInterface),
                               QDBusConnection::systemBus());
    const uint filters = state.role == Role::QueryInstalled ? kFilterInstalled : 0u;
    transaction.asyncCall(QStringLiteral("Resolve"), filters, state.packageSpecs);
}

void PackageKitBackend::callInstall(const TransactionState &state) {
    if (state.packageIds.isEmpty()) {
        failOperation(state.handle, QStringLiteral("PackageKit could not resolve packages"));
        return;
    }
    createTransaction(Role::Install, state.handle, state.kernel, state.packageIds);
}

void PackageKitBackend::callRemove(const TransactionState &state) {
    if (state.packageIds.isEmpty()) {
        failOperation(state.handle, QStringLiteral("PackageKit could not resolve packages"));
        return;
    }
    createTransaction(Role::Remove, state.handle, state.kernel, state.packageIds);
}

void PackageKitBackend::connectTransactionSignals(const QString &path) {
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(QString::fromLatin1(kPackageKitService),
                path,
                QString::fromLatin1(kTransactionInterface),
                QStringLiteral("Package"),
                this,
                SLOT(onTransactionPackage(uint,QString,QString)));
    bus.connect(QString::fromLatin1(kPackageKitService),
                path,
                QString::fromLatin1(kTransactionInterface),
                QStringLiteral("Percentage"),
                this,
                SLOT(onTransactionPercentage(uint)));
    bus.connect(QString::fromLatin1(kPackageKitService),
                path,
                QString::fromLatin1(kTransactionInterface),
                QStringLiteral("PercentageChanged"),
                this,
                SLOT(onTransactionPercentage(uint)));
    bus.connect(QString::fromLatin1(kPackageKitService),
                path,
                QString::fromLatin1(kTransactionInterface),
                QStringLiteral("Status"),
                this,
                SLOT(onTransactionStatus(uint)));
    bus.connect(QString::fromLatin1(kPackageKitService),
                path,
                QString::fromLatin1(kTransactionInterface),
                QStringLiteral("StatusChanged"),
                this,
                SLOT(onTransactionStatus(uint)));
    bus.connect(QString::fromLatin1(kPackageKitService),
                path,
                QString::fromLatin1(kTransactionInterface),
                QStringLiteral("ErrorCode"),
                this,
                SLOT(onTransactionError(uint,QString)));
    bus.connect(QString::fromLatin1(kPackageKitService),
                path,
                QString::fromLatin1(kTransactionInterface),
                QStringLiteral("Finished"),
                this,
                SLOT(onTransactionFinished(uint,uint)));
}

void PackageKitBackend::disconnectTransactionSignals(const QString &path) {
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.disconnect(QString::fromLatin1(kPackageKitService),
                   path,
                   QString::fromLatin1(kTransactionInterface),
                   QStringLiteral("Package"),
                   this,
                   SLOT(onTransactionPackage(uint,QString,QString)));
    bus.disconnect(QString::fromLatin1(kPackageKitService),
                   path,
                   QString::fromLatin1(kTransactionInterface),
                   QStringLiteral("Percentage"),
                   this,
                   SLOT(onTransactionPercentage(uint)));
    bus.disconnect(QString::fromLatin1(kPackageKitService),
                   path,
                   QString::fromLatin1(kTransactionInterface),
                   QStringLiteral("PercentageChanged"),
                   this,
                   SLOT(onTransactionPercentage(uint)));
    bus.disconnect(QString::fromLatin1(kPackageKitService),
                   path,
                   QString::fromLatin1(kTransactionInterface),
                   QStringLiteral("Status"),
                   this,
                   SLOT(onTransactionStatus(uint)));
    bus.disconnect(QString::fromLatin1(kPackageKitService),
                   path,
                   QString::fromLatin1(kTransactionInterface),
                   QStringLiteral("StatusChanged"),
                   this,
                   SLOT(onTransactionStatus(uint)));
    bus.disconnect(QString::fromLatin1(kPackageKitService),
                   path,
                   QString::fromLatin1(kTransactionInterface),
                   QStringLiteral("ErrorCode"),
                   this,
                   SLOT(onTransactionError(uint,QString)));
    bus.disconnect(QString::fromLatin1(kPackageKitService),
                   path,
                   QString::fromLatin1(kTransactionInterface),
                   QStringLiteral("Finished"),
                   this,
                   SLOT(onTransactionFinished(uint,uint)));
}

QString PackageKitBackend::transactionPathFromMessage() const {
    return calledFromDBus() ? message().path() : QString();
}

QStringList PackageKitBackend::packageSpecsForKernel(
    const kh::model::KernelInfo &kernel) const {
    QStringList specs;
    const QStringList package_names =
        kernel.subPackages.isEmpty()
            ? kh::sources::PackageNamesForSource(kernel.sourceId)
            : kernel.subPackages;
    for (const QString &package_name : package_names) {
        specs.push_back(package_name + QLatin1Char('-') + kernel.version);
    }
    return specs;
}

bool PackageKitBackend::removalBlocked(const kh::model::KernelInfo &kernel,
                                       bool force,
                                       QString *reason) const {
    if (kernel.status == kh::model::KernelStatus::InstalledRunning ||
        kh::sources::IsKernelRunning(kernel)) {
        *reason = QStringLiteral("Refusing to remove the currently running kernel.");
        return true;
    }
    if (kernel.isPinned && !force) {
        *reason = QStringLiteral("Refusing to remove a pinned kernel without force.");
        return true;
    }
    return false;
}

OperationHandle PackageKitBackend::nextHandle() {
    return {QStringLiteral("packagekit-%1").arg(next_operation_id_++)};
}

void PackageKitBackend::failOperation(const OperationHandle &handle, const QString &error) {
    emit operationFailed(handle, error);
    emit operationFinished(handle, false);
}

}  // namespace kh::pkg
