#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>
#include <QString>

#include "core/model/KernelInfo.h"
#include "core/model/SourceId.h"
#include "gui/theme/Theme.h"

namespace kh::gui::theme {

// Generates QStyleSheet fragments and tinted icons for the whole app from
// the token set in Theme.h. Singleton because exactly one theme applies to
// the process at a time and dozens of widgets need read access to it
// without threading a pointer through every constructor.
class ThemeManager : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        Light,
        Dark,
        System,
    };

    static ThemeManager &instance();

    void setMode(Mode mode);
    Mode mode() const;
    // Effective mode after resolving System via the desktop color scheme.
    bool isDark() const;

    // Applies appStyleSheet() to qApp and repaints. Call once at startup and
    // again whenever setMode() or the system theme changes (themeChanged
    // covers both).
    void applyToApplication() const;

    QString appStyleSheet() const;
    QString buttonStyle(ButtonVariant variant = ButtonVariant::Default) const;
    QString tableStyle() const;
    QString statusBarStyle() const;
    QString toolBarStyle() const;
    QString menuStyle() const;
    QString dialogStyle() const;
    QString inputStyle() const;
    QString tabStyle() const;
    QString scrollBarStyle() const;
    QString progressBarStyle() const;

    // Pill-badge stylesheets meant for a QWidget/QLabel with
    // Qt::WA_StyledBackground set (plain QWidget ignores QSS backgrounds
    // otherwise).
    QString statusBadgeStyle(kh::model::KernelStatus status) const;
    QString sourceBadgeStyle(kh::model::SourceId source) const;

    QColor statusColor(kh::model::KernelStatus status) const;
    QColor sourceColor(kh::model::SourceId source) const;
    QString statusIconName(kh::model::KernelStatus status) const;
    QString sourceIconName(kh::model::SourceId source) const;

    // Palette accessors — resolved for the current effective mode.
    QColor colorPrimary() const;
    QColor colorBackground() const;
    QColor colorSurface() const;
    QColor colorSurfaceAlt() const;
    QColor colorBorder() const;
    QColor colorDivider() const;
    QColor colorTextPrimary() const;
    QColor colorTextSecondary() const;
    QColor colorTextDisabled() const;

    // Loads gui/resources/icons/<name>.svg and rasterizes it tinted with
    // `color` (defaults to the current-theme primary-text color) at every
    // requested pixel size, so toolbar/menu icons stay legible in both
    // light and dark mode without shipping two icon sets.
    QIcon icon(const QString &name, const QColor &color = QColor()) const;
    QIcon icon(const QString &name, int pixel_size, const QColor &color = QColor()) const;

signals:
    void themeChanged();

private:
    ThemeManager();

    void updateSystemDark();

    Mode mode_ = Mode::System;
    bool system_dark_ = false;
};

}  // namespace kh::gui::theme
