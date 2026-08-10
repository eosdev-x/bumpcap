#include "gui/widgets/KernelCard.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>

#include "gui/theme/Theme.h"
#include "gui/theme/ThemeManager.h"
#include "gui/widgets/SourceBadge.h"
#include "gui/widgets/StatusBadge.h"

namespace kh::gui::widgets {

KernelCard::KernelCard(QWidget *parent) : QFrame(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);

    source_badge_ = new SourceBadge(this);
    version_label_ = new QLabel(this);
    version_label_->setStyleSheet(QStringLiteral("font-weight: 600;"));
    status_badge_ = new StatusBadge(this);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(theme::Spacing::Sm, theme::Spacing::Xs, theme::Spacing::Sm,
                               theme::Spacing::Xs);
    layout->setSpacing(theme::Spacing::Sm);
    layout->addWidget(source_badge_);
    layout->addWidget(version_label_, 1);
    layout->addWidget(status_badge_);

    auto &theme_manager = theme::ThemeManager::instance();
    setStyleSheet(QStringLiteral("KernelCard { background-color: %1; border: 1px solid %2; "
                                 "border-radius: %3px; }")
                      .arg(theme_manager.colorSurface().name(), theme_manager.colorBorder().name())
                      .arg(theme::Radius::Md));
}

void KernelCard::setKernel(const kh::model::KernelInfo &kernel) {
    kernel_ = kernel;
    source_badge_->setSource(kernel_.sourceId, kernel_.sourceDisplayName);
    version_label_->setText(kernel_.version);
    status_badge_->setStatus(kernel_.status);
    status_badge_->setPinned(kernel_.isPinned);
    setToolTip(buildTooltip());
}

const kh::model::KernelInfo &KernelCard::kernel() const {
    return kernel_;
}

void KernelCard::mousePressEvent(QMouseEvent *event) {
    QFrame::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
}

void KernelCard::enterEvent(QEnterEvent *event) {
    QFrame::enterEvent(event);
    auto &theme_manager = theme::ThemeManager::instance();
    setStyleSheet(QStringLiteral("KernelCard { background-color: %1; border: 1px solid %2; "
                                 "border-radius: %3px; }")
                      .arg(theme_manager.colorSurfaceAlt().name(),
                           theme_manager.colorPrimary().name())
                      .arg(theme::Radius::Md));
}

void KernelCard::leaveEvent(QEvent *event) {
    QFrame::leaveEvent(event);
    auto &theme_manager = theme::ThemeManager::instance();
    setStyleSheet(QStringLiteral("KernelCard { background-color: %1; border: 1px solid %2; "
                                 "border-radius: %3px; }")
                      .arg(theme_manager.colorSurface().name(), theme_manager.colorBorder().name())
                      .arg(theme::Radius::Md));
}

QString KernelCard::buildTooltip() const {
    QString tooltip = kernel_.version;
    if (kernel_.releaseDate.isValid()) {
        tooltip += QStringLiteral("\nReleased: %1")
                       .arg(kernel_.releaseDate.toLocalTime().date().toString(Qt::ISODate));
    }
    if (!kernel_.notes.isEmpty()) {
        tooltip += QStringLiteral("\nNotes: %1").arg(kernel_.notes);
    }
    if (kernel_.compatibility.has_value() && !kernel_.compatibility->compatible) {
        tooltip += QStringLiteral("\n%1").arg(kernel_.compatibility->reason);
    }
    return tooltip;
}

}  // namespace kh::gui::widgets
