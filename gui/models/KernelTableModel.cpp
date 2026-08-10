#include "gui/models/KernelTableModel.h"

#include <QApplication>
#include <QBrush>
#include <QCollator>
#include <QDateTime>
#include <QFont>
#include <QIcon>
#include <QStyle>

#include <algorithm>

#include "core/model/SourceId.h"

namespace kh::gui {
namespace {

QIcon ThemeIcon(const QString &name, QStyle::StandardPixmap fallback) {
    if (QIcon::hasThemeIcon(name)) {
        return QIcon::fromTheme(name);
    }
    return QApplication::style()->standardIcon(fallback);
}

int StatusRank(kh::model::KernelStatus status) {
    switch (status) {
    case kh::model::KernelStatus::UpdateAvailable:
        return 0;
    case kh::model::KernelStatus::InstalledRunning:
        return 1;
    case kh::model::KernelStatus::Installed:
        return 2;
    case kh::model::KernelStatus::Available:
        return 3;
    }
    return 4;
}

}  // namespace

KernelTableModel::KernelTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int KernelTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : kernels_.size();
}

int KernelTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : kColumnCount;
}

QVariant KernelTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= kernels_.size()) {
        return {};
    }

    const kh::model::KernelInfo &kernel = kernels_.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return displayData(kernel, index.column());
    case Qt::DecorationRole:
        return decorationData(kernel, index.column());
    case Qt::ToolTipRole:
        return tooltipData(kernel, index.column());
    case Qt::ForegroundRole:
        if (kernel.status == kh::model::KernelStatus::UpdateAvailable) {
            return QBrush(QColor(20, 95, 165));
        }
        return {};
    case Qt::FontRole: {
        QFont font;
        if (kernel.isPinned || kernel.status == kh::model::KernelStatus::InstalledRunning) {
            font.setBold(true);
            return font;
        }
        return {};
    }
    case Qt::UserRole:
        return QVariant::fromValue(kernel);
    case Qt::UserRole + 1:
        return static_cast<int>(kernel.status);
    case Qt::UserRole + 2:
        return kernel.isPinned;
    default:
        return {};
    }
}

QVariant KernelTableModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case kSourceColumn:
        return QStringLiteral("Source");
    case kVersionColumn:
        return QStringLiteral("Version");
    case kReleasedColumn:
        return QStringLiteral("Released");
    case kStatusColumn:
        return QStringLiteral("Status");
    case kNotesColumn:
        return QStringLiteral("Notes");
    default:
        return {};
    }
}

Qt::ItemFlags KernelTableModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void KernelTableModel::sort(int column, Qt::SortOrder order) {
    if (column < 0 || column >= kColumnCount) {
        return;
    }
    sort_column_ = column;
    sort_order_ = order;
    beginResetModel();
    std::stable_sort(kernels_.begin(), kernels_.end(), [this](const auto &left, const auto &right) {
        const bool less = lessThan(left, right, sort_column_);
        if (sort_order_ == Qt::AscendingOrder) {
            return less;
        }
        return lessThan(right, left, sort_column_);
    });
    endResetModel();
}

void KernelTableModel::setKernels(QList<kh::model::KernelInfo> kernels) {
    beginResetModel();
    kernels_ = std::move(kernels);
    std::stable_sort(kernels_.begin(), kernels_.end(), [this](const auto &left, const auto &right) {
        if (sourcePriority(left.sourceId) != sourcePriority(right.sourceId)) {
            return sourcePriority(left.sourceId) < sourcePriority(right.sourceId);
        }
        return QString::localeAwareCompare(right.version, left.version) < 0;
    });
    endResetModel();
}

void KernelTableModel::setSourcePriorities(const QHash<int, int> &source_priorities) {
    source_priorities_ = source_priorities;
    sort(sort_column_, sort_order_);
}

const QList<kh::model::KernelInfo> &KernelTableModel::kernels() const {
    return kernels_;
}

kh::model::KernelInfo KernelTableModel::kernelAt(int row) const {
    if (row < 0 || row >= kernels_.size()) {
        return {};
    }
    return kernels_.at(row);
}

bool KernelTableModel::setKernelPinned(const QString &version, bool pinned) {
    for (int row = 0; row < kernels_.size(); ++row) {
        if (kernels_[row].version == version) {
            kernels_[row].isPinned = pinned;
            emit dataChanged(index(row, 0), index(row, kColumnCount - 1));
            return true;
        }
    }
    return false;
}

