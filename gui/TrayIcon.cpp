#include "gui/TrayIcon.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QWidget>

#include "gui/theme/Theme.h"
#include "gui/theme/ThemeManager.h"

namespace kh::gui {

TrayIcon::TrayIcon(QWidget *main_window, QObject *parent)
    : QObject(parent),
      main_window_(main_window),
      tray_icon_(new QSystemTrayIcon(this)),
      menu_(new QMenu(main_window)) {
    tray_icon_->setIcon(QIcon(QStringLiteral(":/kh/gui/icons/bumpcap.svg")));
    tray_icon_->setToolTip(QStringLiteral("Bumpcap"));
    tray_icon_->setContextMenu(menu_);
    connect(tray_icon_,
            &QSystemTrayIcon::activated,
            this,
            &TrayIcon::onActivated);
    rebuildMenu();
}

bool TrayIcon::isAvailable() const {
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayIcon::setKernels(const QList<kh::model::KernelInfo> &kernels) {
    kernels_ = kernels;
    update_count_ = 0;
    for (const kh::model::KernelInfo &kernel : kernels_) {
        if (kernel.status == kh::model::KernelStatus::UpdateAvailable) {
            ++update_count_;
        }
    }
    tray_icon_->setIcon(iconForUpdateState());
    tray_icon_->setToolTip(update_count_ > 0
                               ? QStringLiteral("Bumpcap - %1 update(s) available").arg(update_count_)
                               : QStringLiteral("Bumpcap"));
    rebuildMenu();
}

void TrayIcon::show() {
    if (isAvailable()) {
        tray_icon_->show();
    }
}

void TrayIcon::hide() {
    tray_icon_->hide();
}

void TrayIcon::onActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason != QSystemTrayIcon::Trigger) {
        return;
    }
    if (main_window_ != nullptr && main_window_->isVisible()) {
        main_window_->hide();
    } else {
        emit showWindowRequested();
    }
}

void TrayIcon::rebuildMenu() {
    menu_->clear();

    auto &theme_manager = theme::ThemeManager::instance();

    QAction *refresh = menu_->addAction(theme_manager.icon(QStringLiteral("toolbar-refresh")),
                                        QStringLiteral("Refresh"));
    connect(refresh, &QAction::triggered, this, &TrayIcon::refreshRequested);

    QAction *show_window = menu_->addAction(QStringLiteral("Show Window"));
    connect(show_window, &QAction::triggered, this, &TrayIcon::showWindowRequested);

    int quick_items = 0;
    for (const kh::model::KernelInfo &kernel : kernels_) {
        if (kernel.status != kh::model::KernelStatus::UpdateAvailable || quick_items >= 3) {
            continue;
        }
        if (quick_items == 0) {
            menu_->addSeparator();
        }
        QAction *install = menu_->addAction(
            theme_manager.icon(QStringLiteral("status-update"), QColor(theme::Colors::Warning)),
            QStringLiteral("Install %1").arg(kernel.version));
        connect(install, &QAction::triggered, this, [this, kernel]() {
            emit quickInstallRequested(kernel);
        });
        ++quick_items;
    }

    menu_->addSeparator();
    QAction *quit = menu_->addAction(QStringLiteral("Quit"));
    connect(quit, &QAction::triggered, this, &TrayIcon::quitRequested);
}

QIcon TrayIcon::iconForUpdateState() const {
    if (update_count_ > 0) {
        return theme::ThemeManager::instance().icon(
            QStringLiteral("status-update"), 22, QColor(theme::Colors::Warning));
    }
    return QIcon(QStringLiteral(":/kh/gui/icons/bumpcap.svg"));
}

}  // namespace kh::gui
