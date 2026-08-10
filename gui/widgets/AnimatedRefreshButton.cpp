#include "gui/widgets/AnimatedRefreshButton.h"

#include <QPainter>
#include <QPropertyAnimation>
#include <QStyleOptionToolButton>
#include <QStylePainter>

#include "gui/theme/Animations.h"

namespace kh::gui::widgets {

AnimatedRefreshButton::AnimatedRefreshButton(QWidget *parent) : QToolButton(parent) {}

void AnimatedRefreshButton::startSpinning() {
    if (animation_ != nullptr) {
        return;
    }
    animation_ = theme::animations::spin(this, 900);
    setEnabled(false);
}

void AnimatedRefreshButton::stopSpinning() {
    if (animation_ == nullptr) {
        return;
    }
    animation_->stop();
    animation_->deleteLater();
    animation_ = nullptr;
    setRotation(0.0);
    setEnabled(true);
}

bool AnimatedRefreshButton::isSpinning() const {
    return animation_ != nullptr;
}

qreal AnimatedRefreshButton::rotation() const {
    return rotation_;
}

void AnimatedRefreshButton::setRotation(qreal degrees) {
    rotation_ = degrees;
    update();
}

void AnimatedRefreshButton::paintEvent(QPaintEvent *) {
    QStylePainter painter(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);

    if (qFuzzyIsNull(rotation_) || option.icon.isNull()) {
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
        return;
    }

    const QIcon original_icon = option.icon;
    option.icon = QIcon();
    painter.drawComplexControl(QStyle::CC_ToolButton, option);

    const QPixmap pixmap = original_icon.pixmap(option.iconSize);
    painter.save();
    painter.translate(rect().center());
    painter.rotate(rotation_);
    painter.translate(-pixmap.rect().center());
    painter.drawPixmap(0, 0, pixmap);
    painter.restore();
}

}  // namespace kh::gui::widgets
