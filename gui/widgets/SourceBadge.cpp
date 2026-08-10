#include "gui/widgets/SourceBadge.h"

#include <QHBoxLayout>
#include <QLabel>

#include "core/model/SourceId.h"
#include "gui/theme/Theme.h"
#include "gui/theme/ThemeManager.h"

namespace kh::gui::widgets {

SourceBadge::SourceBadge(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);

    icon_label_ = new QLabel(this);
    icon_label_->setFixedSize(12, 12);
    icon_label_->setScaledContents(true);

    text_label_ = new QLabel(this);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(theme::Spacing::Sm, 2, theme::Spacing::Sm, 2);
    layout->setSpacing(theme::Spacing::Xs);
    layout->addWidget(icon_label_);
    layout->addWidget(text_label_);

    refresh();
}

void SourceBadge::setSource(kh::model::SourceId source, const QString &display_name) {
    source_ = source;
    display_name_ = display_name;
    refresh();
}

kh::model::SourceId SourceBadge::source() const {
    return source_;
}

void SourceBadge::refresh() {
    auto &theme_manager = theme::ThemeManager::instance();

    setStyleSheet(theme_manager.sourceBadgeStyle(source_));
    text_label_->setText(display_name_.isEmpty() ? kh::model::SourceIdDisplayName(source_)
                                                  : display_name_);
    icon_label_->setPixmap(
        theme_manager.icon(theme_manager.sourceIconName(source_), 12, Qt::white).pixmap(12, 12));
}

}  // namespace kh::gui::widgets
