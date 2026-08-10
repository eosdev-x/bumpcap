#pragma once

#include <QWidget>

#include "core/model/SourceId.h"

class QLabel;

namespace kh::gui::widgets {

// Color-coded source identifier chip: Fedora blue for Fedora Stable, orange
// for Rawhide, CachyOS teal for both CachyOS variants, neutral gray for the
// (stubbed) kernel.org source.
class SourceBadge : public QWidget {
    Q_OBJECT

public:
    explicit SourceBadge(QWidget *parent = nullptr);

    void setSource(kh::model::SourceId source, const QString &display_name = QString());
    kh::model::SourceId source() const;

private:
    void refresh();

    QLabel *icon_label_ = nullptr;
    QLabel *text_label_ = nullptr;
    kh::model::SourceId source_ = kh::model::SourceId::FedoraStable;
    QString display_name_;
};

}  // namespace kh::gui::widgets
