#include "gui/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QTableWidget>
#include <QTextStream>
#include <QTime>
#include <QToolBar>
#include <QUrl>

#include <memory>

#include "core/model/SourceId.h"
#include "core/sources/CpuFeatures.h"
#include "gui/DetailPanel.h"
#include "gui/ProgressDialog.h"
#include "gui/SettingsDialog.h"
#include "gui/TrayIcon.h"
#include "gui/models/KernelBadgeDelegate.h"
#include "gui/theme/ThemeManager.h"
#include "gui/widgets/AnimatedRefreshButton.h"
#include "gui/widgets/SearchFilter.h"

namespace kh::gui {
namespace {

class KernelFilterProxyModel : public QSortFilterProxyModel {
public:
    enum QuickFilter {
        kAll = 0,
        kInstalled,
        kUpdates,
        kPinned,
    };

    explicit KernelFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    void setQuickFilter(QuickFilter quick_filter) {
        quick_filter_ = quick_filter;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override {
        const QModelIndex source_index = sourceModel()->index(source_row,
                                                             KernelTableModel::kSourceColumn,
                                                             source_parent);
        const kh::model::KernelInfo kernel =
            source_index.data(Qt::UserRole).value<kh::model::KernelInfo>();

        switch (quick_filter_) {
        case kInstalled:
            if (kernel.status != kh::model::KernelStatus::Installed &&
                kernel.status != kh::model::KernelStatus::InstalledRunning) {
                return false;
            }
            break;
        case kUpdates:
            if (kernel.status != kh::model::KernelStatus::UpdateAvailable) {
                return false;
            }
            break;
        case kPinned:
            if (!kernel.isPinned) {
                return false;
            }
            break;
        case kAll:
            break;
        }

        const QString needle = filterRegularExpression().pattern();
        if (needle.isEmpty()) {
            return true;
        }
        const QString haystack =
            QStringLiteral("%1 %2 %3 %4")
                .arg(kernel.sourceDisplayName,
                     kernel.version,
                     kernel.shortVersion,
                     kernel.notes);
        return haystack.contains(needle, Qt::CaseInsensitive);
    }

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
        const kh::model::KernelInfo left_kernel =
            left.siblingAtColumn(KernelTableModel::kSourceColumn)
                .data(Qt::UserRole)
                .value<kh::model::KernelInfo>();
        const kh::model::KernelInfo right_kernel =
            right.siblingAtColumn(KernelTableModel::kSourceColumn)
                .data(Qt::UserRole)
                .value<kh::model::KernelInfo>();

        if (left.column() == KernelTableModel::kReleasedColumn) {
            return left_kernel.releaseDate < right_kernel.releaseDate;
        }
        if (left.column() == KernelTableModel::kStatusColumn) {
            return left.data(Qt::UserRole + 1).toInt() < right.data(Qt::UserRole + 1).toInt();
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }

private:
    QuickFilter quick_filter_ = kAll;
};

bool IsInstalled(kh::model::KernelStatus status) {
    return status == kh::model::KernelStatus::Installed ||
           status == kh::model::KernelStatus::InstalledRunning;
}

QString CsvEscape(QString value) {
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

}  // namespace

MainWindow::MainWindow(kh::repo::KernelRepository *repository,
                       kh::pkg::IPackageBackend *package_backend,
                       kh::boot::IBootloaderManager *bootloader_manager,
                       kh::config::ConfigManager *config_manager,
                       kh::state::StateStore *state_store,
                       kh::notify::Notifier *notifier,
                       QWidget *parent)
    : QMainWindow(parent),
      repository_(repository),
      package_backend_(package_backend),
      bootloader_manager_(bootloader_manager),
      config_manager_(config_manager),
      state_store_(state_store),
      notifier_(notifier) {
    setWindowTitle(QStringLiteral("Bumpcap"));
    setWindowIcon(QIcon(QStringLiteral(":/kh/gui/icons/bumpcap.svg")));

    createActions();
    createMenus();
    createToolbar();
    createStatusBar();
    createCentralTable();
    createDetailPanel();
    createTrayIcon();
    connectCoreSignals();
    connect(&theme::ThemeManager::instance(),
            &theme::ThemeManager::themeChanged,
            this,
            &MainWindow::applyTheme);
    applyConfig();
    applyTheme();
    updateActionState();
}

void MainWindow::refreshKernels() {
    if (repository_ == nullptr) {
        showOperationUnavailable(QStringLiteral("Refresh"));
        return;
    }
    repository_->refresh();
}

void MainWindow::onKernelListChanged(QList<kh::model::KernelInfo> kernels) {
    table_model_->setSourcePriorities(sourcePriorities());
    table_model_->setKernels(std::move(kernels));
    if (tray_icon_ != nullptr) {
        tray_icon_->setKernels(table_model_->kernels());
    }
    updateStatusSummary();
    updateActionState();
}

void MainWindow::onRefreshStarted() {
    statusBar()->showMessage(QStringLiteral("Refreshing kernel metadata..."));
    refresh_action_->setEnabled(false);
    if (refresh_button_ != nullptr) {
        refresh_button_->startSpinning();
    }
}

void MainWindow::onRefreshFinished() {
    refresh_action_->setEnabled(true);
    if (refresh_button_ != nullptr) {
        refresh_button_->stopSpinning();
    }
    last_checked_label_->setText(
        QStringLiteral("Last checked %1").arg(QTime::currentTime().toString(QStringLiteral("hh:mm"))));
    statusBar()->showMessage(QStringLiteral("Refresh complete"), 4000);
    updateStatusSummary();
}

void MainWindow::onRefreshFailed(const QString &error) {
    statusBar()->showMessage(QStringLiteral("Refresh warning: %1").arg(error), 8000);
}

void MainWindow::updateActionState() {
    const kh::model::KernelInfo kernel = selectedKernel();
    const bool has_kernel = !kernel.version.isEmpty();
    install_action_->setEnabled(has_kernel && !IsInstalled(kernel.status));
    remove_action_->setEnabled(has_kernel && IsInstalled(kernel.status) &&
                               kernel.status != kh::model::KernelStatus::InstalledRunning);
    pin_action_->setEnabled(has_kernel);
    pin_action_->setText(kernel.isPinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"));
}

void MainWindow::showContextMenu(const QPoint &position) {
    const QModelIndex index = table_view_->indexAt(position);
    if (index.isValid()) {
        table_view_->selectionModel()->select(index,
                                              QItemSelectionModel::ClearAndSelect |
                                                  QItemSelectionModel::Rows);
    }
    const kh::model::KernelInfo kernel = selectedKernel();
    if (kernel.version.isEmpty()) {
        return;
    }

    QMenu menu(this);
    menu.addAction(install_action_);
    menu.addAction(remove_action_);
    menu.addAction(pin_action_);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Edit Note"), this, &MainWindow::editSelectedKernelNote);
    menu.addAction(QStringLiteral("Set as Default"), this, &MainWindow::setSelectedKernelAsDefault);
    menu.addAction(QStringLiteral("View Changelog"), this, &MainWindow::showSelectedKernelDetails);
    menu.addAction(QStringLiteral("Copy Version"), this, &MainWindow::copySelectedKernelVersion);
    menu.exec(table_view_->viewport()->mapToGlobal(position));
}

void MainWindow::showSelectedKernelDetails() {
    const kh::model::KernelInfo kernel = selectedKernel();
    if (kernel.version.isEmpty()) {
        return;
    }
    detail_panel_->setKernel(kernel);
    detail_panel_->show();
    detail_panel_->raise();
}

void MainWindow::installSelectedKernel() {
    startPackageOperation(selectedKernel(), true);
}

void MainWindow::removeSelectedKernel() {
    startPackageOperation(selectedKernel(), false);
}

void MainWindow::toggleSelectedKernelPin() {
    const kh::model::KernelInfo kernel = selectedKernel();
    if (kernel.version.isEmpty()) {
        return;
    }
    const bool pin = !kernel.isPinned;
    if (state_store_ != nullptr && state_store_->isOpen()) {
        if (pin) {
            state_store_->setPinned(kernel.version, kernel.sourceId);
        } else {
            state_store_->removePin(kernel.version);
        }
    }
    table_model_->setKernelPinned(kernel.version, pin);
    updateActionState();
    updateStatusSummary();
}

void MainWindow::editSelectedKernelNote() {
    const kh::model::KernelInfo kernel = selectedKernel();
    if (kernel.version.isEmpty()) {
        return;
    }
    bool accepted = false;
    const QString note = QInputDialog::getMultiLineText(this,
                                                        QStringLiteral("Edit Note"),
                                                        kernel.version,
                                                        kernel.notes,
                                                        &accepted);
    if (!accepted) {
        return;
    }
    if (state_store_ != nullptr && state_store_->isOpen()) {
        state_store_->setNote(kernel.version, note);
    }
    table_model_->setKernelNote(kernel.version, note);
    detail_panel_->setKernel(selectedKernel());
}

void MainWindow::setSelectedKernelAsDefault() {
    const kh::model::KernelInfo kernel = selectedKernel();
    if (kernel.version.isEmpty()) {
        return;
    }
    if (bootloader_manager_ == nullptr) {
        showOperationUnavailable(QStringLiteral("Bootloader management"));
        return;
    }
    if (QMessageBox::question(this,
                              QStringLiteral("Set Default Kernel"),
                              QStringLiteral("Set %1 as the default boot entry?").arg(kernel.version)) !=
        QMessageBox::Yes) {
        return;
    }

    auto listed_connection = std::make_shared<QMetaObject::Connection>();
    auto failed_connection = std::make_shared<QMetaObject::Connection>();
    *listed_connection = connect(
        bootloader_manager_,
        &kh::boot::IBootloaderManager::bootEntriesListed,
        this,
        [this, kernel, listed_connection, failed_connection](const QList<kh::boot::BootEntry> &entries) {
            disconnect(*listed_connection);
            disconnect(*failed_connection);
            for (const kh::boot::BootEntry &entry : entries) {
                if (entry.kernelVersion == kernel.version) {
                    const QString kernel_path =
                        entry.entryId.startsWith(QStringLiteral("/boot/vmlinuz-"))
                            ? entry.entryId
                            : QStringLiteral("/boot/vmlinuz-%1").arg(entry.kernelVersion);
                    bootloader_manager_->setDefaultEntry(kernel_path);
                    return;
                }
            }
            QMessageBox::warning(this,
                                 QStringLiteral("Boot Entry Not Found"),
                                 QStringLiteral("No boot entry matched %1.").arg(kernel.version));
        });
    *failed_connection = connect(
        bootloader_manager_,
        &kh::boot::IBootloaderManager::bootEntriesFailed,
        this,
        [this, listed_connection, failed_connection](const QString &error) {
            disconnect(*listed_connection);
            disconnect(*failed_connection);
            QMessageBox::warning(this, QStringLiteral("Bootloader Error"), error);
        });
    bootloader_manager_->listBootEntries();
}

void MainWindow::copySelectedKernelVersion() {
    const kh::model::KernelInfo kernel = selectedKernel();
    if (!kernel.version.isEmpty()) {
        QApplication::clipboard()->setText(kernel.version);
        statusBar()->showMessage(QStringLiteral("Version copied"), 2500);
    }
}

void MainWindow::showSettings() {
    SettingsDialog dialog(config_manager_, this);
    if (dialog.exec() == QDialog::Accepted) {
        applyConfig();
        applyTheme();
    }
}

void MainWindow::showBootOrderPanel() {
    if (boot_dock_ == nullptr) {
        boot_dock_ = new QDockWidget(QStringLiteral("Boot Order"), this);
        boot_dock_->setObjectName(QStringLiteral("bootOrderPanel"));
        boot_table_ = new QTableWidget(0, 5, boot_dock_);
        boot_table_->setHorizontalHeaderLabels(
            {QStringLiteral("Order"),
             QStringLiteral("Kernel Version"),
             QStringLiteral("Title"),
             QStringLiteral("Default"),
             QStringLiteral("Running")});
        boot_table_->horizontalHeader()->setStretchLastSection(true);
        boot_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        boot_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        boot_dock_->setWidget(boot_table_);
        addDockWidget(Qt::BottomDockWidgetArea, boot_dock_);
    }
    boot_dock_->show();
    if (bootloader_manager_ != nullptr) {
        bootloader_manager_->listBootEntries();
    } else {
        boot_table_->setRowCount(0);
    }
}

void MainWindow::regenerateBootConfig() {
    if (bootloader_manager_ == nullptr) {
        showOperationUnavailable(QStringLiteral("Regenerate GRUB Config"));
        return;
    }
    if (QMessageBox::warning(this,
                             QStringLiteral("Regenerate GRUB Config"),
                             QStringLiteral("Regenerate the bootloader configuration?"),
                             QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    bootloader_manager_->regenerateConfig();
}

void MainWindow::exportKernelList() {
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("Export Kernel List"),
                                                      QStringLiteral("bumpcap-kernels.csv"),
                                                      QStringLiteral("CSV Files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), file.errorString());
        return;
    }
    QTextStream stream(&file);
    stream << "Source,Version,Released,Status,Notes\n";
    for (const kh::model::KernelInfo &kernel : table_model_->kernels()) {
        stream << CsvEscape(kernel.sourceDisplayName) << ','
               << CsvEscape(kernel.version) << ','
               << CsvEscape(kernel.releaseDate.toString(Qt::ISODate)) << ','
               << CsvEscape(KernelTableModel::accessibleStatusText(kernel)) << ','
               << CsvEscape(kernel.notes) << '\n';
    }
    if (!file.commit()) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"), file.errorString());
    }
}

void MainWindow::openLogFile() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDesktopServices::openUrl(QUrl::fromLocalFile(root));
}

void MainWindow::checkCpuCompatibility() {
    const kh::sources::CpuFeatures features = kh::sources::DetectHostCpuFeatures();
    QMessageBox::information(
        this,
        QStringLiteral("CPU Compatibility"),
        QStringLiteral("Detected CPU level: %1\nDetection method: %2")
            .arg(kh::sources::MicroarchitectureLevelToString(features.level),
                 features.detectionMethod));
}

void MainWindow::showAboutDialog() {
    QMessageBox::about(this,
                       QStringLiteral("About Bumpcap"),
                       QStringLiteral("Bumpcap 0.1.0\nQt Widgets utility for managing Fedora kernels."));
}

void MainWindow::applyConfig() {
    table_model_->setSourcePriorities(sourcePriorities());
}

void MainWindow::applyTheme() {
    auto &theme_manager = theme::ThemeManager::instance();
    if (config_manager_ != nullptr) {
        const QString mode = config_manager_->themeMode();
        if (mode == QStringLiteral("light")) {
            theme_manager.setMode(theme::ThemeManager::Mode::Light);
        } else if (mode == QStringLiteral("dark")) {
            theme_manager.setMode(theme::ThemeManager::Mode::Dark);
        } else {
            theme_manager.setMode(theme::ThemeManager::Mode::System);
        }
    }
    // setMode() above may have already triggered this via themeChanged; the
    // repeat call is a cheap no-op UI refresh, not re-entrant config logic.
    theme_manager.applyToApplication();
    refreshActionIcons();
    theme_light_action_->setChecked(theme_manager.mode() == theme::ThemeManager::Mode::Light);
    theme_dark_action_->setChecked(theme_manager.mode() == theme::ThemeManager::Mode::Dark);
    theme_system_action_->setChecked(theme_manager.mode() == theme::ThemeManager::Mode::System);
}

void MainWindow::createActions() {
    refresh_action_ = new QAction(QStringLiteral("Refresh"), this);
    refresh_action_->setShortcut(QKeySequence::Refresh);
    connect(refresh_action_, &QAction::triggered, this, &MainWindow::refreshKernels);

    install_action_ = new QAction(QStringLiteral("Install"), this);
    connect(install_action_, &QAction::triggered, this, &MainWindow::installSelectedKernel);

    remove_action_ = new QAction(QStringLiteral("Remove"), this);
    connect(remove_action_, &QAction::triggered, this, &MainWindow::removeSelectedKernel);

    pin_action_ = new QAction(QStringLiteral("Pin"), this);
    connect(pin_action_, &QAction::triggered, this, &MainWindow::toggleSelectedKernelPin);

    settings_action_ = new QAction(QStringLiteral("Preferences..."), this);
    settings_action_->setShortcut(QKeySequence::Preferences);
    connect(settings_action_, &QAction::triggered, this, &MainWindow::showSettings);

    detail_panel_action_ = new QAction(QStringLiteral("Detail Panel"), this);
    detail_panel_action_->setCheckable(true);
    detail_panel_action_->setChecked(true);

    boot_panel_action_ = new QAction(QStringLiteral("Boot Order Panel"), this);
    connect(boot_panel_action_, &QAction::triggered, this, &MainWindow::showBootOrderPanel);

    compact_mode_action_ = new QAction(QStringLiteral("Compact Mode"), this);
    compact_mode_action_->setCheckable(true);
    connect(compact_mode_action_, &QAction::toggled, this, [this](bool compact) {
        table_view_->verticalHeader()->setDefaultSectionSize(compact ? 24 : 32);
    });

    status_bar_action_ = new QAction(QStringLiteral("Show Status Bar"), this);
    status_bar_action_->setCheckable(true);
    status_bar_action_->setChecked(true);
    connect(status_bar_action_, &QAction::toggled, statusBar(), &QStatusBar::setVisible);

    auto *theme_group = new QActionGroup(this);
    theme_group->setExclusive(true);
    theme_light_action_ = new QAction(QStringLiteral("Light"), this);
    theme_dark_action_ = new QAction(QStringLiteral("Dark"), this);
    theme_system_action_ = new QAction(QStringLiteral("Match System"), this);
    for (QAction *action : {theme_light_action_, theme_dark_action_, theme_system_action_}) {
        action->setCheckable(true);
        theme_group->addAction(action);
    }
    connect(theme_light_action_, &QAction::triggered, this, [this]() {
        if (config_manager_ != nullptr) {
            config_manager_->setThemeMode(QStringLiteral("light"));
        }
        applyTheme();
    });
    connect(theme_dark_action_, &QAction::triggered, this, [this]() {
        if (config_manager_ != nullptr) {
            config_manager_->setThemeMode(QStringLiteral("dark"));
        }
        applyTheme();
    });
    connect(theme_system_action_, &QAction::triggered, this, [this]() {
        if (config_manager_ != nullptr) {
            config_manager_->setThemeMode(QStringLiteral("system"));
        }
        applyTheme();
    });

    refreshActionIcons();
}

void MainWindow::refreshActionIcons() {
    auto &theme_manager = theme::ThemeManager::instance();
    refresh_action_->setIcon(theme_manager.icon(QStringLiteral("toolbar-refresh")));
    install_action_->setIcon(theme_manager.icon(QStringLiteral("toolbar-install")));
    remove_action_->setIcon(theme_manager.icon(QStringLiteral("toolbar-remove")));
    pin_action_->setIcon(theme_manager.icon(QStringLiteral("toolbar-pin")));
    settings_action_->setIcon(theme_manager.icon(QStringLiteral("toolbar-settings")));
    boot_panel_action_->setIcon(theme_manager.icon(QStringLiteral("toolbar-boot-order")));
}

void MainWindow::createMenus() {
    QMenu *file_menu = menuBar()->addMenu(QStringLiteral("File"));
    file_menu->addAction(refresh_action_);
    file_menu->addAction(QStringLiteral("Export Kernel List"), this, &MainWindow::exportKernelList);
    file_menu->addSeparator();
    file_menu->addAction(QStringLiteral("Quit"), qApp, &QApplication::quit, QKeySequence::Quit);

    QMenu *edit_menu = menuBar()->addMenu(QStringLiteral("Edit"));
    edit_menu->addAction(settings_action_);

    QMenu *view_menu = menuBar()->addMenu(QStringLiteral("View"));
    view_menu->addAction(boot_panel_action_);
    view_menu->addAction(detail_panel_action_);
    view_menu->addAction(status_bar_action_);
    view_menu->addAction(compact_mode_action_);
    view_menu->addSeparator();
    QMenu *theme_menu = view_menu->addMenu(QStringLiteral("Appearance"));
    theme_menu->addAction(theme_light_action_);
    theme_menu->addAction(theme_dark_action_);
    theme_menu->addAction(theme_system_action_);

    QMenu *kernel_menu = menuBar()->addMenu(QStringLiteral("Kernel"));
    kernel_menu->addAction(install_action_);
    kernel_menu->addAction(remove_action_);
    kernel_menu->addAction(pin_action_);
    kernel_menu->addAction(QStringLiteral("Set as Default"), this, &MainWindow::setSelectedKernelAsDefault);
    kernel_menu->addAction(QStringLiteral("Edit Note"), this, &MainWindow::editSelectedKernelNote);
    kernel_menu->addAction(QStringLiteral("Remove Old Kernels..."), this, [this]() {
        QMessageBox::information(this,
                                 QStringLiteral("Remove Old Kernels"),
                                 QStringLiteral("Old-kernel cleanup will be enabled when the backend exposes batch removal."));
    });

    QMenu *tools_menu = menuBar()->addMenu(QStringLiteral("Tools"));
    tools_menu->addAction(QStringLiteral("Boot Order..."), this, &MainWindow::showBootOrderPanel);
    tools_menu->addAction(QStringLiteral("Regenerate GRUB Config"),
                          this,
                          &MainWindow::regenerateBootConfig);
    tools_menu->addAction(QStringLiteral("Open Log File"), this, &MainWindow::openLogFile);
    tools_menu->addAction(QStringLiteral("Check CPU Compatibility"),
                          this,
                          &MainWindow::checkCpuCompatibility);

    QMenu *help_menu = menuBar()->addMenu(QStringLiteral("Help"));
    help_menu->addAction(QStringLiteral("About"), this, &MainWindow::showAboutDialog);
    help_menu->addAction(QStringLiteral("Report Issue"), this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/bumpcap/bumpcap/issues")));
    });
    help_menu->addAction(QStringLiteral("Check for Bumpcap Updates"), this, [this]() {
        QMessageBox::information(this,
                                 QStringLiteral("Bumpcap Updates"),
                                 QStringLiteral("Application update checks are not implemented yet."));
    });
}

void MainWindow::createToolbar() {
    QToolBar *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));

