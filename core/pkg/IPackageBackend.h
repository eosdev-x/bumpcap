#pragma once

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include "core/model/KernelInfo.h"

namespace kh::pkg {

struct OperationHandle {
    QString id;
};

class IPackageBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IPackageBackend() override = default;

    virtual void queryInstalled() = 0;
    virtual OperationHandle installKernel(const kh::model::KernelInfo &kernel) = 0;
    virtual OperationHandle removeKernel(const kh::model::KernelInfo &kernel,
                                         bool force = false) = 0;
    virtual void cancelOperation(const OperationHandle &handle) = 0;

signals:
    void installedQueryFinished(QList<kh::model::KernelInfo> installed);
    void installedQueryFailed(QString error);
    void operationProgress(kh::pkg::OperationHandle handle,
                           int percent,
                           QString statusText);
    void operationFinished(kh::pkg::OperationHandle handle, bool success);
    void operationFailed(kh::pkg::OperationHandle handle, QString errorMessage);
};

}  // namespace kh::pkg

Q_DECLARE_METATYPE(kh::pkg::OperationHandle)

