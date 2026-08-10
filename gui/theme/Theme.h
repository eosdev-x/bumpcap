#pragma once

// Design tokens for the Bumpcap UI kit. Plain constexpr data only — no Qt
// dependency here so the tokens stay trivially includable from anywhere
// (widgets, delegates, ThemeManager). ThemeManager.h turns these into
// QColor/QString stylesheet fragments and picks light vs. dark variants.

namespace kh::gui::theme {

// Fedora-blue-anchored palette. Primary matches Fedora's own brand blue so
// the app reads as a native system tool rather than a third-party skin.
// Status colors follow common semantic conventions (green=good/running,
// orange=action-available, red=destructive/error, blue=neutral-info) so a
// user's existing mental model from GNOME Software / other package tools
// transfers directly.
struct Colors {
    static constexpr auto Primary = "#3C6EB4";
    static constexpr auto PrimaryDark = "#2A5299";
    static constexpr auto PrimaryLight = "#5B8FD4";

    static constexpr auto Success = "#2ECC71";
    static constexpr auto SuccessDark = "#25A25A";
    static constexpr auto Warning = "#F39C12";
    static constexpr auto WarningDark = "#C6790A";
    static constexpr auto Error = "#E74C3C";
    static constexpr auto ErrorDark = "#C0392B";
    static constexpr auto Info = "#3498DB";
    static constexpr auto InfoDark = "#2679B5";
    static constexpr auto Pinned = "#9B59B6";
    static constexpr auto PinnedDark = "#7D3C98";

    // Light mode neutrals
    static constexpr auto TextPrimary = "#2C3E50";
    static constexpr auto TextSecondary = "#7F8C8D";
    static constexpr auto TextDisabled = "#BDC3C7";
    static constexpr auto Background = "#FFFFFF";
    static constexpr auto Surface = "#F8F9FA";
    static constexpr auto SurfaceAlt = "#EEF1F4";
    static constexpr auto Border = "#DEE2E6";
    static constexpr auto Divider = "#E9ECEF";

    // Dark mode neutrals
    static constexpr auto DarkBackground = "#1A1A2E";
    static constexpr auto DarkSurface = "#16213E";
    static constexpr auto DarkSurfaceAlt = "#1F2C4C";
    static constexpr auto DarkTextPrimary = "#ECF0F1";
    static constexpr auto DarkTextSecondary = "#9FB0C3";
    static constexpr auto DarkTextDisabled = "#546A85";
    static constexpr auto DarkBorder = "#2C3E50";
    static constexpr auto DarkDivider = "#25324A";

    // Source brand accents (badges, table row hints)
    static constexpr auto FedoraBlue = "#3C6EB4";
    static constexpr auto RawhideOrange = "#F39C12";
    static constexpr auto CachyOsTeal = "#0FB9B1";
    static constexpr auto KernelOrgGray = "#6E7B8B";
};

struct Typography {
    static constexpr auto FontFamily = "Cantarell";
    static constexpr auto FontFamilyMono = "Source Code Pro";
    static constexpr int TitleSize = 16;
    static constexpr int SubtitleSize = 14;
    static constexpr int BodySize = 13;
    static constexpr int CaptionSize = 11;
    static constexpr int MonoSize = 12;
};

// 4px base spacing scale — keeps margins/paddings consistent across every
// hand-built layout instead of ad hoc magic numbers.
struct Spacing {
    static constexpr int Xs = 4;
    static constexpr int Sm = 8;
    static constexpr int Md = 12;
    static constexpr int Lg = 16;
    static constexpr int Xl = 24;
    static constexpr int Xxl = 32;
};

struct Radius {
    static constexpr int None = 0;
    static constexpr int Sm = 4;
    static constexpr int Md = 6;
    static constexpr int Lg = 8;
    static constexpr int Full = 999;
};

// Qt Widgets has no real box-shadow; these strings document the intended
// elevation for widgets that fake it with QGraphicsDropShadowEffect
// (see Animations.h) rather than being consumed by QSS directly.
struct Shadow {
    static constexpr auto Sm = "0 1px 2px rgba(0,0,0,0.05)";
    static constexpr auto Md = "0 4px 6px rgba(0,0,0,0.07)";
    static constexpr auto Lg = "0 10px 15px rgba(0,0,0,0.1)";
};

enum class ButtonVariant {
    Default,
    Primary,
    Danger,
    Flat,
};

}  // namespace kh::gui::theme
