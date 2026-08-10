#pragma once

class QPropertyAnimation;
class QWidget;

namespace kh::gui::theme::animations {

enum class SlideDirection {
    Left,
    Right,
    Top,
    Bottom,
};

// All factories start the animation immediately and parent it to `widget`
// so it is destroyed along with the widget if the caller drops the
// returned pointer. One-shot animations (fadeIn/fadeOut/slideIn) delete
// themselves on completion; looping ones (pulse/spin) do not — call
// ->stop() (and optionally reset the target's property) when the effect
// should end, e.g. when a refresh operation finishes.

// Fades `widget` from transparent to opaque. Installs a QGraphicsOpacityEffect
// on the widget as a side effect.
QPropertyAnimation *fadeIn(QWidget *widget, int duration_ms = 200);

// Fades `widget` from opaque to transparent; hides the widget on completion.
QPropertyAnimation *fadeOut(QWidget *widget, int duration_ms = 200);

// Loops opacity between 1.0 and 0.4 — used for the "running kernel" status
// indicator so it reads as live without being distracting.
QPropertyAnimation *pulse(QWidget *widget, int duration_ms = 900);

// Loops a 0-360 degree rotation on the widget's "rotation" Q_PROPERTY.
// The target must declare that property (see AnimatedRefreshButton).
QPropertyAnimation *spin(QWidget *widget, int duration_ms = 1000);

// Slides `widget` into its current position from off-screen in `direction`.
QPropertyAnimation *slideIn(QWidget *widget,
                            SlideDirection direction,
                            int duration_ms = 250);

}  // namespace kh::gui::theme::animations
