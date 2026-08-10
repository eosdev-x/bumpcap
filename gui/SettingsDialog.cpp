#include "gui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "core/model/SourceId.h"
#include "core/sources/CpuFeatures.h"

namespace kh::gui {
namespace {

QString LogFilePath() {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(root).filePath(QStringLiteral("bumpcap.log"));
}

}  // namespace

SettingsDialog::SettingsDialog(kh::config::ConfigManager *config_manager, QWidget *parent)
    : QDialog(parent), config_manager_(config_manager) {
    setWindowTitle(QStringLiteral("Preferences"));
    resize(620, 520);

    auto *layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    tabs_->addTab(createSourcesTab(), QStringLiteral("Sources"));
    tabs_->addTab(createNotificationsTab(), QStringLiteral("Notifications"));
    tabs_->addTab(createInstallTab(), QStringLiteral("Updates && Install"));
    tabs_->addTab(createBootloaderTab(), QStringLiteral("Bootloader"));
    tabs_->addTab(createAppearanceTab(), QStringLiteral("Appearance"));
    tabs_->addTab(createAdvancedTab(), QStringLiteral("Advanced"));
    layout->addWidget(tabs_, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    loadFromConfig();
}

void SettingsDialog::accept() {
    saveToConfig();
    QDialog::accept();
}

void SettingsDialog::resetSettings() {
    if (QMessageBox::question(this,
                              QStringLiteral("Reset Settings"),
                              QStringLiteral("Reset Bumpcap preferences to defaults?")) !=
        QMessageBox::Yes) {
        return;
    }
    if (config_manager_ != nullptr) {
        config_manager_->resetToDefaults();
        loadFromConfig();
    }
}

QWidget *SettingsDialog::createSourcesTab() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    sources_list_ = new QListWidget(page);
    sources_list_->setDragDropMode(QAbstractItemView::InternalMove);
    sources_list_->setDefaultDropAction(Qt::MoveAction);
    sources_list_->setSelectionMode(QAbstractItemView::SingleSelection);

    addSourceItem(kh::model::SourceId::FedoraStable, QStringLiteral("Fedora Stable"), true);
    addSourceItem(kh::model::SourceId::FedoraRawhide, QStringLiteral("Fedora Rawhide"));
    addSourceItem(kh::model::SourceId::CachyOsStable, QStringLiteral("CachyOS Stable"));
    addSourceItem(kh::model::SourceId::CachyOsLts, QStringLiteral("CachyOS LTS"));

    compatibility_label_ = new QLabel(compatibilityWarning(), page);
    compatibility_label_->setWordWrap(true);

    layout->addWidget(new QLabel(QStringLiteral("Enabled sources and priority:"), page));
    layout->addWidget(sources_list_, 1);
    layout->addWidget(compatibility_label_);
    return page;
}

QWidget *SettingsDialog::createNotificationsTab() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    notifications_enabled_ = new QCheckBox(QStringLiteral("Enable desktop notifications"), page);
    layout->addWidget(notifications_enabled_);

    auto *source_box = new QGroupBox(QStringLiteral("Sources"), page);
    auto *source_layout = new QVBoxLayout(source_box);
    for (const auto source_id : {kh::model::SourceId::FedoraStable,
                                 kh::model::SourceId::FedoraRawhide,
                                 kh::model::SourceId::CachyOsStable,
                                 kh::model::SourceId::CachyOsLts}) {
        auto *check = new QCheckBox(kh::model::SourceIdDisplayName(source_id), source_box);
        notification_source_checks_.insert(static_cast<int>(source_id), check);
        source_layout->addWidget(check);
    }
    layout->addWidget(source_box);

    check_interval_combo_ = new QComboBox(page);
    check_interval_combo_->addItem(QStringLiteral("1 hour"), 60);
    check_interval_combo_->addItem(QStringLiteral("6 hours"), 360);
    check_interval_combo_->addItem(QStringLiteral("12 hours"), 720);
    check_interval_combo_->addItem(QStringLiteral("Daily"), 1440);
    check_interval_combo_->addItem(QStringLiteral("Manual"), 0);

