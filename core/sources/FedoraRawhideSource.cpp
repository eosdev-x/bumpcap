#include "core/sources/FedoraRawhideSource.h"

#include <QRegularExpression>

#include "core/log/Log.h"
#include "core/sources/KernelPackageUtils.h"

namespace kh::sources {
namespace {

constexpr char kRawhideRepodataUrl[] =
    "https://dl.fedoraproject.org/pub/fedora/linux/development/rawhide/"
    "Everything/x86_64/os/repodata/";

}  // namespace

FedoraRawhideSource::FedoraRawhideSource(QObject *parent)
    : IKernelSource(parent), network_(this) {
    network_.setBaseBackoffMilliseconds(1000);
    QObject::connect(&network_,
                     &kh::net::NetworkClient::requestFinished,
                     this,
                     &FedoraRawhideSource::onNetworkFinished);
    QObject::connect(&network_,
                     &kh::net::NetworkClient::requestFailed,
                     this,
                     &FedoraRawhideSource::onNetworkFailed);
}

FedoraRawhideSource::~FedoraRawhideSource() {
    if (repoquery_process_ != nullptr) {
        repoquery_process_->kill();
    }
    if (decompress_process_ != nullptr) {
        decompress_process_->kill();
    }
}

SourceId FedoraRawhideSource::id() const {
    return SourceId::FedoraRawhide;
}

QString FedoraRawhideSource::displayName() const {
    return kh::model::SourceIdDisplayName(id());
}

QString FedoraRawhideSource::originDescription() const {
    return IsRawhideSystem() ? QStringLiteral("Fedora Rawhide enabled repositories")
                             : QStringLiteral("Fedora Rawhide repodata (browse only)");
}

bool FedoraRawhideSource::requiresRepoSetup() const {
    return !IsRawhideSystem();
}

std::optional<CompatibilityResult> FedoraRawhideSource::checkCompatibility() const {
    return std::nullopt;
}

void FedoraRawhideSource::fetchAvailable() {
    if (IsRawhideSystem()) {
        startLocalRepoquery();
        return;
    }

    stage_ = FetchStage::RepodataDirectory;
    active_request_id_ = network_.get(QUrl(QString::fromLatin1(kRawhideRepodataUrl)));
}

void FedoraRawhideSource::ensureRepoEnabled() {
    if (IsRawhideSystem()) {
        emit repoEnableFinished(true, QString());
        return;
    }
    emit repoEnableFinished(
        false,
        QStringLiteral("Rawhide is browse-only on stable Fedora systems; Bumpcap does not "
                       "enable Rawhide repositories automatically."));
}

void FedoraRawhideSource::startLocalRepoquery() {
    const QString executable = FindExecutable({QStringLiteral("dnf5"), QStringLiteral("dnf")});
    if (executable.isEmpty()) {
        emit fetchFailed(QStringLiteral("Neither dnf5 nor dnf is available for repoquery"));
        return;
    }
    repoquery_process_ = new QProcess(this);
    repoquery_process_->setProgram(executable);
    const QString format =
        QStringLiteral("%{name}\t%{version}-%{release}.%{arch}\t%{buildtime}");
    QStringList arguments;
    if (executable.endsWith(QStringLiteral("dnf5"))) {
        arguments << QStringLiteral("repoquery") << QStringLiteral("--available")
                  << QStringLiteral("--queryformat") << format << QStringLiteral("kernel*");
    } else {
        arguments << QStringLiteral("repoquery") << QStringLiteral("--available")
                  << QStringLiteral("--qf") << format << QStringLiteral("kernel*");
    }
    repoquery_process_->setArguments(arguments);
    QObject::connect(repoquery_process_,
                     &QProcess::finished,
                     this,
                     &FedoraRawhideSource::finishRepoquery);
    repoquery_process_->start();
}

void FedoraRawhideSource::finishRepoquery(int exit_code, QProcess::ExitStatus exit_status) {
    QProcess *process = repoquery_process_;
    repoquery_process_ = nullptr;
    const QByteArray output = process->readAllStandardOutput();
    const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
    process->deleteLater();
    if (exit_status != QProcess::NormalExit || exit_code != 0) {
        emit fetchFailed(error.isEmpty() ? QStringLiteral("Rawhide repoquery failed") : error);
        return;
    }
    emit fetchFinished(ParseRepoqueryOutput(output, id(), displayName()));
}

void FedoraRawhideSource::onNetworkFinished(int request_id,
                                            QByteArray body,
                                            int http_status_code) {
    if (request_id != active_request_id_) {
        return;
    }
    if (http_status_code >= 400) {
        emit fetchFailed(QStringLiteral("Rawhide repodata request failed with HTTP %1")
                             .arg(http_status_code));
        return;
    }
    if (stage_ == FetchStage::RepodataDirectory) {
        const QUrl primary_url = primaryMetadataUrlFromDirectory(body);
        if (!primary_url.isValid()) {
            emit fetchFailed(QStringLiteral("Unable to find Rawhide primary.xml metadata"));
            return;
        }
        stage_ = FetchStage::PrimaryXml;
        active_request_id_ = network_.get(primary_url);
        return;
    }
    if (stage_ == FetchStage::PrimaryXml) {
        decompressPrimaryXml(body);
    }
}

void FedoraRawhideSource::onNetworkFailed(int request_id,
                                          QString error_message,
                                          int http_status_code) {
    if (request_id != active_request_id_) {
        return;
    }
    emit fetchFailed(QStringLiteral("Rawhide network request failed (%1): %2")
                         .arg(http_status_code)
                         .arg(error_message));
}

void FedoraRawhideSource::decompressPrimaryXml(const QByteArray &compressed) {
    const QString executable = FindExecutable({QStringLiteral("zstd"), QStringLiteral("unzstd")});
    if (executable.isEmpty()) {
        emit fetchFailed(QStringLiteral("zstd is required to read Rawhide primary.xml.zst"));
        return;
    }
    decompress_process_ = new QProcess(this);
    decompress_process_->setProgram(executable);
    decompress_process_->setArguments({QStringLiteral("-dc")});
    QObject::connect(decompress_process_,
                     &QProcess::finished,
                     this,
                     &FedoraRawhideSource::finishDecompress);
    decompress_process_->start();
    decompress_process_->write(compressed);
    decompress_process_->closeWriteChannel();
}

void FedoraRawhideSource::finishDecompress(int exit_code, QProcess::ExitStatus exit_status) {
    QProcess *process = decompress_process_;
    decompress_process_ = nullptr;
    const QByteArray xml = process->readAllStandardOutput();
    const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
    process->deleteLater();
    if (exit_status != QProcess::NormalExit || exit_code != 0) {
        emit fetchFailed(error.isEmpty() ? QStringLiteral("Unable to decompress Rawhide metadata")
                                         : error);
        return;
    }
    emit fetchFinished(ParsePrimaryXml(xml, id(), displayName()));
}

QUrl FedoraRawhideSource::primaryMetadataUrlFromDirectory(
    const QByteArray &directory_html) const {
    const QString html = QString::fromUtf8(directory_html);
    const QRegularExpression regex(QStringLiteral("href=['\"]([^'\"]+primary\\.xml\\.zst)['\"]"));
    const QRegularExpressionMatch match = regex.match(html);
    if (!match.hasMatch()) {
        return {};
    }
    return QUrl(QString::fromLatin1(kRawhideRepodataUrl)).resolved(QUrl(match.captured(1)));
}

}  // namespace kh::sources
