#pragma once

#include <QProcess>
#include <QUrl>

#include "core/net/NetworkClient.h"
#include "core/sources/IKernelSource.h"

namespace kh::sources {

class FedoraRawhideSource : public IKernelSource {
    Q_OBJECT

public:
    explicit FedoraRawhideSource(QObject *parent = nullptr);
    ~FedoraRawhideSource() override;

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
        RepodataDirectory,
        PrimaryXml,
    };

    void startLocalRepoquery();
    void finishRepoquery(int exit_code, QProcess::ExitStatus exit_status);
    void onNetworkFinished(int request_id, QByteArray body, int http_status_code);
    void onNetworkFailed(int request_id, QString error_message, int http_status_code);
    void decompressPrimaryXml(const QByteArray &compressed);
    void finishDecompress(int exit_code, QProcess::ExitStatus exit_status);
    QUrl primaryMetadataUrlFromDirectory(const QByteArray &directory_html) const;

    kh::net::NetworkClient network_;
    QProcess *repoquery_process_ = nullptr;
    QProcess *decompress_process_ = nullptr;
    FetchStage stage_ = FetchStage::None;
    int active_request_id_ = 0;
};

}  // namespace kh::sources