    stable_only_notifications_ =
        new QCheckBox(QStringLiteral("Only notify for stable sources"), page);

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("Check interval:"), check_interval_combo_);
    layout->addLayout(form);
    layout->addWidget(stable_only_notifications_);
    layout->addStretch(1);
    return page;
}

QWidget *SettingsDialog::createInstallTab() {
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    backend_combo_ = new QComboBox(page);
    backend_combo_->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
    backend_combo_->addItem(QStringLiteral("PackageKit"), QStringLiteral("packagekit"));
    backend_combo_->addItem(QStringLiteral("DNF CLI"), QStringLiteral("dnf-cli"));

    install_headers_ = new QCheckBox(QStringLiteral("Install headers and devel packages"), page);
    confirm_operations_ =
        new QCheckBox(QStringLiteral("Confirm before installing or removing kernels"), page);

    auto *retention_hint = new QLabel(
        QStringLiteral("Kernel retention is still governed by DNF installonly_limit."),
        page);
    retention_hint->setWordWrap(true);

    form->addRow(QStringLiteral("Package backend:"), backend_combo_);
    form->addRow(QString(), install_headers_);
    form->addRow(QString(), confirm_operations_);
    form->addRow(QStringLiteral("Retention:"), retention_hint);
    return page;
}

QWidget *SettingsDialog::createBootloaderTab() {
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    after_install_combo_ = new QComboBox(page);
    after_install_combo_->addItem(QStringLiteral("None"), QStringLiteral("none"));
    after_install_combo_->addItem(QStringLiteral("Prompt"), QStringLiteral("prompt"));
    after_install_combo_->addItem(QStringLiteral("Always set default"), QStringLiteral("always"));
    form->addRow(QStringLiteral("After install:"), after_install_combo_);
    return page;
}

QWidget *SettingsDialog::createAppearanceTab() {
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    theme_mode_combo_ = new QComboBox(page);
    theme_mode_combo_->addItem(QStringLiteral("Match System"), QStringLiteral("system"));
    theme_mode_combo_->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    theme_mode_combo_->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    form->addRow(QStringLiteral("Theme:"), theme_mode_combo_);

    auto *hint = new QLabel(
        QStringLiteral("Bumpcap follows Fedora's Fedora-blue accent in both light and "
                       "dark mode."),
        page);
    hint->setWordWrap(true);
    form->addRow(QString(), hint);
    return page;
}

QWidget *SettingsDialog::createAdvancedTab() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout();

    log_level_combo_ = new QComboBox(page);
    for (const QString &level : {QStringLiteral("debug"),
                                 QStringLiteral("info"),
                                 QStringLiteral("warning"),
                                 QStringLiteral("error")}) {
        log_level_combo_->addItem(level, level);
    }

    log_file_location_ = new QLineEdit(LogFilePath(), page);
    log_file_location_->setReadOnly(true);

    form->addRow(QStringLiteral("Log level:"), log_level_combo_);
    form->addRow(QStringLiteral("Log file:"), log_file_location_);
    layout->addLayout(form);

    auto *reset_button = new QPushButton(QStringLiteral("Reset Settings"), page);
    auto *clear_cache_button = new QPushButton(QStringLiteral("Clear Cache"), page);
    clear_cache_button->setEnabled(false);
    clear_cache_button->setToolTip(
        QStringLiteral("Cache clearing will be enabled when metadata caching lands in core."));
    layout->addWidget(reset_button);
    layout->addWidget(clear_cache_button);
    layout->addStretch(1);

    connect(reset_button, &QPushButton::clicked, this, &SettingsDialog::resetSettings);
    return page;
}

