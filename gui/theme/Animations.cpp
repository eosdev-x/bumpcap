#include "gui/theme/Animations.h"

#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QObject>
#include <QPoint>
#include <QPropertyAnimation>
#include <QWidget>

namespace kh::gui::theme::animations {
namespace {

QGraphicsOpacityEffect *EnsureOpacityEffect(QWidget *widget) {
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
    if (effect == nullptr) {
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
    }
    return effect;
}

}  // namespace

QPropertyAnimation *fadeIn(QWidget *widget, int duration_ms) {
    auto *effect = EnsureOpacityEffect(widget);
    effect->setOpacity(0.0);
    widget->show();

    auto *animation = new QPropertyAnimation(effect, "opacity", widget);
    animation->setDuration(duration_ms);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::InOutQuad);
    animation->start(QPropertyAnimation::DeleteWhenStopped);
    return animation;
}

QPropertyAnimation *fadeOut(QWidget *widget, int duration_ms) {
    auto *effect = EnsureOpacityEffect(widget);

    auto *animation = new QPropertyAnimation(effect, "opacity", widget);
    animation->setDuration(duration_ms);
    animation->setStartValue(effect->opacity());
    animation->setEndValue(0.0);
    animation->setEasingCurve(QEasingCurve::InOutQuad);
    QObject::connect(animation, &QPropertyAnimation::finished, widget, &QWidget::hide);
    animation->start(QPropertyAnimation::DeleteWhenStopped);
    return animation;
}

QPropertyAnimation *pulse(QWidget *widget, int duration_ms) {
    auto *effect = EnsureOpacityEffect(widget);

    auto *animation = new QPropertyAnimation(effect, "opacity", widget);
    animation->setDuration(duration_ms);
    animation->setStartValue(1.0);
    animation->setKeyValueAt(0.5, 0.4);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::InOutSine);
    animation->setLoopCount(-1);
    animation->start();
    return animation;
}

QPropertyAnimation *spin(QWidget *widget, int duration_ms) {
    auto *animation = new QPropertyAnimation(widget, "rotation", widget);
    animation->setDuration(duration_ms);
    animation->setStartValue(0.0);
    animation->setEndValue(360.0);
    animation->setEasingCurve(QEasingCurve::Linear);
    animation->setLoopCount(-1);
    animation->start();
    return animation;
}

QPropertyAnimation *slideIn(QWidget *widget, SlideDirection direction, int duration_ms) {
    const QPoint final_pos = widget->pos();
    QPoint start_pos = final_pos;
    switch (direction) {
    case SlideDirection::Left:
        start_pos.setX(final_pos.x() - widget->width());
        break;
    case SlideDirection::Right:
        start_pos.setX(final_pos.x() + widget->width());
        break;
    case SlideDirection::Top:
        start_pos.setY(final_pos.y() - widget->height());
        break;
    case SlideDirection::Bottom:
        start_pos.setY(final_pos.y() + widget->height());
        break;
    }

    widget->move(start_pos);
    widget->show();

    auto *animation = new QPropertyAnimation(widget, "pos", widget);
    animation->setDuration(duration_ms);
    animation->setStartValue(start_pos);
    animation->setEndValue(final_pos);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QPropertyAnimation::DeleteWhenStopped);
    return animation;
}

}  // namespace kh::gui::theme::animations
