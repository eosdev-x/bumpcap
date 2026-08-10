#pragma once

#include <QDialog>
#include <QHash>

#include "core/config/ConfigManager.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;

namespace kh::gui {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(kh::config::ConfigManager *config_manager,
                            QWidget *parent = nullptr);

private slots:
    void accept() override;
    void resetSettings();

private:
    QWidget *createSourcesTab();
    QWidget *createNotificationsTab();
    QWidget *createInstallTab();
    QWidget *createBootloaderTab();
    QWidget *createAppearanceTab();
    QWidget *createAdvancedTab();
    void loadFromConfig();
    void saveToConfig();
    void addSourceItem(kh::model::SourceId source_id,
                       const QString &label,
                       bool always_enabled = false);
    QString compatibilityWarning() const;

    kh::config::ConfigManager *config_manager_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QListWidget *sources_list_ = nullptr;
    QLabel *compatibility_label_ = nullptr;
    QCheckBox *notifications_enabled_ = nullptr;
    QHash<int, QCheckBox *> notification_source_checks_;
    QComboBox *check_interval_combo_ = nullptr;
    QCheckBox *stable_only_notifications_ = nullptr;
    QComboBox *backend_combo_ = nullptr;
    QCheckBox *install_headers_ = nullptr;
    QCheckBox *confirm_operations_ = nullptr;
    QComboBox *after_install_combo_ = nullptr;
    QComboBox *theme_mode_combo_ = nullptr;
    QComboBox *log_level_combo_ = nullptr;
    QLineEdit *log_file_location_ = nullptr;
};

}  // namespace kh::gui
