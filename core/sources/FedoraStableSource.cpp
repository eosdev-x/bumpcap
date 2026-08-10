#include "core/sources/FedoraStableSource.h"

#include "core/log/Log.h"
#include "core/sources/KernelPackageUtils.h"

namespace kh::sources {

FedoraStableSource::FedoraStableSource(QObject *parent) : IKernelSource(parent) {}

FedoraStableSource::~FedoraStableSource() {
    if (repoquery_process_ != nullptr) {
        repoquery_process_->kill();
    }
}

SourceId FedoraStableSource::id() const {
    return SourceId::FedoraStable;
}

QString FedoraStableSource::displayName() const {
    return kh::model::SourceIdDisplayName(id());
}

QString FedoraStableSource::originDescription() const {
    return QStringLiteral("Fedora enabled repositories");
}

bool FedoraStableSource::requiresRepoSetup() const {
    return false;
}

std::optional<CompatibilityResult> FedoraStableSource::checkCompatibility() const {
    return std::nullopt;
}

void FedoraStableSource::fetchAvailable() {
    if (repoquery_process_ != nullptr) {
        emit fetchFailed(QStringLiteral("Fedora stable refresh is already running"));
        return;
    }

    const QString executable = dnfExecutable();
    if (executable.isEmpty()) {
        emit fetchFailed(QStringLiteral("Neither dnf5 nor dnf is available for repoquery"));
        return;
    }

    QStringList arguments;
    if (executable.endsWith(QStringLiteral("dnf5"))) {
        arguments << QStringLiteral("repoquery") << QStringLiteral("--available")
                  << QStringLiteral("--assumeyes")
                  << QStringLiteral("--queryformat")
                  << QStringLiteral("%{name}\t%{version}-%{release}.%{arch}\t%{buildtime}")
                  << QStringLiteral("kernel*");
    } else {
        arguments << QStringLiteral("repoquery") << QStringLiteral("--available")
                  << QStringLiteral("--assumeyes")
                  << QStringLiteral("--qf")
                  << QStringLiteral("%{name}\t%{version}-%{release}.%{arch}\t%{buildtime}")
                  << QStringLiteral("kernel*");
    }

    repoquery_process_ = new QProcess(this);
    repoquery_process_->setProgram(executable);
    repoquery_process_->setArguments(arguments);
    QObject::connect(repoquery_process_,
                     &QProcess::finished,
                     this,
                     &FedoraStableSource::finishRepoquery);
    repoquery_process_->start();
}

void FedoraStableSource::ensureRepoEnabled() {
    emit repoEnableFinished(true, QString());
}

void FedoraStableSource::finishRepoquery(int exit_code, QProcess::ExitStatus exit_status) {
    QProcess *process = repoquery_process_;
    repoquery_process_ = nullptr;
    const QByteArray output = process->readAllStandardOutput();
    const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
    process->deleteLater();

    if (exit_status != QProcess::NormalExit || exit_code != 0) {
        const QString message = error.isEmpty() ? QStringLiteral("dnf repoquery failed")
                                                : error;
        qCWarning(kh::log::sources) << message;
        emit fetchFailed(message);
        return;
    }

    emit fetchFinished(ParseRepoqueryOutput(output, id(), displayName()));
}

QString FedoraStableSource::dnfExecutable() const {
    return FindExecutable({QStringLiteral("dnf5"), QStringLiteral("dnf")});
}

}  // namespace kh::sources
