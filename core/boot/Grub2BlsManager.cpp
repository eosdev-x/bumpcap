#include "core/boot/Grub2BlsManager.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantList>

#include "core/log/Log.h"
#include "core/sources/KernelPackageUtils.h"

namespace kh::boot {

Grub2BlsManager::Grub2BlsManager(QObject *parent) : IBootloaderManager(parent) {}

Grub2BlsManager::~Grub2BlsManager() {
    if (list_process_ != nullptr) {
        list_process_->kill();
    }
    if (operation_process_ != nullptr) {
        operation_process_->kill();
    }
}

void Grub2BlsManager::listBootEntries() {
    if (list_process_ != nullptr) {
        emit bootEntriesFailed(QStringLiteral("Boot entry listing is already running"));
        return;
    }
    const QString grubby = QStandardPaths::findExecutable(QStringLiteral("grubby"));
    if (grubby.isEmpty()) {
        emit bootEntriesFailed(QStringLiteral("grubby is not installed"));
        return;
    }
    list_process_ = new QProcess(this);
    list_process_->setProgram(grubby);
    list_process_->setArguments({QStringLiteral("--info=ALL")});
    QObject::connect(list_process_,
                     &QProcess::finished,
                     this,
                     &Grub2BlsManager::finishList);
    list_process_->start();
}

void Grub2BlsManager::setDefaultEntry(const QString &entryId) {
    if (operation_process_ != nullptr || helper_operation_running_) {
        emit operationFinished(false, QStringLiteral("Bootloader operation is already running"));
        return;
    }
    if (callHelperOperation(QStringLiteral("SetDefaultKernel"), {entryId})) {
        return;
    }
    if (!validateKernelPath(entryId)) {
        emit operationFinished(false, QStringLiteral("Kernel path contains unsupported characters"));
        return;
    }
    QStringList arguments{QStringLiteral("--set-default"), entryId};
    startFallbackOperation(QStringLiteral("grubby"), arguments);
}

void Grub2BlsManager::regenerateConfig() {
    if (operation_process_ != nullptr || helper_operation_running_) {
        emit operationFinished(false, QStringLiteral("Bootloader operation is already running"));
        return;
    }
    if (callHelperOperation(QStringLiteral("RegenerateGrubConfig"), {})) {
        return;
    }
    QStringList arguments{QStringLiteral("-o"), QStringLiteral("/boot/grub2/grub.cfg")};
    startFallbackOperation(QStringLiteral("grub2-mkconfig"), arguments);
}

void Grub2BlsManager::rebootIntoEntryOnce(const QString &entryId) {
    if (operation_process_ != nullptr || helper_operation_running_) {
        emit operationFinished(false, QStringLiteral("Bootloader operation is already running"));
        return;
    }
    if (callHelperOperation(QStringLiteral("RebootIntoKernelOnce"), {entryId})) {
        return;
    }
    if (!validateKernelPath(entryId)) {
        emit operationFinished(false, QStringLiteral("Kernel path contains unsupported characters"));
        return;
    }
    QStringList arguments{entryId};
    startFallbackOperation(QStringLiteral("grub2-reboot"), arguments);
}

void Grub2BlsManager::startFallbackOperation(const QString &program_name,
                                             const QStringList &initial_arguments) {
    QStringList arguments = initial_arguments;
    const QString program = privilegedProgram(&arguments, program_name);
    operation_process_ = new QProcess(this);
    operation_process_->setProgram(program);
    operation_process_->setArguments(arguments);
    QObject::connect(operation_process_,
                     &QProcess::finished,
                     this,
                     &Grub2BlsManager::finishOperation);
    operation_process_->start();
}

void Grub2BlsManager::finishList(int exit_code, QProcess::ExitStatus exit_status) {
    QProcess *process = list_process_;
    list_process_ = nullptr;
    const QString output = QString::fromUtf8(process->readAllStandardOutput());
    const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
    process->deleteLater();
    if (exit_status != QProcess::NormalExit || exit_code != 0) {
        emit bootEntriesFailed(error.isEmpty() ? QStringLiteral("grubby --info=ALL failed")
                                               : error);
        return;
    }
    emit bootEntriesListed(parseGrubbyInfo(output));
}

void Grub2BlsManager::finishOperation(int exit_code, QProcess::ExitStatus exit_status) {
    QProcess *process = operation_process_;
    operation_process_ = nullptr;
    const QString output =
        QString::fromUtf8(process->readAllStandardOutput() + process->readAllStandardError())
            .trimmed();
    process->deleteLater();
    const bool success = exit_status == QProcess::NormalExit && exit_code == 0;
    emit operationFinished(success,
                           success ? QString()
                                   : (output.isEmpty()
                                          ? QStringLiteral("Bootloader operation failed")
                                          : output));
}

void Grub2BlsManager::finishHelperOperation(QDBusPendingCallWatcher *watcher) {
    helper_operation_running_ = false;
    const QDBusPendingReply<void> reply = *watcher;
    watcher->deleteLater();
    if (reply.isError()) {
        emit operationFinished(false, reply.error().message());
        return;
    }
    emit operationFinished(true, QString());
}

bool Grub2BlsManager::callHelperOperation(const QString &method,
                                          const QVariantList &arguments) {
    QDBusInterface iface(QStringLiteral("org.bumpcap.Helper1"),
                         QStringLiteral("/org/bumpcap/Helper1"),
                         QStringLiteral("org.bumpcap.Helper1"),
                         QDBusConnection::systemBus());
    if (!iface.isValid()) {
        return false;
    }

    helper_operation_running_ = true;
    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(iface.asyncCallWithArgumentList(method, arguments), this);
    QObject::connect(watcher,
                     &QDBusPendingCallWatcher::finished,
                     this,
                     &Grub2BlsManager::finishHelperOperation);
    return true;
}

bool Grub2BlsManager::validateKernelPath(const QString &kernel_path) const {
    static const QRegularExpression path_pattern(
        QStringLiteral("^/boot/vmlinuz-[A-Za-z0-9._:+-]+$"));
    return path_pattern.match(kernel_path).hasMatch();
}

QList<BootEntry> Grub2BlsManager::parseGrubbyInfo(const QString &output) const {
    QList<BootEntry> entries;
    BootEntry current;
    const QString running = kh::sources::RunningKernelVersion();
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("index="))) {
            if (!current.entryId.isEmpty() || !current.title.isEmpty()) {
                entries.push_back(current);
            }
            current = {};
            current.bootOrderIndex = line.section(QLatin1Char('='), 1).toInt();
        } else if (line.startsWith(QStringLiteral("kernel="))) {
            QString kernel_path = line.section(QLatin1Char('='), 1);
            kernel_path.remove(QLatin1Char('"'));
            current.entryId = kernel_path;
            current.kernelVersion = kernel_path.section(QLatin1Char('/'), -1);
            if (current.kernelVersion.startsWith(QStringLiteral("vmlinuz-"))) {
                current.kernelVersion.remove(0, QStringLiteral("vmlinuz-").size());
            }
            current.isCurrentlyRunning = !running.isEmpty() && current.kernelVersion == running;
        } else if (line.startsWith(QStringLiteral("title="))) {
            current.title = line.section(QLatin1Char('='), 1);
            current.title.remove(QLatin1Char('"'));
        } else if (line.startsWith(QStringLiteral("id="))) {
            QString id = line.section(QLatin1Char('='), 1);
            id.remove(QLatin1Char('"'));
            if (!id.isEmpty()) {
                current.entryId = id;
            }
        } else if (line.startsWith(QStringLiteral("default="))) {
            current.isDefault = line.section(QLatin1Char('='), 1).trimmed() ==
                                QLatin1String("true");
        }
    }
    if (!current.entryId.isEmpty() || !current.title.isEmpty()) {
        entries.push_back(current);
    }
    return entries;
}

QString Grub2BlsManager::privilegedProgram(QStringList *arguments,
                                           const QString &program) const {
    const QString program_path = QStandardPaths::findExecutable(program);
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (program_path.isEmpty()) {
        return program;
    }
    if (pkexec.isEmpty()) {
        return program_path;
    }
    arguments->prepend(program_path);
    return pkexec;
}

}  // namespace kh::boot
