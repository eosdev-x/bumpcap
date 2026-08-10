#pragma once

#include <QDBusPendingCallWatcher>
#include <QProcess>

#include "core/boot/IBootloaderManager.h"

namespace kh::boot {

class Grub2BlsManager : public IBootloaderManager {
    Q_OBJECT

public:
    explicit Grub2BlsManager(QObject *parent = nullptr);
    ~Grub2BlsManager() override;

    void listBootEntries() override;
    void setDefaultEntry(const QString &entryId) override;
    void regenerateConfig() override;
    void rebootIntoEntryOnce(const QString &entryId) override;

private:
    void finishList(int exit_code, QProcess::ExitStatus exit_status);
    void finishOperation(int exit_code, QProcess::ExitStatus exit_status);
    void finishHelperOperation(QDBusPendingCallWatcher *watcher);
    bool callHelperOperation(const QString &method, const QVariantList &arguments);
    bool validateKernelPath(const QString &kernel_path) const;
    void startFallbackOperation(const QString &program, const QStringList &arguments);
    QList<BootEntry> parseGrubbyInfo(const QString &output) const;
    QString privilegedProgram(QStringList *arguments, const QString &program) const;

    QProcess *list_process_ = nullptr;
    QProcess *operation_process_ = nullptr;
    bool helper_operation_running_ = false;
};

}  // namespace kh::boot
