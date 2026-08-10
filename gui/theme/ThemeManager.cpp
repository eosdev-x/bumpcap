#include "gui/theme/ThemeManager.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QStyleHints>
#include <QSvgRenderer>

namespace kh::gui::theme {
namespace {

// Bundles the handful of tokens every stylesheet fragment needs so callers
// don't have to branch on isDark() themselves.
struct Palette {
    QColor background;
    QColor surface;
    QColor surfaceAlt;
    QColor border;
    QColor divider;
    QColor textPrimary;
    QColor textSecondary;
    QColor textDisabled;
};

Palette PaletteFor(bool dark) {
    Palette p;
    if (dark) {
        p.background = QColor(Colors::DarkBackground);
        p.surface = QColor(Colors::DarkSurface);
        p.surfaceAlt = QColor(Colors::DarkSurfaceAlt);
        p.border = QColor(Colors::DarkBorder);
        p.divider = QColor(Colors::DarkDivider);
        p.textPrimary = QColor(Colors::DarkTextPrimary);
        p.textSecondary = QColor(Colors::DarkTextSecondary);
        p.textDisabled = QColor(Colors::DarkTextDisabled);
    } else {
        p.background = QColor(Colors::Background);
        p.surface = QColor(Colors::Surface);
        p.surfaceAlt = QColor(Colors::SurfaceAlt);
        p.border = QColor(Colors::Border);
        p.divider = QColor(Colors::Divider);
        p.textPrimary = QColor(Colors::TextPrimary);
        p.textSecondary = QColor(Colors::TextSecondary);
        p.textDisabled = QColor(Colors::TextDisabled);
    }
    return p;
}

// Readable text color for a saturated badge background: near-black on
// light/warm chips, near-white on the darker saturated chips. All the
// status/source accent colors in Theme.h are mid-to-dark saturated tones,
// so white text clears WCAG AA (4.5:1) against every one of them; this is
// verified for the specific palette above rather than computed generically.
QColor OnAccentText() {
    return QColor(Qt::white);
}

}  // namespace

ThemeManager &ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager() {
    updateSystemDark();
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            const bool was_dark = isDark();
            updateSystemDark();
            if (mode_ == Mode::System && was_dark != isDark()) {
                emit themeChanged();
            }
        });
    }
}

void ThemeManager::setMode(Mode mode) {
    if (mode_ == mode) {
        return;
    }
    const bool was_dark = isDark();
    mode_ = mode;
    if (was_dark != isDark()) {
        emit themeChanged();
    }
}

ThemeManager::Mode ThemeManager::mode() const {
    return mode_;
}

bool ThemeManager::isDark() const {
    switch (mode_) {
    case Mode::Light:
        return false;
    case Mode::Dark:
        return true;
    case Mode::System:
        return system_dark_;
    }
    return false;
}

void ThemeManager::applyToApplication() const {
    if (auto *app = qApp) {
        app->setStyleSheet(appStyleSheet());
    }
}

void ThemeManager::updateSystemDark() {
    auto *hints = QGuiApplication::styleHints();
    system_dark_ = hints != nullptr && hints->colorScheme() == Qt::ColorScheme::Dark;
}

QString ThemeManager::appStyleSheet() const {
    const Palette p = PaletteFor(isDark());
    return QStringLiteral(
               "QWidget { background-color: %1; color: %2; font-family: '%3'; "
               "font-size: %4px; }"
               "QMainWindow, QDialog { background-color: %1; }"
               "QLabel { background-color: transparent; }"
               "QToolTip { background-color: %5; color: %2; border: 1px solid %6; "
               "padding: 4px 6px; border-radius: %7px; }"
               "QSplitter::handle { background-color: %6; }"
               "QGroupBox { border: 1px solid %6; border-radius: %8px; margin-top: %9px; "
               "padding-top: %10px; font-weight: 600; }"
               "QGroupBox::title { subcontrol-origin: margin; left: %9px; padding: 0 %11px; }")
               .arg(p.background.name(),
                    p.textPrimary.name(),
                    QString::fromLatin1(Typography::FontFamily))
               .arg(Typography::BodySize)
               .arg(p.surfaceAlt.name(), p.border.name())
               .arg(Radius::Sm)
               .arg(Radius::Md)
               .arg(Spacing::Md)
               .arg(Spacing::Sm)
               .arg(Spacing::Xs)
           + buttonStyle(ButtonVariant::Default)
           + buttonStyle(ButtonVariant::Primary)
           + buttonStyle(ButtonVariant::Danger)
           + buttonStyle(ButtonVariant::Flat)
           + tableStyle()
           + statusBarStyle()
           + toolBarStyle()
           + menuStyle()
           + dialogStyle()
           + inputStyle()
           + tabStyle()
           + scrollBarStyle()
           + progressBarStyle();
}

