#pragma once

#include <QDBusContext>
#include <QObject>
#include <QString>

namespace kh::helper {

class HelperService final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bumpcap.Helper1")

public:
    explicit HelperService(QObject *parent = nullptr);
    ~HelperService() override = default;

public slots:
    void EnableCopr(const QString &project, const QString &owner);
    void DisableCopr(const QString &project, const QString &owner);
    void SetDefaultKernel(const QString &kernelPath);
    void RegenerateGrubConfig();
    void RebootIntoKernelOnce(const QString &kernelPath);

signals:
    void Progress(QString operation, QString message, int percent);
    void Completed(QString operation, bool success, QString message);

private:
    bool authorize(const QString &action_id, const QString &operation);
    bool validateCopr(const QString &project, const QString &owner, QString *error) const;
    bool validateKernelPath(const QString &kernel_path, QString *error) const;
    bool runCommand(const QString &operation,
                    const QString &program,
                    const QStringList &arguments);
    void reject(const QString &operation, const QString &message);
};

}  // namespace kh::helper
