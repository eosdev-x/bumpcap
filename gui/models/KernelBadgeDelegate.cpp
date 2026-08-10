#include "gui/models/KernelBadgeDelegate.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>

#include "core/model/KernelInfo.h"
#include "core/model/SourceId.h"
#include "gui/models/KernelTableModel.h"
#include "gui/theme/Theme.h"
#include "gui/theme/ThemeManager.h"

namespace kh::gui {
namespace {

constexpr int kBadgePaddingH = 10;
constexpr int kBadgeHeight = 20;
constexpr int kCellMargin = 6;

void PaintPill(QPainter *painter, const QRect &rect, const QColor &background, const QString &text) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    // Clip to the cell: a badge wider than a narrowed column must not bleed
    // into the neighboring column's paint area.
    painter->setClipRect(rect);

    QFont font = painter->font();
    font.setPointSizeF(theme::Typography::CaptionSize);
    font.setBold(true);
    painter->setFont(font);

    const QFontMetrics metrics(font);
    const int available_width = qMax(0, rect.width() - kCellMargin * 2);
    const int badge_width =
        qMin(metrics.horizontalAdvance(text) + kBadgePaddingH * 2, available_width);
    const int badge_height = qMin(rect.height() - 4, kBadgeHeight);
    const QRect badge_rect(rect.left() + kCellMargin,
                           rect.top() + (rect.height() - badge_height) / 2,
                           badge_width,
                           badge_height);
    const QString elided_text =
        metrics.elidedText(text, Qt::ElideRight, qMax(0, badge_width - kBadgePaddingH));

    painter->setPen(Qt::NoPen);
    painter->setBrush(background);
    painter->drawRoundedRect(badge_rect, badge_height / 2, badge_height / 2);

    painter->setPen(Qt::white);
    painter->drawText(badge_rect, Qt::AlignCenter, elided_text);
    painter->restore();
}

}  // namespace

KernelBadgeDelegate::KernelBadgeDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void KernelBadgeDelegate::paint(QPainter *painter,
                                const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    if (index.column() != KernelTableModel::kStatusColumn &&
        index.column() != KernelTableModel::kSourceColumn) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    opt.icon = QIcon();
    QStyle *style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const auto kernel = index.data(Qt::UserRole).value<kh::model::KernelInfo>();
    auto &theme_manager = theme::ThemeManager::instance();

    if (index.column() == KernelTableModel::kStatusColumn) {
        PaintPill(painter,
                 option.rect,
                 theme_manager.statusColor(kernel.status),
                 KernelTableModel::statusText(kernel.status));
        if (kernel.isPinned) {
            const QRect pin_rect(option.rect.right() - kCellMargin - 14,
                                 option.rect.top() + (option.rect.height() - 14) / 2,
                                 14,
                                 14);
            painter->drawPixmap(
                pin_rect,
                theme_manager.icon(QStringLiteral("status-pinned"), 14, QColor(theme::Colors::Pinned))
                    .pixmap(14, 14));
        }
    } else {
        const QString source_name = kernel.sourceDisplayName.isEmpty()
                                        ? kh::model::SourceIdDisplayName(kernel.sourceId)
                                        : kernel.sourceDisplayName;
        PaintPill(painter, option.rect, theme_manager.sourceColor(kernel.sourceId), source_name);
    }
}

QSize KernelBadgeDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    if (index.column() == KernelTableModel::kStatusColumn ||
        index.column() == KernelTableModel::kSourceColumn) {
        size.setHeight(qMax(size.height(), kBadgeHeight + 8));
    }
    return size;
}

}  // namespace kh::gui