    refresh_button_ = new widgets::AnimatedRefreshButton(toolbar);
    refresh_button_->setDefaultAction(refresh_action_);
    refresh_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->addWidget(refresh_button_);

    toolbar->addAction(install_action_);
    toolbar->addAction(remove_action_);
    toolbar->addAction(pin_action_);
    toolbar->addSeparator();

    filter_edit_ = new widgets::SearchFilter(toolbar);
    filter_edit_->setPlaceholderText(QStringLiteral("Filter kernels"));
    toolbar->addWidget(filter_edit_);

    quick_filter_combo_ = new QComboBox(toolbar);
    quick_filter_combo_->addItem(QStringLiteral("All"), KernelFilterProxyModel::kAll);
    quick_filter_combo_->addItem(QStringLiteral("Installed only"), KernelFilterProxyModel::kInstalled);
    quick_filter_combo_->addItem(QStringLiteral("Updates only"), KernelFilterProxyModel::kUpdates);
    quick_filter_combo_->addItem(QStringLiteral("Pinned only"), KernelFilterProxyModel::kPinned);
    toolbar->addWidget(quick_filter_combo_);
}

void MainWindow::createStatusBar() {
    kernel_count_label_ = new QLabel(QStringLiteral("0 kernels"), this);
    update_count_label_ = new QLabel(QStringLiteral("0 updates"), this);
    last_checked_label_ = new QLabel(QStringLiteral("Last checked never"), this);
    statusBar()->addPermanentWidget(kernel_count_label_);
    statusBar()->addPermanentWidget(update_count_label_);
    statusBar()->addPermanentWidget(last_checked_label_);
}

void MainWindow::createCentralTable() {
    table_model_ = new KernelTableModel(this);
    filter_model_ = new KernelFilterProxyModel(this);
    filter_model_->setSourceModel(table_model_);

    table_view_ = new QTableView(this);
    table_view_->setModel(filter_model_);
    table_view_->setSortingEnabled(true);
    table_view_->sortByColumn(KernelTableModel::kSourceColumn, Qt::AscendingOrder);
    table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_view_->setAlternatingRowColors(true);
    table_view_->setWordWrap(false);
    table_view_->verticalHeader()->setVisible(false);
    table_view_->verticalHeader()->setDefaultSectionSize(32);
    table_view_->horizontalHeader()->setStretchLastSection(true);
    table_view_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_view_->horizontalHeader()->setSectionResizeMode(KernelTableModel::kNotesColumn,
                                                          QHeaderView::Stretch);
    auto *badge_delegate = new KernelBadgeDelegate(table_view_);
    table_view_->setItemDelegateForColumn(KernelTableModel::kSourceColumn, badge_delegate);
    table_view_->setItemDelegateForColumn(KernelTableModel::kStatusColumn, badge_delegate);
    setCentralWidget(table_view_);

    connect(table_view_,
            &QTableView::customContextMenuRequested,
            this,
            &MainWindow::showContextMenu);
    connect(table_view_,
            &QTableView::doubleClicked,
            this,
            &MainWindow::showSelectedKernelDetails);
    connect(table_view_->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            &MainWindow::updateActionState);
    connect(filter_edit_, &widgets::SearchFilter::filterChanged, this, [this](const QString &text) {
        filter_model_->setFilterFixedString(text);
    });
    connect(quick_filter_combo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this]() {
                auto *kernel_filter = static_cast<KernelFilterProxyModel *>(filter_model_);
                kernel_filter->setQuickFilter(static_cast<KernelFilterProxyModel::QuickFilter>(
                    quick_filter_combo_->currentData().toInt()));
            });
}

