#pragma once

#include <QStyledItemDelegate>

namespace kh::gui {

// Paints the Source and Status columns of the kernel table as colored pill
// badges (matching gui::widgets::StatusBadge/SourceBadge) instead of plain
// text+icon, so the table reads at a glance the same way the design system
// intends the standalone badge widgets to. Every other column falls
// through to the default QStyledItemDelegate rendering.
class KernelBadgeDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit KernelBadgeDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter,
              const QStyleOptionViewItem &option,
              const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

}  // namespace kh::gui
