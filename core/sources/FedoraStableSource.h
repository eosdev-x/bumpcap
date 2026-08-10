#pragma once

#include <QProcess>

#include "core/sources/IKernelSource.h"

namespace kh::sources {

class FedoraStableSource : public IKernelSource {
    Q_OBJECT

public:
    explicit FedoraStableSource(QObject *parent = nullptr);
    ~FedoraStableSource() override;

    SourceId id() const override;
    QString displayName() const override;
    QString originDescription() const override;
    bool requiresRepoSetup() const override;
    std::optional<CompatibilityResult> checkCompatibility() const override;
    void fetchAvailable() override;
    void ensureRepoEnabled() override;

private:
    void finishRepoquery(int exit_code, QProcess::ExitStatus exit_status);
    QString dnfExecutable() const;

    QProcess *repoquery_process_ = nullptr;
};

}  // namespace kh::sources
