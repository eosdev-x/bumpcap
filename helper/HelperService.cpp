#include "helper/HelperService.h"

#include <PolkitQt1/Authority>
#include <PolkitQt1/Subject>

#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusReply>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

Q_LOGGING_CATEGORY(helperLog, "bumpcap.helper")

namespace kh::helper {
namespace {

constexpr int kCommandTimeoutMs = 10 * 60 * 1000;
constexpr QLatin1String kManageCoprAction("org.bumpcap.manage-copr");
constexpr QLatin1String kSetDefaultKernelAction("org.bumpcap.set-default-kernel");
constexpr QLatin1String kRegenerateGrubAction("org.bumpcap.regenerate-grub");
constexpr QLatin1String kAllowedCoprOwner("bieszczaders");
constexpr QLatin1String kAllowedCoprProjectPrefix("kernel-cachyos");

QString ShellQuotedForLog(const QStringList &arguments) {
    QStringList quoted;
    quoted.reserve(arguments.size());
    for (const QString &argument : arguments) {
        QString escaped = argument;
        escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        quoted.push_back(QStringLiteral("'%1'").arg(escaped));
    }
    return quoted.join(QLatin1Char(' '));
}

}  // namespace

HelperService::HelperService(QObject *parent) : QObject(parent) {}

void HelperService::EnableCopr(const QString &project, const QString &owner) {
    const QString operation = QStringLiteral("EnableCopr");
    QString error;
    if (!validateCopr(project, owner, &error)) {
        reject(operation, error);
        return;
    }
    if (!authorize(kManageCoprAction, operation)) {
        return;
    }
    runCommand(operation, QStringLiteral("dnf5"),
               {QStringLiteral("-y"), QStringLiteral("copr"), QStringLiteral("enable"),
                QStringLiteral("%1/%2").arg(owner, project)});
}

void HelperService::DisableCopr(const QString &project, const QString &owner) {
    const QString operation = QStringLiteral("DisableCopr");
    QString error;
    if (!validateCopr(project, owner, &error)) {
        reject(operation, error);
        return;
    }
    if (!authorize(kManageCoprAction, operation)) {
        return;
    }
    runCommand(operation, QStringLiteral("dnf5"),
               {QStringLiteral("-y"), QStringLiteral("copr"), QStringLiteral("disable"),
                QStringLiteral("%1/%2").arg(owner, project)});
}

void HelperService::SetDefaultKernel(const QString &kernelPath) {
    const QString operation = QStringLiteral("SetDefaultKernel");
    QString error;
    if (!validateKernelPath(kernelPath, &error)) {
        reject(operation, error);
        return;
    }
    if (!authorize(kSetDefaultKernelAction, operation)) {
        return;
    }
    runCommand(operation, QStringLiteral("grubby"),
               {QStringLiteral("--set-default"), kernelPath});
}

void HelperService::RegenerateGrubConfig() {
    const QString operation = QStringLiteral("RegenerateGrubConfig");
    if (!authorize(kRegenerateGrubAction, operation)) {
        return;
    }
    runCommand(operation, QStringLiteral("grub2-mkconfig"),
               {QStringLiteral("-o"), QStringLiteral("/boot/grub2/grub.cfg")});
}

void HelperService::RebootIntoKernelOnce(const QString &kernelPath) {
    const QString operation = QStringLiteral("RebootIntoKernelOnce");
    QString error;
    if (!validateKernelPath(kernelPath, &error)) {
        reject(operation, error);
        return;
    }
    if (!authorize(kSetDefaultKernelAction, operation)) {
        return;
    }
    runCommand(operation, QStringLiteral("grub2-reboot"), {kernelPath});
}

bool HelperService::authorize(const QString &action_id, const QString &operation) {
    const QString caller = message().service();
    QDBusReply<uint> pid_reply = connection().interface()->servicePid(caller);
    if (!pid_reply.isValid()) {
        const QString error = QStringLiteral("Unable to identify D-Bus caller %1: %2")
                                  .arg(caller, pid_reply.error().message());
        qCWarning(helperLog) << operation << error;
        sendErrorReply(QDBusError::AccessDenied, error);
        emit Completed(operation, false, error);
        return false;
    }

    PolkitQt1::UnixProcessSubject subject(pid_reply.value());
    const PolkitQt1::Authority::Result result =
        PolkitQt1::Authority::instance()->checkAuthorizationSync(
            action_id, subject, PolkitQt1::Authority::AllowUserInteraction);
    if (result != PolkitQt1::Authority::Yes) {
        const QString error =
            QStringLiteral("Polkit authorization denied for action %1").arg(action_id);
        qCWarning(helperLog) << operation << error << "caller" << caller << "pid"
                             << pid_reply.value();
        sendErrorReply(QDBusError::AccessDenied, error);
        emit Completed(operation, false, error);
        return false;
    }

    qCInfo(helperLog) << operation << "authorized for caller" << caller << "pid"
                      << pid_reply.value() << "action" << action_id;
    return true;
}

bool HelperService::validateCopr(const QString &project,
                                 const QString &owner,
                                 QString *error) const {
    static const QRegularExpression name_pattern(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (!name_pattern.match(owner).hasMatch()) {
        *error = QStringLiteral("Invalid COPR owner. Allowed characters: A-Z, a-z, 0-9, _ and -.");
        return false;
    }
    if (!name_pattern.match(project).hasMatch()) {
        *error =
            QStringLiteral("Invalid COPR project. Allowed characters: A-Z, a-z, 0-9, _ and -.");
        return false;
    }
    if (owner != kAllowedCoprOwner) {
        *error = QStringLiteral("COPR owner '%1' is not allowed.").arg(owner);
        return false;
    }
    if (!project.startsWith(kAllowedCoprProjectPrefix)) {
        *error = QStringLiteral("COPR project '%1' is not in the Bumpcap allow-list.")
                     .arg(project);
        return false;
    }
    return true;
}

bool HelperService::validateKernelPath(const QString &kernel_path, QString *error) const {
    if (!kernel_path.startsWith(QStringLiteral("/boot/vmlinuz-"))) {
        *error = QStringLiteral("Kernel path must start with /boot/vmlinuz-.");
        return false;
    }
    if (kernel_path.contains(QStringLiteral("..")) ||
        kernel_path.contains(QLatin1Char('\0')) ||
        kernel_path.contains(QStringLiteral("//"))) {
        *error = QStringLiteral("Kernel path must not contain path traversal or empty segments.");
        return false;
    }
    static const QRegularExpression path_pattern(QStringLiteral("^/boot/vmlinuz-[A-Za-z0-9._:+-]+$"));
    if (!path_pattern.match(kernel_path).hasMatch()) {
        *error = QStringLiteral("Kernel path contains unsupported characters.");
        return false;
    }
    return true;
}

bool HelperService::runCommand(const QString &operation,
                               const QString &program,
                               const QStringList &arguments) {
    emit Progress(operation, QStringLiteral("Starting"), 0);
    qCInfo(helperLog).noquote()
        << operation << "executing" << program << ShellQuotedForLog(arguments);

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted()) {
        const QString error =
            QStringLiteral("Unable to start %1: %2").arg(program, process.errorString());
        qCWarning(helperLog) << operation << error;
        sendErrorReply(QDBusError::Failed, error);
        emit Completed(operation, false, error);
        return false;
    }

    emit Progress(operation, QStringLiteral("Running"), 50);
    if (!process.waitForFinished(kCommandTimeoutMs)) {
        process.kill();
        process.waitForFinished(5000);
        const QString error = QStringLiteral("%1 timed out").arg(program);
        qCWarning(helperLog) << operation << error;
        sendErrorReply(QDBusError::TimedOut, error);
        emit Completed(operation, false, error);
        return false;
    }

    const QString stderr_output = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    const QString stdout_output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = !stderr_output.isEmpty() ? stderr_output : stdout_output;
        const QString error = QStringLiteral("%1 failed with exit code %2%3")
                                  .arg(program)
                                  .arg(process.exitCode())
                                  .arg(detail.isEmpty() ? QString()
                                                        : QStringLiteral(": %1").arg(detail));
        qCWarning(helperLog) << operation << error;
        sendErrorReply(QDBusError::Failed, error);
        emit Completed(operation, false, error);
        return false;
    }

    const QString message =
        stdout_output.isEmpty() ? QStringLiteral("Completed successfully") : stdout_output;
    qCInfo(helperLog) << operation << "completed successfully";
    emit Progress(operation, QStringLiteral("Finished"), 100);
    emit Completed(operation, true, message);
    return true;
}

void HelperService::reject(const QString &operation, const QString &message) {
    qCWarning(helperLog) << operation << "rejected:" << message;
    sendErrorReply(QDBusError::InvalidArgs, message);
    emit Completed(operation, false, message);
}

}  // namespace kh::helper