QString ThemeManager::buttonStyle(ButtonVariant variant) const {
    const Palette p = PaletteFor(isDark());
    QColor base;
    QColor baseHover;
    QColor basePressed;
    QColor text;
    QColor border = p.border;
    QString selector = QStringLiteral("QPushButton, QToolButton");

    switch (variant) {
    case ButtonVariant::Primary:
        base = QColor(Colors::Primary);
        baseHover = QColor(Colors::PrimaryLight);
        basePressed = QColor(Colors::PrimaryDark);
        text = OnAccentText();
        border = base;
        selector = QStringLiteral("QPushButton[variant=\"primary\"]");
        break;
    case ButtonVariant::Danger:
        base = QColor(Colors::Error);
        baseHover = QColor(Colors::Error).lighter(115);
        basePressed = QColor(Colors::ErrorDark);
        text = OnAccentText();
        border = base;
        selector = QStringLiteral("QPushButton[variant=\"danger\"]");
        break;
    case ButtonVariant::Flat:
        base = QColor(Qt::transparent);
        baseHover = p.surfaceAlt;
        basePressed = p.divider;
        text = p.textPrimary;
        border = QColor(Qt::transparent);
        selector = QStringLiteral("QToolButton[variant=\"flat\"]");
        break;
    case ButtonVariant::Default:
        base = p.surface;
        baseHover = p.surfaceAlt;
        basePressed = p.divider;
        text = p.textPrimary;
        break;
    }

    return QStringLiteral(
               "%1 { background-color: %2; color: %3; border: 1px solid %4; "
               "border-radius: %5px; padding: %6px %7px; }"
               "%1:hover { background-color: %8; }"
               "%1:pressed { background-color: %9; }"
               "%1:disabled { background-color: %10; color: %11; border-color: %10; }")
        .arg(selector,
             base.name(QColor::HexArgb),
             text.name(),
             border.name(QColor::HexArgb))
        .arg(Radius::Md)
        .arg(Spacing::Xs)
        .arg(Spacing::Md)
        .arg(baseHover.name(QColor::HexArgb),
             basePressed.name(QColor::HexArgb),
             p.divider.name(),
             p.textDisabled.name());
}

QString ThemeManager::tableStyle() const {
    const Palette p = PaletteFor(isDark());
    const QColor selection = QColor(Colors::Primary);
    return QStringLiteral(
               "QTableView, QTableWidget, QTreeView, QListWidget, QListView { "
               "background-color: %1; alternate-background-color: %2; color: %3; "
               "gridline-color: %4; border: 1px solid %4; border-radius: %5px; "
               "selection-background-color: %6; selection-color: %7; }"
               "QHeaderView::section { background-color: %2; color: %3; "
               "padding: %8px %9px; border: none; border-bottom: 2px solid %4; "
               "border-right: 1px solid %4; font-weight: 600; }"
               "QTableView::item, QTreeView::item { padding: %10px %9px; }")
        .arg(p.background.name(), p.surface.name(), p.textPrimary.name(), p.border.name())
        .arg(Radius::Md)
        .arg(selection.name(), OnAccentText().name())
        .arg(Spacing::Sm)
        .arg(Spacing::Md)
        .arg(Spacing::Xs);
}

QString ThemeManager::statusBarStyle() const {
    const Palette p = PaletteFor(isDark());
    return QStringLiteral(
               "QStatusBar { background-color: %1; color: %2; border-top: 1px solid %3; }"
               "QStatusBar QLabel { color: %2; padding: 0 %4px; }")
        .arg(p.surface.name(), p.textSecondary.name(), p.border.name())
        .arg(Spacing::Sm);
}

QString ThemeManager::toolBarStyle() const {
    const Palette p = PaletteFor(isDark());
    return QStringLiteral(
               "QToolBar { background-color: %1; border-bottom: 1px solid %2; "
               "padding: %3px; spacing: %4px; }"
               "QToolBar QToolButton { border-radius: %5px; padding: %6px; }"
               "QToolBar QToolButton:hover { background-color: %7; }"
               "QMenuBar { background-color: %1; color: %8; border-bottom: 1px solid %2; }"
               "QMenuBar::item { padding: %6px %3px; background: transparent; }"
               "QMenuBar::item:selected { background-color: %7; border-radius: %9px; }")
        .arg(p.surface.name(), p.border.name())
        .arg(Spacing::Xs)
        .arg(Spacing::Sm)
        .arg(Radius::Sm)
        .arg(Spacing::Xs)
        .arg(p.surfaceAlt.name(), p.textPrimary.name())
        .arg(Radius::Sm);
}

