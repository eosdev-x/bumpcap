#include "core/repo/KernelRepository.h"

#include <QHash>
#include <QSet>

#include "core/log/Log.h"

namespace kh::repo {
namespace {

QString KernelKey(const kh::model::KernelInfo &kernel) {
    return kh::model::SourceIdToString(kernel.sourceId) + QLatin1Char('|') + kernel.version;
}

QString InstalledVersionKey(const kh::model::KernelInfo &kernel) {
    return kernel.version;
}

}  // namespace

KernelRepository::KernelRepository(QObject *parent) : QObject(parent) {}

void KernelRepository::addSource(std::shared_ptr<kh::sources::IKernelSource> source) {
    if (!source) {
        return;
    }
    const kh::sources::SourceId source_id = source->id();
    QObject::connect(source.get(),
                     &kh::sources::IKernelSource::fetchFinished,
                     this,
                     [this, source_id](QList<kh::model::KernelInfo> kernels) {
                         onSourceFinished(source_id, std::move(kernels));
                     });
    QObject::connect(source.get(),
                     &kh::sources::IKernelSource::fetchFailed,
                     this,
                     [this, source_id](const QString &error) {
                         onSourceFailed(source_id, error);
                     });
    sources_.push_back(std::move(source));
}

void KernelRepository::clearSources() {
    sources_.clear();
}

void KernelRepository::setPackageBackend(kh::pkg::IPackageBackend *package_backend) {
    if (package_backend_ == package_backend) {
        return;
    }
    if (package_backend_ != nullptr) {
        package_backend_->disconnect(this);
    }
    package_backend_ = package_backend;
    if (package_backend_ == nullptr) {
        return;
    }
    QObject::connect(package_backend_,
                     &kh::pkg::IPackageBackend::installedQueryFinished,
                     this,
                     &KernelRepository::onInstalledQueryFinished);
    QObject::connect(package_backend_,
                     &kh::pkg::IPackageBackend::installedQueryFailed,
                     this,
                     &KernelRepository::onInstalledQueryFailed);
}

void KernelRepository::setStateStore(kh::state::StateStore *state_store) {
    state_store_ = state_store;
}

QList<kh::model::KernelInfo> KernelRepository::kernels() const {
    return kernels_;
}

void KernelRepository::refresh() {
    if (refresh_in_progress_) {
        qCInfo(kh::log::repo) << "Kernel refresh already in progress";
        return;
    }

    refresh_in_progress_ = true;
    pending_sources_ = sources_.size();
    pending_installed_query_ = package_backend_ != nullptr;
    available_kernels_.clear();
    installed_kernels_.clear();
    emit refreshStarted();

    if (package_backend_ != nullptr) {
        package_backend_->queryInstalled();
    }

    for (const std::shared_ptr<kh::sources::IKernelSource> &source : sources_) {
        source->fetchAvailable();
    }

    finalizeRefreshIfReady();
}

void KernelRepository::onSourceFinished(kh::sources::SourceId source_id,
                                        QList<kh::model::KernelInfo> kernels) {
    for (kh::model::KernelInfo &kernel : kernels) {
        kernel.sourceId = source_id;
        if (kernel.sourceDisplayName.isEmpty()) {
            kernel.sourceDisplayName = kh::model::SourceIdDisplayName(source_id);
        }
    }
    available_kernels_.append(kernels);
    pending_sources_ = qMax(0, pending_sources_ - 1);
    finalizeRefreshIfReady();
}

void KernelRepository::onSourceFailed(kh::sources::SourceId source_id, const QString &error) {
    qCWarning(kh::log::repo) << "Kernel source failed"
                             << kh::model::SourceIdToString(source_id) << error;
    pending_sources_ = qMax(0, pending_sources_ - 1);
    emit refreshFailed(error);
    finalizeRefreshIfReady();
}

void KernelRepository::onInstalledQueryFinished(QList<kh::model::KernelInfo> installed) {
    installed_kernels_ = std::move(installed);
    pending_installed_query_ = false;
    finalizeRefreshIfReady();
}

void KernelRepository::onInstalledQueryFailed(const QString &error) {
    qCWarning(kh::log::repo) << "Installed kernel query failed" << error;
    pending_installed_query_ = false;
    emit refreshFailed(error);
    finalizeRefreshIfReady();
}

void KernelRepository::finalizeRefreshIfReady() {
    if (!refresh_in_progress_ || pending_sources_ > 0 || pending_installed_query_) {
        return;
    }

    kernels_ = mergeKernelLists();
    refresh_in_progress_ = false;
    emit kernelListChanged(kernels_);
    emit refreshFinished();
}

QList<kh::model::KernelInfo> KernelRepository::mergeKernelLists() const {
    QHash<QString, kh::model::KernelInfo> merged;
    QSet<QString> installed_versions;
    for (const kh::model::KernelInfo &kernel : installed_kernels_) {
        installed_versions.insert(InstalledVersionKey(kernel));
    }

    for (const kh::model::KernelInfo &kernel : available_kernels_) {
        kh::model::KernelInfo merged_kernel = kernel;
        if (installed_versions.contains(InstalledVersionKey(kernel)) &&
            merged_kernel.status == kh::model::KernelStatus::Available) {
            merged_kernel.status = kh::model::KernelStatus::Installed;
        }
        merged.insert(KernelKey(merged_kernel), applyState(merged_kernel));
    }

    for (const kh::model::KernelInfo &kernel : installed_kernels_) {
        const QString key = KernelKey(kernel);
        if (!merged.contains(key)) {
            kh::model::KernelInfo installed_kernel = kernel;
            if (installed_kernel.status == kh::model::KernelStatus::Available) {
                installed_kernel.status = kh::model::KernelStatus::Installed;
            }
            merged.insert(key, applyState(installed_kernel));
        }
    }

    return merged.values();
}

kh::model::KernelInfo KernelRepository::applyState(
    const kh::model::KernelInfo &kernel) const {
    kh::model::KernelInfo updated = kernel;
    if (state_store_ != nullptr && state_store_->isOpen()) {
        updated.isPinned = state_store_->isPinned(kernel.version);
        updated.notes = state_store_->note(kernel.version);
    }
    return updated;
}

}  // namespace kh::repo