void MainWindow::createDetailPanel() {
    detail_panel_ = new DetailPanel(state_store_, this);
    addDockWidget(Qt::RightDockWidgetArea, detail_panel_);
    connect(detail_panel_action_, &QAction::toggled, detail_panel_, &QDockWidget::setVisible);
    connect(detail_panel_, &QDockWidget::visibilityChanged, detail_panel_action_, &QAction::setChecked);
    connect(detail_panel_, &DetailPanel::noteSaved, this, [this](const QString &version, const QString &note) {
        table_model_->setKernelNote(version, note);
    });
}

void MainWindow::createTrayIcon() {
    tray_icon_ = new TrayIcon(this, this);
    connect(tray_icon_, &TrayIcon::refreshRequested, this, &MainWindow::refreshKernels);
    connect(tray_icon_, &TrayIcon::showWindowRequested, this, [this]() {
        show();
        raise();
        activateWindow();
    });
    connect(tray_icon_, &TrayIcon::quickInstallRequested, this, [this](const kh::model::KernelInfo &kernel) {
        selectKernel(kernel);
        startPackageOperation(kernel, true);
    });
    connect(tray_icon_, &TrayIcon::quitRequested, qApp, &QApplication::quit);
    tray_icon_->show();
}

void MainWindow::connectCoreSignals() {
    if (repository_ != nullptr) {
        connect(repository_,
                &kh::repo::KernelRepository::kernelListChanged,
                this,
                &MainWindow::onKernelListChanged);
        connect(repository_,
                &kh::repo::KernelRepository::refreshStarted,
                this,
                &MainWindow::onRefreshStarted);
        connect(repository_,
                &kh::repo::KernelRepository::refreshFinished,
                this,
                &MainWindow::onRefreshFinished);
        connect(repository_,
                &kh::repo::KernelRepository::refreshFailed,
                this,
                &MainWindow::onRefreshFailed);
    }
    if (bootloader_manager_ != nullptr) {
        connect(bootloader_manager_,
                &kh::boot::IBootloaderManager::bootEntriesListed,
                this,
                &MainWindow::setBootEntries);
        connect(bootloader_manager_,
                &kh::boot::IBootloaderManager::operationFinished,
                this,
                [this](bool success, const QString &error) {
                    statusBar()->showMessage(success ? QStringLiteral("Bootloader operation complete")
                                                     : QStringLiteral("Bootloader operation failed: %1").arg(error),
                                             7000);
                });
    }
}

