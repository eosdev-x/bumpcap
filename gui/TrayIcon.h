#pragma once

#include <QObject>
#include <QSystemTrayIcon>

#include "core/model/KernelInfo.h"

class QAction;
class QMenu;
class QWidget;

namespace kh::gui {

class TrayIcon : public QObject {
    Q_OBJECT

public:
    explicit TrayIcon(QWidget *main_window, QObject *parent = nullptr);

    bool isAvailable() const;
    void setKernels(const QList<kh::model::KernelInfo> &kernels);
    void show();
    void hide();

signals:
    void refreshRequested();
    void showWindowRequested();
    void quitRequested();
    void quickInstallRequested(kh::model::KernelInfo kernel);

private slots:
    void onActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void rebuildMenu();
    QIcon iconForUpdateState() const;

    QWidget *main_window_ = nullptr;
    QSystemTrayIcon *tray_icon_ = nullptr;
    QMenu *menu_ = nullptr;
    QList<kh::model::KernelInfo> kernels_;
    int update_count_ = 0;
};

}  // namespace kh::gui
