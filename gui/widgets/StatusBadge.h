#pragma once

#include <QWidget>

#include "core/model/KernelInfo.h"

class QLabel;
class QPropertyAnimation;

namespace kh::gui::widgets {

// Small colored pill showing a kernel's install/update status, e.g.
// "[check] Installed", "[running-dot] Running" (pulsing), "[up-arrow] Update
// Available", "[dot] Available". A separate purple "Pinned" chip is shown
// alongside when the kernel is pinned.
class StatusBadge : public QWidget {
    Q_OBJECT

public:
    explicit StatusBadge(QWidget *parent = nullptr);

    void setStatus(kh::model::KernelStatus status);
    kh::model::KernelStatus status() const;

    void setPinned(bool pinned);
    bool isPinned() const;

private:
    void refresh();

    QLabel *icon_label_ = nullptr;
    QLabel *text_label_ = nullptr;
    QLabel *pin_badge_ = nullptr;
    QPropertyAnimation *running_pulse_ = nullptr;
    kh::model::KernelStatus status_ = kh::model::KernelStatus::Available;
    bool pinned_ = false;
};

}  // namespace kh::gui::widgets
