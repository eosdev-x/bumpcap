#include "core/pkg/DnfCliBackend.h"

#include <QRegularExpression>
#include <QStandardPaths>

#include "core/log/Log.h"
#include "core/sources/KernelPackageUtils.h"

namespace kh::pkg {

DnfCliBackend::DnfCliBackend(QObject *parent) : IPackageBackend(parent) {}

DnfCliBackend::~DnfCliBackend() {
    if (query_process_ != nullptr) {
        query_process_->kill();
    }
    for (const OperationState &state : std::as_const(operations_)) {
        if (state.process != nullptr) {
            state.process->kill();
        }
    }
}

void DnfCliBackend::queryInstalled() {
    if (query_process_ != nullptr) {
        emit installedQueryFailed(QStringLiteral("Installed kernel query is already running"));
        return;
    }
    query_process_ = new QProcess(this);
    query_process_->setProgram(QStringLiteral("rpm"));
    query_process_->setArguments({QStringLiteral("-qa"),
                                  QStringLiteral("--qf"),
                                  QStringLiteral("%{NAME}\t%{VERSION}-%{RELEASE}.%{ARCH}\t%{BUILDTIME}\n"),
                                  QStringLiteral("kernel*")});
    QObject::connect(query_process_,
                     &QProcess::finished,
                     this,
                     [this](int exit_code, QProcess::ExitStatus exit_status) {
                         QProcess *process = query_process_;
                         query_process_ = nullptr;
                         const QByteArray output = process->readAllStandardOutput();
                         const QString error =
                             QString::fromUtf8(process->readAllStandardError()).trimmed();
                         process->deleteLater();
                         if (exit_status != QProcess::NormalExit || exit_code != 0) {
                             emit installedQueryFailed(
                                 error.isEmpty() ? QStringLiteral("rpm query failed") : error);
                             return;
                         }
                         QList<kh::model::KernelInfo> kernels =
                             kh::sources::ParseRepoqueryOutput(
                                 output,
                                 kh::model::SourceId::FedoraStable,
                                 kh::model::SourceIdDisplayName(
                                     kh::model::SourceId::FedoraStable));
                         for (kh::model::KernelInfo &kernel : kernels) {
                             kernel.status = kh::sources::IsKernelRunning(kernel)
                                                 ? kh::model::KernelStatus::InstalledRunning
                                                 : kh::model::KernelStatus::Installed;
                         }
                         emit installedQueryFinished(kernels);
                     });
    query_process_->start();
}

OperationHandle DnfCliBackend::installKernel(const kh::model::KernelInfo &kernel) {
    OperationHandle handle{QStringLiteral("dnf-%1").arg(next_operation_id_++)};
    QStringList arguments;
    arguments << QStringLiteral("install") << QStringLiteral("-y");
    arguments << packageSpecsForKernel(kernel);
    return startDnfOperation(QStringLiteral("install"), handle, kernel, arguments);
}

OperationHandle DnfCliBackend::removeKernel(const kh::model::KernelInfo &kernel, bool force) {
    OperationHandle handle{QStringLiteral("dnf-%1").arg(next_operation_id_++)};
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

    QStringList arguments;
    arguments << QStringLiteral("remove") << QStringLiteral("-y");
    arguments << packageSpecsForKernel(kernel);
    return startDnfOperation(QStringLiteral("remove"), handle, kernel, arguments);
}

void DnfCliBackend::cancelOperation(const OperationHandle &handle) {
    auto it = operations_.find(handle.id);
    if (it == operations_.end()) {
        return;
    }
    it->process->kill();
}

OperationHandle DnfCliBackend::startDnfOperation(const QString &action,
                                                 const OperationHandle &handle,
                                                 const kh::model::KernelInfo &kernel,
                                                 const QStringList &arguments) {
    Q_UNUSED(kernel)
    const QString dnf = dnfExecutable();
    if (dnf.isEmpty()) {
        QMetaObject::invokeMethod(
            this,
            [this, handle]() {
                emit operationFailed(handle, QStringLiteral("Neither dnf5 nor dnf is available"));
                emit operationFinished(handle, false);
            },
            Qt::QueuedConnection);
        return handle;
    }

    QProcess *process = new QProcess(this);
    const QString pkexec = privilegedExecutable();
    if (pkexec.isEmpty()) {
        process->setProgram(dnf);
        process->setArguments(arguments);
    } else {
        process->setProgram(pkexec);
        QStringList privileged_arguments;
        privileged_arguments << dnf << arguments;
        process->setArguments(privileged_arguments);
    }

    operations_.insert(handle.id, OperationState{handle, process, action});
    QObject::connect(process, &QProcess::readyReadStandardOutput, this, [this, handle]() {
        handleReadyRead(handle);
    });
    QObject::connect(process, &QProcess::readyReadStandardError, this, [this, handle]() {
        handleReadyRead(handle);
    });
    QObject::connect(process,
                     &QProcess::finished,
                     this,
                     [this, handle](int exit_code, QProcess::ExitStatus exit_status) {
                         finishOperation(handle, exit_code, exit_status);
                     });
    emit operationProgress(handle, 0, action);
    process->start();
    return handle;
}

QStringList DnfCliBackend::packageSpecsForKernel(
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

bool DnfCliBackend::removalBlocked(const kh::model::KernelInfo &kernel,
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

void DnfCliBackend::handleReadyRead(const OperationHandle &handle) {
    auto it = operations_.find(handle.id);
    if (it == operations_.end()) {
        return;
    }
    const QByteArray data =
        it->process->readAllStandardOutput() + it->process->readAllStandardError();
    const QString text = QString::fromUtf8(data);
    static const QRegularExpression percent_regex(
        QStringLiteral("^\\s*(\\d{1,3})%.*$"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression count_regex(
        QStringLiteral("^\\s*\\[(\\d+)/(\\d+)\\]\\s*$"),
        QRegularExpression::MultilineOption);
    const QRegularExpressionMatch percent_match = percent_regex.match(text);
    const QRegularExpressionMatch count_match = count_regex.match(text);
    int percent = -1;
    if (percent_match.hasMatch()) {
        percent = qBound(0, percent_match.captured(1).toInt(), 100);
    } else if (count_match.hasMatch()) {
        const int current = count_match.captured(1).toInt();
        const int total = count_match.captured(2).toInt();
        if (total > 0) {
            percent = qBound(0, (current * 100) / total, 100);
        }
    }
    emit operationProgress(handle, percent, text.trimmed().left(300));
}

void DnfCliBackend::finishOperation(const OperationHandle &handle,
                                    int exit_code,
                                    QProcess::ExitStatus exit_status) {
    auto it = operations_.find(handle.id);
    if (it == operations_.end()) {
        return;
    }
    QProcess *process = it->process;
    const QString output =
        QString::fromUtf8(process->readAllStandardOutput() + process->readAllStandardError())
            .trimmed();
    operations_.erase(it);
    process->deleteLater();

    const bool success = exit_status == QProcess::NormalExit && exit_code == 0;
    if (!success) {
        emit operationFailed(handle,
                             output.isEmpty() ? QStringLiteral("dnf operation failed") : output);
    }
    emit operationProgress(handle, success ? 100 : -1, success ? QStringLiteral("Done") : output);
    emit operationFinished(handle, success);
}

QString DnfCliBackend::dnfExecutable() const {
    return kh::sources::FindExecutable({QStringLiteral("dnf5"), QStringLiteral("dnf")});
}

QString DnfCliBackend::privilegedExecutable() const {
    return QStandardPaths::findExecutable(QStringLiteral("pkexec"));
}

}  // namespace kh::pkg
