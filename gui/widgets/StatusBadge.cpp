#include "gui/widgets/StatusBadge.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>

#include "gui/theme/Animations.h"
#include "gui/theme/Theme.h"
#include "gui/theme/ThemeManager.h"

namespace kh::gui::widgets {
namespace {

QString StatusText(kh::model::KernelStatus status) {
    switch (status) {
    case kh::model::KernelStatus::Available:
        return QStringLiteral("Available");
    case kh::model::KernelStatus::Installed:
        return QStringLiteral("Installed");
    case kh::model::KernelStatus::InstalledRunning:
        return QStringLiteral("Running");
    case kh::model::KernelStatus::UpdateAvailable:
        return QStringLiteral("Update");
    }
    return QStringLiteral("Unknown");
}

}  // namespace

StatusBadge::StatusBadge(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);

    icon_label_ = new QLabel(this);
    icon_label_->setFixedSize(12, 12);
    icon_label_->setScaledContents(true);

    text_label_ = new QLabel(this);

    pin_badge_ = new QLabel(this);
    pin_badge_->setAttribute(Qt::WA_StyledBackground, true);
    pin_badge_->setText(QStringLiteral("Pinned"));
    pin_badge_->setVisible(false);
    pin_badge_->setContentsMargins(theme::Spacing::Sm, 1, theme::Spacing::Sm, 1);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(theme::Spacing::Sm, 2, theme::Spacing::Sm, 2);
    layout->setSpacing(theme::Spacing::Xs);
    layout->addWidget(icon_label_);
    layout->addWidget(text_label_);
    layout->addSpacing(theme::Spacing::Xs);
    layout->addWidget(pin_badge_);

    refresh();
}

void StatusBadge::setStatus(kh::model::KernelStatus status) {
    if (status_ == status) {
        return;
    }
    status_ = status;
    refresh();
}

kh::model::KernelStatus StatusBadge::status() const {
    return status_;
}

void StatusBadge::setPinned(bool pinned) {
    if (pinned_ == pinned) {
        return;
    }
    pinned_ = pinned;
    refresh();
}

bool StatusBadge::isPinned() const {
    return pinned_;
}

void StatusBadge::refresh() {
    auto &theme_manager = theme::ThemeManager::instance();

    setStyleSheet(theme_manager.statusBadgeStyle(status_));
    text_label_->setText(StatusText(status_));
    icon_label_->setPixmap(
        theme_manager.icon(theme_manager.statusIconName(status_), 12, Qt::white).pixmap(12, 12));

    if (running_pulse_ != nullptr) {
        running_pulse_->stop();
        running_pulse_ = nullptr;
    }
    if (status_ == kh::model::KernelStatus::InstalledRunning) {
        running_pulse_ = theme::animations::pulse(icon_label_);
    } else if (icon_label_->graphicsEffect() != nullptr) {
        icon_label_->setGraphicsEffect(nullptr);
    }

    pin_badge_->setVisible(pinned_);
    if (pinned_) {
        pin_badge_->setStyleSheet(
            QStringLiteral("QLabel { background-color: %1; color: white; border-radius: %2px; "
                            "font-size: %3px; font-weight: 600; }")
                .arg(theme::Colors::Pinned)
                .arg(theme::Radius::Full)
                .arg(theme::Typography::CaptionSize));
    }
}

}  // namespace kh::gui::widgets
