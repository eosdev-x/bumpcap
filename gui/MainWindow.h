#pragma once

#include <QMainWindow>

#include "core/boot/IBootloaderManager.h"
#include "core/config/ConfigManager.h"
#include "core/notify/Notifier.h"
#include "core/pkg/IPackageBackend.h"
#include "core/repo/KernelRepository.h"
#include "core/state/StateStore.h"
#include "gui/models/KernelTableModel.h"

class QAction;
class QComboBox;
class QLabel;
class QSortFilterProxyModel;
class QTableView;
class QTableWidget;

namespace kh::gui {

class DetailPanel;
class ProgressDialog;
class TrayIcon;

namespace widgets {
class AnimatedRefreshButton;
class SearchFilter;
}  // namespace widgets

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(kh::repo::KernelRepository *repository,
                        kh::pkg::IPackageBackend *package_backend,
                        kh::boot::IBootloaderManager *bootloader_manager,
                        kh::config::ConfigManager *config_manager,
                        kh::state::StateStore *state_store,
                        kh::notify::Notifier *notifier,
                        QWidget *parent = nullptr);

private slots:
    void refreshKernels();
    void onKernelListChanged(QList<kh::model::KernelInfo> kernels);
    void onRefreshStarted();
    void onRefreshFinished();
    void onRefreshFailed(const QString &error);
    void updateActionState();
    void showContextMenu(const QPoint &position);
    void showSelectedKernelDetails();
    void installSelectedKernel();
    void removeSelectedKernel();
    void toggleSelectedKernelPin();
    void editSelectedKernelNote();
    void setSelectedKernelAsDefault();
    void copySelectedKernelVersion();
    void showSettings();
    void showBootOrderPanel();
    void regenerateBootConfig();
    void exportKernelList();
    void openLogFile();
    void checkCpuCompatibility();
    void showAboutDialog();
    void applyConfig();
    void applyTheme();

private:
    void createActions();
    void createMenus();
    void createToolbar();
    void createStatusBar();
    void createCentralTable();
    void createDetailPanel();
    void createTrayIcon();
    void connectCoreSignals();
    void refreshActionIcons();
    void updateStatusSummary();
    void startPackageOperation(const kh::model::KernelInfo &kernel, bool install);
    void showOperationUnavailable(const QString &operation) const;
    void setBootEntries(const QList<kh::boot::BootEntry> &entries);
    void selectKernel(const kh::model::KernelInfo &kernel);
    kh::model::KernelInfo selectedKernel() const;
    QModelIndex selectedSourceIndex() const;
    QHash<int, int> sourcePriorities() const;

    kh::repo::KernelRepository *repository_ = nullptr;
    kh::pkg::IPackageBackend *package_backend_ = nullptr;
    kh::boot::IBootloaderManager *bootloader_manager_ = nullptr;
    kh::config::ConfigManager *config_manager_ = nullptr;
    kh::state::StateStore *state_store_ = nullptr;
    kh::notify::Notifier *notifier_ = nullptr;

    KernelTableModel *table_model_ = nullptr;
    QSortFilterProxyModel *filter_model_ = nullptr;
    QTableView *table_view_ = nullptr;
    DetailPanel *detail_panel_ = nullptr;
    QTableWidget *boot_table_ = nullptr;
    QDockWidget *boot_dock_ = nullptr;
    TrayIcon *tray_icon_ = nullptr;

    widgets::SearchFilter *filter_edit_ = nullptr;
    QComboBox *quick_filter_combo_ = nullptr;
    QLabel *kernel_count_label_ = nullptr;
    QLabel *update_count_label_ = nullptr;
    QLabel *last_checked_label_ = nullptr;
    widgets::AnimatedRefreshButton *refresh_button_ = nullptr;

    QAction *refresh_action_ = nullptr;
    QAction *install_action_ = nullptr;
    QAction *remove_action_ = nullptr;
    QAction *pin_action_ = nullptr;
    QAction *settings_action_ = nullptr;
    QAction *detail_panel_action_ = nullptr;
    QAction *boot_panel_action_ = nullptr;
    QAction *compact_mode_action_ = nullptr;
    QAction *status_bar_action_ = nullptr;
    QAction *theme_light_action_ = nullptr;
    QAction *theme_dark_action_ = nullptr;
    QAction *theme_system_action_ = nullptr;
};

}  // namespace kh::gui
