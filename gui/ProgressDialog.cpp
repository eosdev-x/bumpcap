#include "gui/ProgressDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace kh::gui {

ProgressDialog::ProgressDialog(const QString &title, QWidget *parent) : QDialog(parent) {
    setWindowTitle(title);
    setModal(true);
    resize(520, 260);

    auto *layout = new QVBoxLayout(this);
    status_label_ = new QLabel(QStringLiteral("Preparing..."), this);
    status_label_->setWordWrap(true);
    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 0);

    details_button_ = new QToolButton(this);
    details_button_->setText(QStringLiteral("Details"));
    details_button_->setCheckable(true);
    details_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    details_button_->setArrowType(Qt::RightArrow);

    details_log_ = new QPlainTextEdit(this);
    details_log_->setReadOnly(true);
    details_log_->setVisible(false);
    details_log_->setMaximumBlockCount(2000);

    auto *buttons = new QDialogButtonBox(this);
    cancel_button_ = buttons->addButton(QDialogButtonBox::Cancel);
    cancel_button_->setProperty("variant", QStringLiteral("danger"));
    close_button_ = buttons->addButton(QDialogButtonBox::Close);
    close_button_->setProperty("variant", QStringLiteral("primary"));
    close_button_->setEnabled(false);

    layout->addWidget(status_label_);
    layout->addWidget(progress_bar_);
    layout->addWidget(details_button_);
    layout->addWidget(details_log_, 1);
    layout->addWidget(buttons);

    connect(details_button_, &QToolButton::toggled, this, &ProgressDialog::toggleDetails);
    connect(cancel_button_, &QPushButton::clicked, this, &ProgressDialog::cancellationRequested);
    connect(close_button_, &QPushButton::clicked, this, &QDialog::accept);
}

void ProgressDialog::setProgress(int percent) {
    if (percent < 0) {
        progress_bar_->setRange(0, 0);
        return;
    }
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(qBound(0, percent, 100));
}

void ProgressDialog::setStatusText(const QString &text) {
    status_label_->setText(text);
}

void ProgressDialog::appendDetail(const QString &line) {
    details_log_->appendPlainText(line);
}

void ProgressDialog::setCancellationEnabled(bool enabled) {
    cancel_button_->setEnabled(enabled);
}

void ProgressDialog::finish(bool success, const QString &message) {
    setProgress(success ? 100 : -1);
    setStatusText(message);
    cancel_button_->setEnabled(false);
    close_button_->setEnabled(true);
}

void ProgressDialog::toggleDetails(bool checked) {
    details_button_->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    details_log_->setVisible(checked);
    adjustSize();
}

}  // namespace kh::gui
