#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

#include "core/model/KernelInfo.h"
#include "core/pkg/IPackageBackend.h"
#include "core/sources/IKernelSource.h"
#include "core/state/StateStore.h"

namespace kh::repo {

class KernelRepository : public QObject {
    Q_OBJECT

public:
    explicit KernelRepository(QObject *parent = nullptr);

    void addSource(std::shared_ptr<kh::sources::IKernelSource> source);
    void clearSources();
    void setPackageBackend(kh::pkg::IPackageBackend *package_backend);
    void setStateStore(kh::state::StateStore *state_store);

    QList<kh::model::KernelInfo> kernels() const;

public slots:
    void refresh();

signals:
    void refreshStarted();
    void kernelListChanged(QList<kh::model::KernelInfo> kernels);
    void refreshFailed(QString errorMessage);
    void refreshFinished();

private:
    void onSourceFinished(kh::sources::SourceId source_id,
                          QList<kh::model::KernelInfo> kernels);
    void onSourceFailed(kh::sources::SourceId source_id, const QString &error);
    void onInstalledQueryFinished(QList<kh::model::KernelInfo> installed);
    void onInstalledQueryFailed(const QString &error);
    void finalizeRefreshIfReady();
    QList<kh::model::KernelInfo> mergeKernelLists() const;
    kh::model::KernelInfo applyState(const kh::model::KernelInfo &kernel) const;

    QList<std::shared_ptr<kh::sources::IKernelSource>> sources_;
    kh::pkg::IPackageBackend *package_backend_ = nullptr;
    kh::state::StateStore *state_store_ = nullptr;

    QList<kh::model::KernelInfo> kernels_;
    QList<kh::model::KernelInfo> available_kernels_;
    QList<kh::model::KernelInfo> installed_kernels_;
    int pending_sources_ = 0;
    bool pending_installed_query_ = false;
    bool refresh_in_progress_ = false;
};

}  // namespace kh::repo

