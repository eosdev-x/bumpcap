#pragma once

#include <QHash>
#include <QProcess>

#include "core/pkg/IPackageBackend.h"

namespace kh::pkg {

class DnfCliBackend : public IPackageBackend {
    Q_OBJECT

public:
    explicit DnfCliBackend(QObject *parent = nullptr);
    ~DnfCliBackend() override;

    void queryInstalled() override;
    OperationHandle installKernel(const kh::model::KernelInfo &kernel) override;
    OperationHandle removeKernel(const kh::model::KernelInfo &kernel, bool force = false) override;
    void cancelOperation(const OperationHandle &handle) override;

private:
    struct OperationState {
        OperationHandle handle;
        QProcess *process = nullptr;
        QString action;
    };

    OperationHandle startDnfOperation(const QString &action,
                                      const OperationHandle &handle,
                                      const kh::model::KernelInfo &kernel,
                                      const QStringList &arguments);
    QStringList packageSpecsForKernel(const kh::model::KernelInfo &kernel) const;
    bool removalBlocked(const kh::model::KernelInfo &kernel, bool force, QString *reason) const;
    void handleReadyRead(const OperationHandle &handle);
    void finishOperation(const OperationHandle &handle,
                         int exit_code,
                         QProcess::ExitStatus exit_status);
    QString dnfExecutable() const;
    QString privilegedExecutable() const;

    QProcess *query_process_ = nullptr;
    QHash<QString, OperationState> operations_;
    int next_operation_id_ = 1;
};

}  // namespace kh::pkg
