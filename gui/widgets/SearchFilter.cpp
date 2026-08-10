#include "gui/widgets/SearchFilter.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QTimer>

#include "gui/theme/ThemeManager.h"

namespace kh::gui::widgets {
namespace {
constexpr int kDebounceMs = 300;
}  // namespace

SearchFilter::SearchFilter(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    edit_ = new QLineEdit(this);
    edit_->setClearButtonEnabled(true);
    edit_->setPlaceholderText(QStringLiteral("Filter kernels"));
    edit_->addAction(theme::ThemeManager::instance().icon(QStringLiteral("action-search"), 14),
                     QLineEdit::LeadingPosition);
    layout->addWidget(edit_);

    debounce_timer_ = new QTimer(this);
    debounce_timer_->setSingleShot(true);
    debounce_timer_->setInterval(kDebounceMs);

    connect(edit_, &QLineEdit::textChanged, this, [this](const QString &) {
        debounce_timer_->start();
    });
    connect(debounce_timer_, &QTimer::timeout, this, [this]() { emit filterChanged(edit_->text()); });
}

QString SearchFilter::text() const {
    return edit_->text();
}

void SearchFilter::setPlaceholderText(const QString &text) {
    edit_->setPlaceholderText(text);
}

void SearchFilter::clear() {
    debounce_timer_->stop();
    edit_->clear();
    emit filterChanged(QString());
}

}  // namespace kh::gui::widgets
