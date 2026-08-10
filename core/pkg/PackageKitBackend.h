#pragma once

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QHash>
#include <QStringList>

#include "core/pkg/IPackageBackend.h"

namespace kh::pkg {

class PackageKitBackend : public IPackageBackend, protected QDBusContext {
    Q_OBJECT

public:
    explicit PackageKitBackend(QObject *parent = nullptr);
    ~PackageKitBackend() override = default;

    void queryInstalled() override;
    OperationHandle installKernel(const kh::model::KernelInfo &kernel) override;
    OperationHandle removeKernel(const kh::model::KernelInfo &kernel, bool force = false) override;
    void cancelOperation(const OperationHandle &handle) override;

private slots:
    void onTransactionPackage(uint info, const QString &package_id, const QString &summary);
    void onTransactionPercentage(uint percentage);
    void onTransactionStatus(uint status);
    void onTransactionError(uint code, const QString &details);
    void onTransactionFinished(uint exit_code, uint runtime);

private:
    enum class Role {
        QueryInstalled,
        ResolveForInstall,
        ResolveForRemove,
        Install,
        Remove,
    };

    struct TransactionState {
        OperationHandle handle;
        Role role = Role::QueryInstalled;
        kh::model::KernelInfo kernel;
        QStringList packageSpecs;
        QStringList packageIds;
        QList<kh::model::KernelInfo> queryResults;
        QString error;
    };

    void createTransaction(Role role,
                           const OperationHandle &handle,
                           const kh::model::KernelInfo &kernel,
                           const QStringList &package_specs);
    void callResolve(const QDBusObjectPath &transaction_path, const TransactionState &state);
    void callInstall(const TransactionState &state);
    void callRemove(const TransactionState &state);
    void connectTransactionSignals(const QString &path);
    void disconnectTransactionSignals(const QString &path);
    QString transactionPathFromMessage() const;
    QStringList packageSpecsForKernel(const kh::model::KernelInfo &kernel) const;
    bool removalBlocked(const kh::model::KernelInfo &kernel, bool force, QString *reason) const;
    OperationHandle nextHandle();
    void failOperation(const OperationHandle &handle, const QString &error);

    QHash<QString, TransactionState> transactions_;
    int next_operation_id_ = 1;
};

}  // namespace kh::pkg