bool KernelTableModel::setKernelNote(const QString &version, const QString &note) {
    for (int row = 0; row < kernels_.size(); ++row) {
        if (kernels_[row].version == version) {
            kernels_[row].notes = note;
            emit dataChanged(index(row, kNotesColumn), index(row, kNotesColumn));
            return true;
        }
    }
    return false;
}

QString KernelTableModel::statusText(kh::model::KernelStatus status) {
    switch (status) {
    case kh::model::KernelStatus::Available:
        return QStringLiteral("Available");
    case kh::model::KernelStatus::Installed:
        return QStringLiteral("Installed");
    case kh::model::KernelStatus::InstalledRunning:
        return QStringLiteral("Installed (running)");
    case kh::model::KernelStatus::UpdateAvailable:
        return QStringLiteral("Update");
    }
    return QStringLiteral("Unknown");
}

QString KernelTableModel::accessibleStatusText(const kh::model::KernelInfo &kernel) {
    QString text = statusText(kernel.status);
    if (kernel.isPinned) {
        text.append(QStringLiteral(", pinned"));
    }
    if (kernel.compatibility.has_value() && !kernel.compatibility->compatible) {
        text.append(QStringLiteral(", incompatible"));
    }
    return text;
}

QVariant KernelTableModel::displayData(const kh::model::KernelInfo &kernel, int column) const {
    switch (column) {
    case kSourceColumn:
        return kernel.sourceDisplayName.isEmpty()
                   ? kh::model::SourceIdDisplayName(kernel.sourceId)
                   : kernel.sourceDisplayName;
    case kVersionColumn:
        return kernel.version;
    case kReleasedColumn:
        return kernel.releaseDate.isValid()
                   ? kernel.releaseDate.toLocalTime().date().toString(Qt::ISODate)
                   : QStringLiteral("Unknown");
    case kStatusColumn:
        return statusText(kernel.status);
    case kNotesColumn:
        return kernel.notes.simplified();
    default:
        return {};
    }
}

QVariant KernelTableModel::decorationData(const kh::model::KernelInfo &kernel, int column) const {
    if (column == kStatusColumn) {
        switch (kernel.status) {
        case kh::model::KernelStatus::Installed:
            return ThemeIcon(QStringLiteral("emblem-ok"), QStyle::SP_DialogApplyButton);
        case kh::model::KernelStatus::InstalledRunning:
            return ThemeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay);
        case kh::model::KernelStatus::UpdateAvailable:
            return ThemeIcon(QStringLiteral("software-update-available"), QStyle::SP_ArrowUp);
        case kh::model::KernelStatus::Available:
            return ThemeIcon(QStringLiteral("package-x-generic"), QStyle::SP_FileIcon);
        }
    }
    if (column == kNotesColumn && kernel.isPinned) {
        return ThemeIcon(QStringLiteral("emblem-favorite"), QStyle::SP_DialogYesButton);
    }
    return {};
}

QVariant KernelTableModel::tooltipData(const kh::model::KernelInfo &kernel, int column) const {
    if (column == kNotesColumn && !kernel.notes.isEmpty()) {
        return kernel.notes;
    }
    if (column == kStatusColumn) {
        return accessibleStatusText(kernel);
    }
    if (column == kSourceColumn && kernel.compatibility.has_value()) {
        return kernel.compatibility->reason;
    }
    return displayData(kernel, column);
}

bool KernelTableModel::lessThan(const kh::model::KernelInfo &left,
                                const kh::model::KernelInfo &right,
                                int column) const {
    if (column == kSourceColumn && sourcePriority(left.sourceId) != sourcePriority(right.sourceId)) {
        return sourcePriority(left.sourceId) < sourcePriority(right.sourceId);
    }

    switch (column) {
    case kSourceColumn:
        return QString::localeAwareCompare(displayData(left, column).toString(),
                                           displayData(right, column).toString()) < 0;
    case kVersionColumn:
        return QString::localeAwareCompare(left.version, right.version) < 0;
    case kReleasedColumn:
        return left.releaseDate < right.releaseDate;
    case kStatusColumn:
        return StatusRank(left.status) < StatusRank(right.status);
    case kNotesColumn:
        if (left.isPinned != right.isPinned) {
            return left.isPinned;
        }
        return QString::localeAwareCompare(left.notes, right.notes) < 0;
    default:
        return false;
    }
}

int KernelTableModel::sourcePriority(kh::model::SourceId source_id) const {
    return source_priorities_.value(static_cast<int>(source_id), static_cast<int>(source_id));
}

}  // namespace kh::gui