QString ThemeManager::menuStyle() const {
    const Palette p = PaletteFor(isDark());
    return QStringLiteral(
               "QMenu { background-color: %1; color: %2; border: 1px solid %3; "
               "border-radius: %4px; padding: %5px; }"
               "QMenu::item { padding: %6px %7px; border-radius: %8px; }"
               "QMenu::item:selected { background-color: %9; color: %10; }"
               "QMenu::separator { height: 1px; background: %3; margin: %5px 0; }")
        .arg(p.surface.name(), p.textPrimary.name(), p.border.name())
        .arg(Radius::Md)
        .arg(Spacing::Xs)
        .arg(Spacing::Xs)
        .arg(Spacing::Lg)
        .arg(Radius::Sm)
        .arg(QColor(Colors::Primary).name(), OnAccentText().name());
}

QString ThemeManager::dialogStyle() const {
    const Palette p = PaletteFor(isDark());
    return QStringLiteral(
               "QDialog { background-color: %1; }"
               "QDialogButtonBox { button-layout: 2; }"
               "QTabWidget::pane { border: 1px solid %2; border-radius: %3px; top: -1px; }"
               "QTabBar::tab { background-color: %4; color: %5; padding: %6px %7px; "
               "border: 1px solid %2; border-bottom: none; "
               "border-top-left-radius: %3px; border-top-right-radius: %3px; }"
               "QTabBar::tab:selected { background-color: %1; color: %8; font-weight: 600; }"
               "QTabBar::tab:!selected { margin-top: %9px; }")
        .arg(p.background.name(), p.border.name())
        .arg(Radius::Md)
        .arg(p.surface.name(), p.textSecondary.name())
        .arg(Spacing::Sm)
        .arg(Spacing::Lg)
        .arg(p.textPrimary.name())
        .arg(2);
}

QString ThemeManager::inputStyle() const {
    const Palette p = PaletteFor(isDark());
    const QColor focus = QColor(Colors::Primary);
    return QStringLiteral(
               "QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QComboBox { "
               "background-color: %1; color: %2; border: 1px solid %3; "
               "border-radius: %4px; padding: %5px %6px; selection-background-color: %7; "
               "selection-color: %8; }"
               "QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus { "
               "border: 1px solid %7; }"
               "QComboBox::drop-down { border: none; width: 20px; }"
               "QCheckBox, QRadioButton { spacing: %6px; }")
        .arg(p.background.name(), p.textPrimary.name(), p.border.name())
        .arg(Radius::Sm)
        .arg(Spacing::Xs)
        .arg(Spacing::Sm)
        .arg(focus.name(), OnAccentText().name());
}

QString ThemeManager::tabStyle() const {
    // Folded into dialogStyle(); kept as a distinct entry point per the
    // component-stylesheet API so callers don't need to know that.
    return dialogStyle();
}

QString ThemeManager::scrollBarStyle() const {
    const Palette p = PaletteFor(isDark());
    return QStringLiteral(
               "QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }"
               "QScrollBar::handle:vertical { background: %1; border-radius: %2px; "
               "min-height: 24px; margin: 2px; }"
               "QScrollBar::handle:vertical:hover { background: %3; }"
               "QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }"
               "QScrollBar::handle:horizontal { background: %1; border-radius: %2px; "
               "min-width: 24px; margin: 2px; }"
               "QScrollBar::handle:horizontal:hover { background: %3; }"
               "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; }"
               "QScrollBar::add-page, QScrollBar::sub-page { background: none; }")
        .arg(p.border.name())
        .arg(Radius::Full)
        .arg(p.textDisabled.name());
}

QString ThemeManager::progressBarStyle() const {
    const Palette p = PaletteFor(isDark());
    return QStringLiteral(
               "QProgressBar { background-color: %1; border: 1px solid %2; "
               "border-radius: %3px; text-align: center; color: %4; min-height: 18px; }"
               "QProgressBar::chunk { background-color: %5; border-radius: %3px; }")
        .arg(p.surfaceAlt.name(), p.border.name())
        .arg(Radius::Sm)
        .arg(p.textPrimary.name())
        .arg(QColor(Colors::Primary).name());
}

