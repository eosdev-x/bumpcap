#pragma once

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

#include "core/model/KernelInfo.h"
#include "core/sources/CompatibilityResult.h"
#include "core/sources/SourceId.h"

namespace kh::sources {

class IKernelSource : public QObject {
    Q_OBJECT

public:
    explicit IKernelSource(QObject *parent = nullptr) : QObject(parent) {}
    ~IKernelSource() override = default;

    virtual SourceId id() const = 0;
    virtual QString displayName() const = 0;
    virtual QString originDescription() const = 0;
    virtual bool requiresRepoSetup() const = 0;
    virtual std::optional<CompatibilityResult> checkCompatibility() const = 0;
    virtual void fetchAvailable() = 0;
    virtual void ensureRepoEnabled() = 0;

signals:
    void fetchFinished(QList<kh::model::KernelInfo> kernels);
    void fetchFailed(QString errorMessage);
    void repoEnableFinished(bool success, QString errorMessage);
};

}  // namespace kh::sources