void MainWindow::updateStatusSummary() {
    const int kernel_count = table_model_->kernels().size();
    int update_count = 0;
    for (const kh::model::KernelInfo &kernel : table_model_->kernels()) {
        if (kernel.status == kh::model::KernelStatus::UpdateAvailable) {
            ++update_count;
        }
    }
    kernel_count_label_->setText(QStringLiteral("%1 kernel(s)").arg(kernel_count));
    update_count_label_->setText(QStringLiteral("%1 update(s)").arg(update_count));
}

void MainWindow::startPackageOperation(const kh::model::KernelInfo &kernel, bool install) {
    if (kernel.version.isEmpty()) {
        return;
    }
    if (package_backend_ == nullptr) {
        showOperationUnavailable(install ? QStringLiteral("Install") : QStringLiteral("Remove"));
        return;
    }
    if (config_manager_ != nullptr && config_manager_->confirmBeforeInstallRemove()) {
        const QString verb = install ? QStringLiteral("install") : QStringLiteral("remove");
        if (QMessageBox::question(this,
                                  install ? QStringLiteral("Install Kernel")
                                          : QStringLiteral("Remove Kernel"),
                                  QStringLiteral("Do you want to %1 %2?").arg(verb, kernel.version)) !=
            QMessageBox::Yes) {
            return;
        }
    }

    auto *dialog = new ProgressDialog((install ? QStringLiteral("Installing ")
                                               : QStringLiteral("Removing ")) +
                                          kernel.version,
                                      this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    kh::pkg::OperationHandle handle =
        install ? package_backend_->installKernel(kernel)
                : package_backend_->removeKernel(kernel, false);

    connect(dialog, &ProgressDialog::cancellationRequested, this, [this, handle, dialog]() {
        package_backend_->cancelOperation(handle);
        dialog->setCancellationEnabled(false);
        dialog->appendDetail(QStringLiteral("Cancellation requested."));
    });
    connect(package_backend_,
            &kh::pkg::IPackageBackend::operationProgress,
            dialog,
            [dialog, handle](const kh::pkg::OperationHandle &progress_handle,
                             int percent,
                             const QString &status_text) {
                if (progress_handle.id != handle.id) {
                    return;
                }
                dialog->setProgress(percent);
                dialog->setStatusText(status_text);
                dialog->appendDetail(status_text);
            });
    connect(package_backend_,
            &kh::pkg::IPackageBackend::operationFinished,
            dialog,
            [this, dialog, handle, kernel, install](const kh::pkg::OperationHandle &finished_handle,
                                                    bool success) {
                if (finished_handle.id != handle.id) {
                    return;
                }
                dialog->finish(success,
                               success ? QStringLiteral("Operation complete")
                                       : QStringLiteral("Operation did not complete"));
                if (state_store_ != nullptr && state_store_->isOpen()) {
                    state_store_->recordInstallAction(kernel.version,
                                                      kernel.sourceId,
                                                      install ? QStringLiteral("install")
                                                              : QStringLiteral("remove"),
                                                      success);
                }
                refreshKernels();
            });
    connect(package_backend_,
            &kh::pkg::IPackageBackend::operationFailed,
            dialog,
            [dialog, handle](const kh::pkg::OperationHandle &failed_handle,
                             const QString &error) {
                if (failed_handle.id != handle.id) {
                    return;
                }
                dialog->appendDetail(error);
                dialog->finish(false, QStringLiteral("Operation failed: %1").arg(error));
            });
    dialog->show();
}

void MainWindow::showOperationUnavailable(const QString &operation) const {
    QMessageBox::information(
        const_cast<MainWindow *>(this),
        operation,
        QStringLiteral("%1 is not available until the matching core backend is connected.")
            .arg(operation));
}

void MainWindow::setBootEntries(const QList<kh::boot::BootEntry> &entries) {
    if (boot_table_ == nullptr) {
        return;
    }
    boot_table_->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const kh::boot::BootEntry &entry = entries.at(row);
        boot_table_->setItem(row, 0, new QTableWidgetItem(QString::number(entry.bootOrderIndex)));
        boot_table_->setItem(row, 1, new QTableWidgetItem(entry.kernelVersion));
        boot_table_->setItem(row, 2, new QTableWidgetItem(entry.title));
        boot_table_->setItem(row, 3, new QTableWidgetItem(entry.isDefault ? QStringLiteral("Yes")
                                                                           : QStringLiteral("No")));
        boot_table_->setItem(row, 4, new QTableWidgetItem(entry.isCurrentlyRunning
                                                              ? QStringLiteral("Yes")
                                                              : QStringLiteral("No")));
    }
}

