#pragma once

#include <QDBusPendingCallWatcher>
#include <QProcess>
#include <QUrl>

#include "core/net/NetworkClient.h"
#include "core/sources/CpuFeatures.h"
#include "core/sources/IKernelSource.h"

namespace kh::sources {

class CachyOsCoprSource : public IKernelSource {
    Q_OBJECT

public:
    enum class Variant {
        Stable,
        Lts,
    };

    explicit CachyOsCoprSource(Variant variant, QObject *parent = nullptr);
    ~CachyOsCoprSource() override;

    SourceId id() const override;
    QString displayName() const override;
    QString originDescription() const override;
    bool requiresRepoSetup() const override;
    std::optional<CompatibilityResult> checkCompatibility() const override;
    void fetchAvailable() override;
    void ensureRepoEnabled() override;

private:
    enum class FetchStage {
        None,
        PackageApi,
        RepodataDirectory,
        PrimaryXml,
    };

    void onNetworkFinished(int request_id, QByteArray body, int http_status_code);
    void onNetworkFailed(int request_id, QString error_message, int http_status_code);
    void startRepodataFetch();
    QUrl repodataDirectoryUrl() const;
    QUrl primaryMetadataUrlFromDirectory(const QByteArray &directory_html) const;
    void decompressPrimaryXml(const QByteArray &compressed);
    void finishDecompress(int exit_code, QProcess::ExitStatus exit_status);
    void finishRepoEnable(int exit_code, QProcess::ExitStatus exit_status);
    void finishRepoEnableHelper(QDBusPendingCallWatcher *watcher);
    void startRepoEnableFallback();
    MicroarchitectureLevel requiredLevel() const;

    Variant variant_;
    kh::net::NetworkClient network_;
    QProcess *decompress_process_ = nullptr;
    QProcess *repo_enable_process_ = nullptr;
    bool repo_enable_helper_running_ = false;
    FetchStage stage_ = FetchStage::None;
    int active_request_id_ = 0;
};

}  // namespace kh::sources