void SettingsDialog::loadFromConfig() {
    if (config_manager_ == nullptr) {
        return;
    }

    QList<QListWidgetItem *> source_items;
    while (sources_list_->count() > 0) {
        source_items.push_back(sources_list_->takeItem(0));
    }
    std::sort(source_items.begin(),
              source_items.end(),
              [this](const QListWidgetItem *left, const QListWidgetItem *right) {
                  const auto left_id = static_cast<kh::model::SourceId>(
                      left->data(Qt::UserRole).toInt());
                  const auto right_id = static_cast<kh::model::SourceId>(
                      right->data(Qt::UserRole).toInt());
                  return config_manager_->sourcePriority(left_id) <
                         config_manager_->sourcePriority(right_id);
              });
    for (QListWidgetItem *item : source_items) {
        sources_list_->addItem(item);
    }

    for (int row = 0; row < sources_list_->count(); ++row) {
        QListWidgetItem *item = sources_list_->item(row);
        const auto source_id = static_cast<kh::model::SourceId>(
            item->data(Qt::UserRole).toInt());
        item->setCheckState(config_manager_->sourceEnabled(source_id) ? Qt::Checked
                                                                      : Qt::Unchecked);
    }

    notifications_enabled_->setChecked(config_manager_->notificationsEnabled());
    for (QCheckBox *check : notification_source_checks_) {
        check->setChecked(true);
    }

    const int interval = config_manager_->checkIntervalMinutes();
    const int interval_index = check_interval_combo_->findData(interval);
    check_interval_combo_->setCurrentIndex(interval_index >= 0 ? interval_index : 1);
    stable_only_notifications_->setChecked(false);

    backend_combo_->setCurrentIndex(
        qMax(0, backend_combo_->findData(config_manager_->packageBackend())));
    install_headers_->setChecked(config_manager_->installHeaders());
    confirm_operations_->setChecked(config_manager_->confirmBeforeInstallRemove());
    after_install_combo_->setCurrentIndex(
        qMax(0, after_install_combo_->findData(config_manager_->afterInstallAction())));
    theme_mode_combo_->setCurrentIndex(
        qMax(0, theme_mode_combo_->findData(config_manager_->themeMode())));
    log_level_combo_->setCurrentIndex(qMax(0, log_level_combo_->findData(config_manager_->logLevel())));
}

void SettingsDialog::saveToConfig() {
    if (config_manager_ == nullptr) {
        return;
    }

    for (int row = 0; row < sources_list_->count(); ++row) {
        QListWidgetItem *item = sources_list_->item(row);
        const auto source_id = static_cast<kh::model::SourceId>(
            item->data(Qt::UserRole).toInt());
        config_manager_->setSourceEnabled(source_id, item->checkState() == Qt::Checked);
        config_manager_->setSourcePriority(source_id, row);
    }

    config_manager_->setNotificationsEnabled(notifications_enabled_->isChecked());
    config_manager_->setCheckIntervalMinutes(check_interval_combo_->currentData().toInt());
    config_manager_->setPackageBackend(backend_combo_->currentData().toString());
    config_manager_->setInstallHeaders(install_headers_->isChecked());
    config_manager_->setConfirmBeforeInstallRemove(confirm_operations_->isChecked());
    config_manager_->setAfterInstallAction(after_install_combo_->currentData().toString());
    config_manager_->setThemeMode(theme_mode_combo_->currentData().toString());
    config_manager_->setLogLevel(log_level_combo_->currentData().toString());
    config_manager_->save();
}

void SettingsDialog::addSourceItem(kh::model::SourceId source_id,
                                   const QString &label,
                                   bool always_enabled) {
    auto *item = new QListWidgetItem(label, sources_list_);
    item->setData(Qt::UserRole, static_cast<int>(source_id));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
    if (always_enabled) {
        item->setCheckState(Qt::Checked);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    } else {
        item->setCheckState(Qt::Unchecked);
    }
}

QString SettingsDialog::compatibilityWarning() const {
    const kh::sources::CpuFeatures features = kh::sources::DetectHostCpuFeatures();
    const QString detected = kh::sources::MicroarchitectureLevelToString(features.level);
    if (!kh::sources::SupportsLevel(features, kh::sources::MicroarchitectureLevel::X86_64V2)) {
        return QStringLiteral(
            "CachyOS kernels need at least x86-64-v2 support. Detected CPU level: %1.")
            .arg(detected);
    }
    if (!kh::sources::SupportsLevel(features, kh::sources::MicroarchitectureLevel::X86_64V3)) {
        return QStringLiteral(
            "CachyOS Stable is built for x86-64-v3. CachyOS LTS is the safer option "
            "for this CPU. Detected CPU level: %1.")
            .arg(detected);
    }
    return QStringLiteral("CachyOS CPU compatibility check passed. Detected CPU level: %1.")
        .arg(detected);
}

}  // namespace kh::gui
