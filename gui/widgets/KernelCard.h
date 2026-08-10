#pragma once

#include <QFrame>

#include "core/model/KernelInfo.h"

class QLabel;
class QMouseEvent;

namespace kh::gui::widgets {

class SourceBadge;
class StatusBadge;

// Compact one-line summary row for a kernel: source badge, version, status
// badge, with full detail (changelog snippet, notes) available on hover via
// tooltip. Intended for the system tray quick-install menu and an eventual
// "Compact Mode" list view — anywhere the full table is too heavy.
class KernelCard : public QFrame {
    Q_OBJECT

public:
    explicit KernelCard(QWidget *parent = nullptr);

    void setKernel(const kh::model::KernelInfo &kernel);
    const kh::model::KernelInfo &kernel() const;

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString buildTooltip() const;

    SourceBadge *source_badge_ = nullptr;
    QLabel *version_label_ = nullptr;
    StatusBadge *status_badge_ = nullptr;
    kh::model::KernelInfo kernel_;
};

}  // namespace kh::gui::widgets
