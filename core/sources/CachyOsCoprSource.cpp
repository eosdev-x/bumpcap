#include "core/sources/CachyOsCoprSource.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "core/log/Log.h"
#include "core/sources/CpuFeatures.h"
#include "core/sources/KernelPackageUtils.h"

namespace kh::sources {
namespace {

constexpr char kCoprApiUrl[] =
    "https://copr.fedorainfracloud.org/api_3/package/list?ownername=bieszczaders&"
    "projectname=kernel-cachyos";

}  // namespace

CachyOsCoprSource::CachyOsCoprSource(Variant variant, QObject *parent)
    : IKernelSource(parent), variant_(variant), network_(this) {
    network_.setBaseBackoffMilliseconds(1000);
    QObject::connect(&network_,
                     &kh::net::NetworkClient::requestFinished,
                     this,
                     &CachyOsCoprSource::onNetworkFinished);
    QObject::connect(&network_,
                     &kh::net::NetworkClient::requestFailed,
                     this,
                     &CachyOsCoprSource::onNetworkFailed);
}

CachyOsCoprSource::~CachyOsCoprSource() {
    if (decompress_process_ != nullptr) {
        decompress_process_->kill();
    }
    if (repo_enable_process_ != nullptr) {
        repo_enable_process_->kill();
    }
}

SourceId CachyOsCoprSource::id() const {
    return variant_ == Variant::Lts ? SourceId::CachyOsLts : SourceId::CachyOsStable;
}

QString CachyOsCoprSource::displayName() const {
    return kh::model::SourceIdDisplayName(id());
}

QString CachyOsCoprSource::originDescription() const {
    return QStringLiteral("COPR: bieszczaders/kernel-cachyos");
}

bool CachyOsCoprSource::requiresRepoSetup() const {
    return true;
}

std::optional<CompatibilityResult> CachyOsCoprSource::checkCompatibility() const {
    const CpuFeatures features = DetectHostCpuFeatures();
    const MicroarchitectureLevel required = requiredLevel();
    if (SupportsLevel(features, required)) {
        return CompatibilityResult{true, QString()};
    }
    return CompatibilityResult{
        false,
        QStringLiteral("%1 requires %2 CPU support; detected %3.")
            .arg(displayName(),
                 MicroarchitectureLevelToString(required),
                 MicroarchitectureLevelToString(features.level))};
}

void CachyOsCoprSource::fetchAvailable() {
    stage_ = FetchStage::PackageApi;
    active_request_id_ = network_.get(QUrl(QString::fromLatin1(kCoprApiUrl)));
}

void CachyOsCoprSource::ensureRepoEnabled() {
    if (repo_enable_process_ != nullptr || repo_enable_helper_running_) {
        emit repoEnableFinished(false, QStringLiteral("COPR enable is already running"));
        return;
    }
    QDBusInterface iface(QStringLiteral("org.bumpcap.Helper1"),
                         QStringLiteral("/org/bumpcap/Helper1"),
                         QStringLiteral("org.bumpcap.Helper1"),
                         QDBusConnection::systemBus());
    if (iface.isValid()) {
        repo_enable_helper_running_ = true;
        QDBusPendingCallWatcher *watcher =
            new QDBusPendingCallWatcher(iface.asyncCall(QStringLiteral("EnableCopr"),
                                                        QStringLiteral("kernel-cachyos"),
                                                        QStringLiteral("bieszczaders")),
                                        this);
        QObject::connect(watcher,
                         &QDBusPendingCallWatcher::finished,
                         this,
                         &CachyOsCoprSource::finishRepoEnableHelper);
        return;
    }
    startRepoEnableFallback();
}

void CachyOsCoprSource::startRepoEnableFallback() {
    const QString dnf = FindExecutable({QStringLiteral("dnf5"), QStringLiteral("dnf")});
    if (dnf.isEmpty()) {
        emit repoEnableFinished(false, QStringLiteral("Neither dnf5 nor dnf is available"));
        return;
    }
    const QString pkexec = FindExecutable({QStringLiteral("pkexec")});
    repo_enable_process_ = new QProcess(this);
    if (pkexec.isEmpty()) {
        repo_enable_process_->setProgram(dnf);
        repo_enable_process_->setArguments({QStringLiteral("copr"),
                                            QStringLiteral("enable"),
                                            QStringLiteral("bieszczaders/kernel-cachyos"),
                                            QStringLiteral("-y")});
    } else {
        repo_enable_process_->setProgram(pkexec);
        repo_enable_process_->setArguments({dnf,
                                            QStringLiteral("copr"),
                                            QStringLiteral("enable"),
                                            QStringLiteral("bieszczaders/kernel-cachyos"),
                                            QStringLiteral("-y")});
    }
    QObject::connect(repo_enable_process_,
                     &QProcess::finished,
                     this,
                     &CachyOsCoprSource::finishRepoEnable);
    repo_enable_process_->start();
}

void CachyOsCoprSource::onNetworkFinished(int request_id,
                                          QByteArray body,
                                          int http_status_code) {
    if (request_id != active_request_id_) {
        return;
    }
    if (http_status_code >= 400) {
        emit fetchFailed(QStringLiteral("CachyOS COPR request failed with HTTP %1")
                             .arg(http_status_code));
        return;
    }
    if (stage_ == FetchStage::PackageApi) {
        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (!document.isObject()) {
            emit fetchFailed(QStringLiteral("CachyOS COPR API returned invalid JSON"));
            return;
        }
        startRepodataFetch();
        return;
    }
    if (stage_ == FetchStage::RepodataDirectory) {
        const QUrl primary_url = primaryMetadataUrlFromDirectory(body);
        if (!primary_url.isValid()) {
            emit fetchFailed(QStringLiteral("Unable to find CachyOS primary.xml metadata"));
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

void CachyOsCoprSource::onNetworkFailed(int request_id,
                                        QString error_message,
                                        int http_status_code) {
    if (request_id != active_request_id_) {
        return;
    }
    emit fetchFailed(QStringLiteral("CachyOS COPR network request failed (%1): %2")
                         .arg(http_status_code)
                         .arg(error_message));
}

void CachyOsCoprSource::startRepodataFetch() {
    stage_ = FetchStage::RepodataDirectory;
    active_request_id_ = network_.get(repodataDirectoryUrl());
}

QUrl CachyOsCoprSource::repodataDirectoryUrl() const {
    QString release_version = SystemReleaseVersion();
    if (release_version.isEmpty()) {
        release_version = QStringLiteral("rawhide");
    }
    return QUrl(QStringLiteral(
                    "https://download.copr.fedorainfracloud.org/results/bieszczaders/"
                    "kernel-cachyos/fedora-%1-x86_64/repodata/")
                    .arg(release_version));
}

QUrl CachyOsCoprSource::primaryMetadataUrlFromDirectory(
    const QByteArray &directory_html) const {
    const QString html = QString::fromUtf8(directory_html);
    const QRegularExpression regex(QStringLiteral("href=['\"]([^'\"]+primary\\.xml\\.gz)['\"]"));
    const QRegularExpressionMatch match = regex.match(html);
    if (!match.hasMatch()) {
        return {};
    }
    return repodataDirectoryUrl().resolved(QUrl(match.captured(1)));
}

void CachyOsCoprSource::decompressPrimaryXml(const QByteArray &compressed) {
    const QString executable = FindExecutable({QStringLiteral("gzip")});
    if (executable.isEmpty()) {
        emit fetchFailed(QStringLiteral("gzip is required to read CachyOS primary.xml.gz"));
        return;
    }
    decompress_process_ = new QProcess(this);
    decompress_process_->setProgram(executable);
    decompress_process_->setArguments({QStringLiteral("-dc")});
    QObject::connect(decompress_process_,
                     &QProcess::finished,
                     this,
                     &CachyOsCoprSource::finishDecompress);
    decompress_process_->start();
    decompress_process_->write(compressed);
    decompress_process_->closeWriteChannel();
}

void CachyOsCoprSource::finishDecompress(int exit_code, QProcess::ExitStatus exit_status) {
    QProcess *process = decompress_process_;
    decompress_process_ = nullptr;
    const QByteArray xml = process->readAllStandardOutput();
    const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
    process->deleteLater();
    if (exit_status != QProcess::NormalExit || exit_code != 0) {
        emit fetchFailed(error.isEmpty() ? QStringLiteral("Unable to decompress CachyOS metadata")
                                         : error);
        return;
    }
    QList<kh::model::KernelInfo> kernels = ParsePrimaryXml(xml, id(), displayName());
    const std::optional<CompatibilityResult> compatibility = checkCompatibility();
    for (kh::model::KernelInfo &kernel : kernels) {
        kernel.compatibility = compatibility;
    }
    emit fetchFinished(kernels);
}

void CachyOsCoprSource::finishRepoEnable(int exit_code, QProcess::ExitStatus exit_status) {
    QProcess *process = repo_enable_process_;
    repo_enable_process_ = nullptr;
    const QString output =
        QString::fromUtf8(process->readAllStandardOutput() + process->readAllStandardError())
            .trimmed();
    process->deleteLater();
    if (exit_status != QProcess::NormalExit || exit_code != 0) {
        emit repoEnableFinished(false,
                                output.isEmpty() ? QStringLiteral("Unable to enable COPR repo")
                                                 : output);
        return;
    }
    emit repoEnableFinished(true, QString());
}

void CachyOsCoprSource::finishRepoEnableHelper(QDBusPendingCallWatcher *watcher) {
    repo_enable_helper_running_ = false;
    const QDBusPendingReply<void> reply = *watcher;
    watcher->deleteLater();
    if (reply.isError()) {
        emit repoEnableFinished(false, reply.error().message());
        return;
    }
    emit repoEnableFinished(true, QString());
}

MicroarchitectureLevel CachyOsCoprSource::requiredLevel() const {
    return variant_ == Variant::Lts ? MicroarchitectureLevel::X86_64V2
                                    : MicroarchitectureLevel::X86_64V3;
}

}  // namespace kh::sources