void MainWindow::selectKernel(const kh::model::KernelInfo &kernel) {
    for (int row = 0; row < table_model_->rowCount(); ++row) {
        if (table_model_->kernelAt(row).version != kernel.version) {
            continue;
        }
        const QModelIndex source_index = table_model_->index(row, 0);
        const QModelIndex proxy_index = filter_model_->mapFromSource(source_index);
        if (proxy_index.isValid()) {
            table_view_->selectRow(proxy_index.row());
            table_view_->scrollTo(proxy_index);
        }
        return;
    }
}

kh::model::KernelInfo MainWindow::selectedKernel() const {
    const QModelIndex source_index = selectedSourceIndex();
    if (!source_index.isValid()) {
        return {};
    }
    return table_model_->kernelAt(source_index.row());
}

QModelIndex MainWindow::selectedSourceIndex() const {
    if (table_view_ == nullptr || table_view_->selectionModel() == nullptr) {
        return {};
    }
    const QModelIndexList selected = table_view_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return {};
    }
    return filter_model_->mapToSource(selected.first());
}

QHash<int, int> MainWindow::sourcePriorities() const {
    QHash<int, int> priorities;
    for (const auto source_id : {kh::model::SourceId::FedoraStable,
                                 kh::model::SourceId::FedoraRawhide,
                                 kh::model::SourceId::CachyOsStable,
                                 kh::model::SourceId::CachyOsLts,
                                 kh::model::SourceId::KernelOrgMainline}) {
        priorities.insert(static_cast<int>(source_id),
                          config_manager_ != nullptr ? config_manager_->sourcePriority(source_id)
                                                     : static_cast<int>(source_id));
    }
    return priorities;
}

}  // namespace kh::gui