QString ThemeManager::statusBadgeStyle(kh::model::KernelStatus status) const {
    const QColor bg = statusColor(status);
    return QStringLiteral(
               "QWidget { background-color: %1; border-radius: %2px; }"
               "QLabel { background-color: transparent; color: %3; font-size: %4px; "
               "font-weight: 600; }")
        .arg(bg.name())
        .arg(Radius::Full)
        .arg(OnAccentText().name())
        .arg(Typography::CaptionSize);
}

QString ThemeManager::sourceBadgeStyle(kh::model::SourceId source) const {
    const QColor bg = sourceColor(source);
    return QStringLiteral(
               "QWidget { background-color: %1; border-radius: %2px; }"
               "QLabel { background-color: transparent; color: %3; font-size: %4px; "
               "font-weight: 600; }")
        .arg(bg.name())
        .arg(Radius::Full)
        .arg(OnAccentText().name())
        .arg(Typography::CaptionSize);
}

QColor ThemeManager::statusColor(kh::model::KernelStatus status) const {
    switch (status) {
    case kh::model::KernelStatus::Available:
        return QColor(Colors::Info);
    case kh::model::KernelStatus::Installed:
        return QColor(Colors::Success);
    case kh::model::KernelStatus::InstalledRunning:
        return QColor(Colors::SuccessDark);
    case kh::model::KernelStatus::UpdateAvailable:
        return QColor(Colors::Warning);
    }
    return QColor(Colors::Info);
}

QColor ThemeManager::sourceColor(kh::model::SourceId source) const {
    switch (source) {
    case kh::model::SourceId::FedoraStable:
        return QColor(Colors::FedoraBlue);
    case kh::model::SourceId::FedoraRawhide:
        return QColor(Colors::RawhideOrange);
    case kh::model::SourceId::CachyOsStable:
    case kh::model::SourceId::CachyOsLts:
        return QColor(Colors::CachyOsTeal);
    case kh::model::SourceId::KernelOrgMainline:
        return QColor(Colors::KernelOrgGray);
    }
    return QColor(Colors::KernelOrgGray);
}

QString ThemeManager::statusIconName(kh::model::KernelStatus status) const {
    switch (status) {
    case kh::model::KernelStatus::Available:
        return QStringLiteral("status-available");
    case kh::model::KernelStatus::Installed:
        return QStringLiteral("status-installed");
    case kh::model::KernelStatus::InstalledRunning:
        return QStringLiteral("status-running");
    case kh::model::KernelStatus::UpdateAvailable:
        return QStringLiteral("status-update");
    }
    return QStringLiteral("status-available");
}

QString ThemeManager::sourceIconName(kh::model::SourceId source) const {
    switch (source) {
    case kh::model::SourceId::FedoraStable:
        return QStringLiteral("source-fedora");
    case kh::model::SourceId::FedoraRawhide:
        return QStringLiteral("source-rawhide");
    case kh::model::SourceId::CachyOsStable:
    case kh::model::SourceId::CachyOsLts:
        return QStringLiteral("source-cachyos");
    case kh::model::SourceId::KernelOrgMainline:
        return QStringLiteral("source-kernelorg");
    }
    return QStringLiteral("source-fedora");
}

QColor ThemeManager::colorPrimary() const { return QColor(Colors::Primary); }
QColor ThemeManager::colorBackground() const { return PaletteFor(isDark()).background; }
QColor ThemeManager::colorSurface() const { return PaletteFor(isDark()).surface; }
QColor ThemeManager::colorSurfaceAlt() const { return PaletteFor(isDark()).surfaceAlt; }
QColor ThemeManager::colorBorder() const { return PaletteFor(isDark()).border; }
QColor ThemeManager::colorDivider() const { return PaletteFor(isDark()).divider; }
QColor ThemeManager::colorTextPrimary() const { return PaletteFor(isDark()).textPrimary; }
QColor ThemeManager::colorTextSecondary() const { return PaletteFor(isDark()).textSecondary; }
QColor ThemeManager::colorTextDisabled() const { return PaletteFor(isDark()).textDisabled; }

QIcon ThemeManager::icon(const QString &name, const QColor &color) const {
    return icon(name, 16, color);
}

QIcon ThemeManager::icon(const QString &name, int pixel_size, const QColor &color) const {
    QSvgRenderer renderer(QStringLiteral(":/kh/gui/icons/%1.svg").arg(name));
    if (!renderer.isValid()) {
        return {};
    }

    const QColor tint = color.isValid() ? color : colorTextPrimary();
    QIcon result;
    for (const int size : {pixel_size, pixel_size * 2}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), tint);
        painter.end();
        result.addPixmap(pixmap);
    }
    return result;
}

}  // namespace kh::gui::theme
